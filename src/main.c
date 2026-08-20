#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "safety.h"
#include "uart_link.h"
#include "protocol.h"
#include "pwm.h"
#include "adc.h"
#include "measure.h"
#include "status_led.h"
#include "control.h"
#include "calib.h"

extern uint16_t RamfuncsLoadStart;
extern uint16_t RamfuncsLoadSize;
extern uint16_t RamfuncsRunStart;

interrupt void cpu_timer0_isr(void);
interrupt void cpu_timer1_isr(void);

// Dernier CommandState recu, expose au debogueur. Les consignes partent aux
// bornes basses : tant que l'ESP32 n'a rien envoye de valide, rien ne
// demarre (run reste false de toute facon).
//
// vout_set_v part a CTRL_VOSET_DISABLED, donc etage 2 DESACTIVE par defaut.
// C'est l'etat de la carte tant que le MOSFET, la diode et l'inductance de
// l'etage 2 ne sont pas montes ; le defaut le plus sur est de ne rien
// commander sur un etage absent. L'ESP32 doit envoyer un VOSET valide pour
// l'activer.
static volatile command_state_t g_last_cmd = {
    false,                   // ht_enabled
    false,                   // run
    CTRL_V1_SET_MIN_V,       // v1_set_v
    CTRL_VOSET_DISABLED,     // vout_set_v : etage 2 desactive
    false, false             // pwm1/pwm2, historiques
};
static volatile bool s_send_telemetry = false;

// Consignes refusees depuis le demarrage, remontees en telemetrie : c'est
// le seul moyen pour l'ESP32 de savoir qu'une commande n'a pas ete prise.
static uint16_t s_rejected_count = 0;

// Age de la derniere trame $C valide, en ticks de 10 ms. Incremente par
// l'ISR, remis a zero par la boucle principale a chaque commande valide.
// Sature pour ne jamais reboucler.
#define TICK_MS                 10U
#define LINK_TIMEOUT_TICKS      ((uint16_t)(UART_LINK_TIMEOUT_MS / TICK_MS))
static volatile uint16_t s_ticks_since_cmd = LINK_TIMEOUT_TICKS;

// Surtemperature, suivie PAR VOIE : le protocole distingue les codes 4 (T1)
// et 5 (T2), un simple maximum ne permettrait pas de les separer.
// Hysteresis pour eviter le battement autour du seuil.
static bool s_overtemp_t1 = false;
static bool s_overtemp_t2 = false;

static bool update_one_overtemp(bool current, float temp_c)
{
    if (!current)
    {
        return (temp_c >= SAFETY_OVERTEMP_C);
    }
    return (temp_c > (SAFETY_OVERTEMP_C - SAFETY_OVERTEMP_HYST_C));
}

static void update_overtemp(void)
{
    s_overtemp_t1 =
        update_one_overtemp(s_overtemp_t1, measure_temp(adc_get_raw(ADC_CH_T1)));
    s_overtemp_t2 =
        update_one_overtemp(s_overtemp_t2, measure_temp(adc_get_raw(ADC_CH_T2)));
}

// Autorise la conversion : sorties PWM debridees et etages valides. Le duty
// lui-meme est entierement pilote par control.c, qui part de la tension
// mesuree et rampe -- il n'y a plus aucune valeur de duty en dur ici.
//
// Ordre impose : la decharge est inhibee AVANT d'autoriser les etages,
// sinon la conversion travaillerait contre le circuit de decharge.
// Chemin de puissance deja arme ? enable_power_path() est appelee a CHAQUE
// tour de boucle tant que RUN est vrai, alors que la mise a zero du duty
// qu'elle contient ne vaut qu'a l'armement. Sans ce garde-fou, la boucle
// principale ecrase en permanence le duty calcule par control.c, qui ne le
// releve qu'au tick de regulation decime : le PWM sort alors par salves
// separees de longs trous (observe au scope). Remis a false par
// enter_safe_state(), seul chemin de retour au repos.
static bool s_power_path_armed = false;

#if STAGE2_OPENLOOP_TEST
// Commande manuelle de Stage2-EN pendant l'essai, A ECRIRE DEPUIS LE
// DEBOGUEUR. Mise a 0, elle ferme la porte ET IC9 en laissant la sortie
// EPWM2A commuter : c'est le seul moyen d'observer l'effet de la porte
// SEULE. Une ecriture directe sur GPIO17 ne tiendrait pas -- la boucle
// principale reappelle enable_power_path() a chaque tour et la reecrirait
// en quelques microsecondes ; un point d'arret ne convient pas non plus,
// FREE_SOFT = 0 gelant le compteur ePWM avec le CPU.
//
// volatile et NON static : le symbole doit survivre a -O2 et rester
// accessible au debogueur. Disparait avec le bloc d'essai.
volatile bool g_s2_en_test = true;
#endif

static void enable_power_path(void)
{
    // Etage 2 desactive (consigne de sortie nulle) : sa porte ET reste
    // fermee et sa sortie ePWM inhibee. control.c maintient deja son duty a
    // zero, mais ne pas armer la porte rend l'etage franchement inerte --
    // ce qui est observable au scope, donc verifiable.
    bool s2 = control_s2_enabled();

#if STAGE2_OPENLOOP_TEST
    // Essai en boucle ouverte : control.c sort un duty fixe sur l'etage 2
    // sans que s_s2_enabled soit vrai. Sans cet armement, la porte ET IC9
    // resterait fermee et la sortie ePWM2 inhibee -- le duty existerait dans
    // CMPA sans jamais atteindre le driver. Voir calib.h.
    s2 = true;
#endif

    hv_discharge_set(false);

    // Uniquement a l'armement : demarrer a duty nul est voulu (control.c
    // rampe ensuite depuis la tension mesuree), le maintenir a zero ne
    // l'est pas.
    if (!s_power_path_armed)
    {
        pwm_set_duty(STAGE_1, 0.0f);
        pwm_set_duty(STAGE_2, 0.0f);
        s_power_path_armed = true;
    }

    pwm_enable(STAGE_1, true);
    pwm_enable(STAGE_2, s2);
    stage_enable_set(STAGE_1, true);
#if STAGE2_OPENLOOP_TEST
    // La sortie ePWM2 reste debridee ci-dessus : on ne coupe QUE la porte,
    // sinon les deux mecanismes agiraient ensemble et la mesure ne dirait
    // pas lequel a produit l'effet observe.
    stage_enable_set(STAGE_2, s2 && g_s2_en_test);
#else
    stage_enable_set(STAGE_2, s2);
#endif
}

// Priorite 2 > 3 > 4 > 5 > 1 : EMUSTOP est le code le moins prioritaire
// malgre son numero, pour ne jamais masquer une surintensite reelle.
static bool link_lost(void)
{
    return (s_ticks_since_cmd >= LINK_TIMEOUT_TICKS);
}

static fault_code_t compute_fault_code(const safety_faults_t *f)
{
    fault_code_t ctrl_fault;

    if (f->overcurrent && f->stage1_fault)
    {
        return FAULT_OVERCURRENT_I1;
    }
    if (f->overcurrent && f->stage2_fault)
    {
        return FAULT_OVERCURRENT_I2;
    }

    // Survoltage V1/VOUT et sous-tension VIN : detectes par control.c dans
    // l'ISR ADC, sur les valeurs brutes. Le code remonte tel quel.
    ctrl_fault = control_get_fault();
    if (ctrl_fault != FAULT_NONE)
    {
        return ctrl_fault;
    }

    if (s_overtemp_t1)
    {
        return FAULT_OVERTEMP_T1;
    }
    if (s_overtemp_t2)
    {
        return FAULT_OVERTEMP_T2;
    }
    if (link_lost())
    {
        return FAULT_LINK_LOST;
    }
    if (f->emustop)
    {
        return FAULT_EMUSTOP;
    }
    return FAULT_NONE;
}

// Etat sur : les deux etages inhibes ET la sortie HT coupee.
//
// Couper HV_EN est indispensable et n'a rien de redondant : un boost a 0 %
// de duty ne donne PAS 0 V en sortie, le chemin Vin -> L -> diode -> Cout
// reste passant en permanence. HV_EN (VOM1271) est le seul organe qui isole
// reellement la charge (PROMPT §6 etape 6).
static void enter_safe_state(void)
{
    // Le prochain enable_power_path() devra re-armer, donc repartir de zero.
    s_power_path_armed = false;

    control_set_run(false);
    pwm_enable(STAGE_1, false);
    pwm_enable(STAGE_2, false);
    stage_enable_set(STAGE_1, false);
    stage_enable_set(STAGE_2, false);
    hv_enable_set(false);

    // Decharge active. Elle vient APRES la coupure de HV_EN : la charge est
    // d'abord isolee, puis le condensateur vide en ~2 s. Sans elle, les
    // 400 V resteraient presents pres d'une minute sur le seul bleeder de
    // 1 MOhm, sans aucune indication une fois l'alimentation coupee.
    hv_discharge_set(true);
}

// Priorite : surintensite > surtension > surtemperature > EMUSTOP >
// liaison perdue. Le meme ordre que compute_fault_code(), pour que la LED
// et le code remonte a l'IHM ne puissent jamais designer deux causes
// differentes.
static led_state_t compute_led_state(const safety_faults_t *f)
{
    safety_faults_t faults = *f;

    if (faults.overcurrent)
    {
        return LED_STATE_OVERCURRENT;
    }
    // Les surtensions sont detectees par control.c, PAS par safety.c : elles
    // n'apparaissent donc pas dans safety_faults_t. Les oublier ici laissait
    // la LED sur le motif nominal alors que la puissance etait coupee et
    // verrouillee -- un affichage rassurant et faux, decouvert au banc apres
    // la destruction du MOSFET par surtension a vide.
    if (control_get_fault() == FAULT_UNDERVOLTAGE_VIN)
    {
        return LED_STATE_UNDERVOLTAGE;
    }
    if (control_get_fault() != FAULT_NONE)
    {
        return LED_STATE_OVERVOLTAGE;
    }
    if (s_overtemp_t1 || s_overtemp_t2)
    {
        return LED_STATE_OVERTEMP;
    }
    if (faults.emustop)
    {
        return LED_STATE_EMUSTOP;
    }
    if (s_ticks_since_cmd >= LINK_TIMEOUT_TICKS)
    {
        return LED_STATE_LINK_LOST;
    }
    return LED_STATE_NOMINAL;
}

void main(void)
 {
#ifdef _FLASH
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
#endif

    bsp_clock_init();
    bsp_gpio_leds_init();
    status_led_init(); // bleu fixe des le depart : "je suis parti"

    DINT;
    InitPieCtrl();
    IER = 0x0000;
    IFR = 0x0000;
    InitPieVectTable();

    bsp_gpio_analog_init();
    bsp_gpio_control_init();

    // Ordre d'init (PROMPT §7) : GPIO en etat sur -> AIOMUX1 -> horloge ->
    // securite -> ADC -> UART -> PWM en dernier.
    //
    // pwm_init() est appele AVANT safety_init() pour une raison materielle :
    // il active PCLKCR1.EPWMxENCLK, sans quoi les ecritures de safety_init()
    // dans les registres ePWM (TZSEL, TZCTL, DCTRIPSEL) seraient perdues.
    // L'esprit de la regle est respecte : pwm_init() laisse les deux sorties
    // forcees a l'etat bas (AQCSFRC) et le duty a 0, donc rien ne sort tant
    // que le Trip Zone n'est pas arme.
    pwm_init();

    // Protection complete : comparateurs + Digital Compare + Trip Zone, et
    // TZ6/EMUSTOP. Les entrees shunt sont maintenant reellement cablees, le
    // comparateur ne declenchera donc pas spontanement.
    safety_init();

    // control_init() lit les periodes ePWM, donc apres pwm_init(), et avant
    // adc_init() dont l'ISR appellera control_fast_check().
    control_init();

    // ADC : declenche par ePWM1, donc apres pwm_init().
    adc_init();
    uart_link_init();

    // Consignes de depart aux bornes basses, coherentes avec g_last_cmd.
    // Rien ne demarre tant que l'ESP32 n'a pas envoye RUN=1 : le harnais de
    // bring-up qui lancait la conversion d'office a ete retire.
    (void)control_set_setpoints(CTRL_V1_SET_MIN_V, CTRL_VOSET_DISABLED);

    EALLOW;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    PieVectTable.TINT1 = &cpu_timer1_isr;
    EDIS;

    InitCpuTimers();
    ConfigCpuTimer(&CpuTimer0, 60, 10000); // base 10 ms (voir cpu_timer0_isr)
    ConfigCpuTimer(&CpuTimer1, 60, CTRL_TICK_PERIOD_US); // pas de regulation
    StartCpuTimer0(); // ConfigCpuTimer laisse TSS=1 (timer a l'arret) par conception
    StartCpuTimer1();

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; // TINT0

    // CPU Timer 1 : INT13, cable directement sur le coeur, donc pas de PIEIER.
    IER |= M_INT13;

    EINT;
    ERTM;

    for (;;)
    {
        // On PART de la commande courante : parse_command() ne renseigne que
        // les tags presents dans la trame, et le protocole veut qu'un champ
        // absent garde sa derniere valeur. Sans cette copie, `cmd` serait une
        // variable de pile non initialisee -- une valeur residuelle pourrait
        // activer RUN ou imposer une consigne arbitraire.
        command_state_t cmd = g_last_cmd;
        safety_faults_t faults;

        if (uart_link_poll(&cmd))
        {
            s_ticks_since_cmd = 0U;

            // Consignes hors bornes : REFUSEES en bloc. L'ancienne reste
            // appliquee et le refus est compte, plutot que de saturer
            // silencieusement -- une erreur de commande doit se voir.
            if (control_set_setpoints(cmd.v1_set_v, cmd.vout_set_v))
            {
                g_last_cmd = cmd;
            }
            else
            {
                s_rejected_count++;
                // On retient quand meme HT et RUN : refuser une consigne
                // numerique ne doit pas empecher un ordre d'arret de passer.
                g_last_cmd.ht_enabled = cmd.ht_enabled;
                g_last_cmd.run = cmd.run;
            }
        }

        update_overtemp();

        faults = safety_get_fault_flags();

        // ARRET GLOBAL : n'importe quel defaut, sur n'importe quel etage,
        // arrete l'ensemble. Sur un boost en cascade, laisser tourner
        // l'etage 2 alors que l'etage 1 est coupe n'a pas de sens : il
        // travaillerait sans tension d'entree.
        //
        // Les Trip Zones restent independantes en materiel (une surintensite
        // etage 2 ne coupe que EPWM2A) : cet arret global est la couche
        // systeme qui vient PAR-DESSUS. Le logiciel peut inhiber en plus,
        // jamais outrepasser la protection materielle (PROMPT §2).
        //
        // Aucun redemarrage automatique : rien ne reactive le PWM ensuite,
        // meme si la temperature redescend sous l'hysteresis ou si le defaut
        // disparait. Il faut un reset, ou une commande explicite (etape 8).
        if (faults.overcurrent || s_overtemp_t1 || s_overtemp_t2
            || (control_get_fault() != FAULT_NONE))
        {
            // Defaut de PUISSANCE : verrouille jusqu'au cycle d'alimentation
            // (PROMPT §8). Aucun acquittement, ni automatique ni par UART.
            control_trip();
            enter_safe_state();
        }
        else if (link_lost())
        {
            // Liaison ESP32 perdue au-dela de 2 s : repli en etat sur, exige
            // par le PROMPT §6 etape 7. La securite ne depend JAMAIS de
            // l'ESP32 -- on ne reste pas en conversion sans superviseur.
            //
            // Ce n'est pas un defaut verrouille : la conversion peut
            // redemarrer des que la liaison revient et que RUN est redemande.
            enter_safe_state();
        }
        else if (faults.emustop)
        {
            // EMUSTOP seul : ce n'est pas un defaut de puissance mais un
            // artefact du debogueur. On autorise donc la reprise -- mais en
            // repassant par la sequence de demarrage complete, jamais en
            // reprenant la conversion la ou elle s'etait arretee.
            //
            // L'effacement ne peut aboutir qu'ici, CPU en marche : tant que
            // le coeur est halte, EMUSTOP reste asserte et le drapeau se
            // re-verrouille aussitot.
            //
            // La liaison est relancee aussi : pendant la halte l'ESP32 a
            // continue d'emettre, l'anneau de reception a deborde et la
            // ligne en cours est tronquee.
            uart_link_restart();
            safety_clear_faults();

            // Desarmer avant de re-armer : la reprise apres EMUSTOP est une
            // sequence de demarrage complete, donc duty repart de zero.
            s_power_path_armed = false;

            // Et la machine d'etat AVEC. Remettre CMPA a zero ne suffit pas :
            // l'integrateur de control.c conserve sinon le duty d'avant la
            // halte et le reimpose des le pas de regulation suivant, sur une
            // sortie qui s'est videe dans la charge pendant l'arret. Le
            // courant d'inductance s'emballe alors en quelques periodes et la
            // protection coupe -- constate en debogage a 10 V d'entree, ou le
            // duty eleve rend le phenomene le plus violent.
            control_restart();
            enable_power_path();
        }
        else if (g_last_cmd.run)
        {
            // Marche demandee : on ouvre le chemin de puissance, control.c
            // pilote le duty depuis la tension mesuree en rampant.
            enable_power_path();
            control_set_run(true);

            // HV_EN n'est autorise qu'une fois les DEUX etages etablis, et
            // seulement si l'operateur l'a demande. Un boost a 0 % de duty
            // ne donne pas 0 V : HV_EN reste le seul organe qui isole
            // reellement la charge (PROMPT §6 etape 6).
            hv_enable_set(g_last_cmd.ht_enabled && control_hv_allowed());
        }
        else
        {
            // Repos : etat sur complet. Le chemin de puissance n'est PAS
            // arme tant que la marche n'est pas demandee -- sinon on
            // inhiberait la decharge et on armerait les sorties pour rien.
            enter_safe_state();
        }

        status_led_set_state(compute_led_state(&faults));

        if (s_send_telemetry)
        {
            telemetry_t t;

            // Grandeurs physiques reelles (etape 5) : conversion par
            // measure.c a partir des coefficients mesures sur la carte
            // (docs/mesure-cartepuissance.md).
            // FREQ/DUTY restent les valeurs de consigne du bloc de bring-up
            // ci-dessus, pas encore une mesure.
            t.freq1_hz = (float)PWM_STAGE1_FREQ_HZ;
            t.freq2_hz = (float)PWM_STAGE2_FREQ_HZ;
            // Duty reellement en sortie, relu depuis CMPA. Pendant le
            // balayage la valeur change bien plus vite que la cadence de
            // telemetrie : c'est un echantillon, pas un suivi.
            t.duty1_pct = pwm_get_duty(STAGE_1) * 100.0f;
            t.duty2_pct = pwm_get_duty(STAGE_2) * 100.0f;
            t.vin_v = measure_vin(adc_get_raw(ADC_CH_VIN));
            t.iin_a = measure_iin(adc_get_raw(ADC_CH_IIN));
            t.v1_v = measure_v1(adc_get_raw(ADC_CH_V1));
            t.i1_a = measure_i1(adc_get_raw(ADC_CH_I1));
            t.t1_c = measure_temp(adc_get_raw(ADC_CH_T1));
            t.vout_v = measure_vout(adc_get_raw(ADC_CH_VOUT));
            t.i2_a = measure_i2(adc_get_raw(ADC_CH_I2));
            t.t2_c = measure_temp(adc_get_raw(ADC_CH_T2));
            t.iout_a = measure_iout(adc_get_raw(ADC_CH_IOUT));
            t.fault = compute_fault_code(&faults);

            t.state = control_get_state();
            t.v1_setpoint_v = control_get_v1_setpoint();
            t.vout_setpoint_v = control_get_vout_setpoint();
            t.rejected = s_rejected_count;

            uart_link_send_telemetry(&t);
            s_send_telemetry = false;
        }

        // Emission en tache de fond : pousse au plus 4 octets (profondeur de
        // la FIFO) puis rend la main. Ne doit jamais retarder la regulation.
        uart_link_service_tx();
    }
}

// Base de temps 10 ms. Le clignotement des signaux d'etage a ete retire :
// GPIO1/GPIO3 (Stagex-default) appartiennent maintenant aux comparateurs, et
// Stagex-EN est fige a 1 pour rendre le declenchement observable sur les
// sorties des portes ET. HV_EN reste a 0.
//
// Les LED n'affichent plus l'etat de l'ADC mais celui des defauts, pilotees
// depuis la boucle principale.
// Aucune multiplication ni division ici : le rythme de la telemetrie est
// obtenu par un compteur qui reboucle sur comparaison, pas par un modulo
// (division logicielle sur C28x).
#define TELEMETRY_PERIOD_TICKS  30U // 300 ms

interrupt void cpu_timer0_isr(void)
{
    static uint16_t telemetry_tick = 0;

    CpuTimer0.InterruptCount++;

    status_led_tick();

    if (s_ticks_since_cmd < LINK_TIMEOUT_TICKS)
    {
        s_ticks_since_cmd++; // sature au seuil, pas de rebouclage
    }

    telemetry_tick++;
    if (telemetry_tick >= TELEMETRY_PERIOD_TICKS)
    {
        telemetry_tick = 0U;
        s_send_telemetry = true;
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}

// Pas de regulation, a CTRL_TICK_PERIOD_US. Le CPU Timer 1 est cable
// DIRECTEMENT sur INT13, hors du PIE : il n'y a donc ni PIEIER a armer ni
// PIEACK a rendre, seulement le drapeau du timer a effacer.
//
// Ce contexte a ete choisi pour sa position dans la hierarchie : INT13 passe
// APRES l'ISR ADC (INT1), qui garde donc sa latence de detection de
// survoltage quoi que fasse la regulation, et AVANT l'UART (groupe 9),
// conformement a la regle "la regulation est prioritaire, l'envoi UART se
// fait s'il reste du temps".
interrupt void cpu_timer1_isr(void)
{
    control_tick();

    CpuTimer1Regs.TCR.bit.TIF = 1;
}
