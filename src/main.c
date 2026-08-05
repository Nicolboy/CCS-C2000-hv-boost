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
#include "calib.h"

extern uint16_t RamfuncsLoadStart;
extern uint16_t RamfuncsLoadSize;
extern uint16_t RamfuncsRunStart;

interrupt void cpu_timer0_isr(void);

// Dernier CommandState recu, expose au debogueur.
static volatile command_state_t g_last_cmd = {false, false, false};
static volatile bool s_send_telemetry = false;

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

// Priorite 2 > 3 > 4 > 5 > 1 : EMUSTOP est le code le moins prioritaire
// malgre son numero, pour ne jamais masquer une surintensite reelle.
static fault_code_t compute_fault_code(const safety_faults_t *f)
{
    if (f->overcurrent && f->stage1_fault)
    {
        return FAULT_OVERCURRENT_I1;
    }
    if (f->overcurrent && f->stage2_fault)
    {
        return FAULT_OVERCURRENT_I2;
    }
    if (s_overtemp_t1)
    {
        return FAULT_OVERTEMP_T1;
    }
    if (s_overtemp_t2)
    {
        return FAULT_OVERTEMP_T2;
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

// Priorite : surintensite > surtemperature > EMUSTOP > liaison perdue.
static led_state_t compute_led_state(const safety_faults_t *f)
{
    safety_faults_t faults = *f;

    if (faults.overcurrent)
    {
        return LED_STATE_OVERCURRENT;
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

    // ADC : declenche par ePWM1, donc apres pwm_init().
    adc_init();
    uart_link_init();

    // ---- BRING-UP : retest PWM + ADC, puissance DECONNECTEE ---------------
    // A RETIRER avant toute mise sous tension de la carte de conversion.
    // HV_EN et Stagex-EN restent a 0 (exclus de ce test).
    pwm_set_duty(STAGE_1, 0.5f);
    pwm_set_duty(STAGE_2, 0.5f);
    pwm_enable(STAGE_1, true);
    pwm_enable(STAGE_2, true);
    // -----------------------------------------------------------------------

    EALLOW;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    EDIS;

    InitCpuTimers();
    ConfigCpuTimer(&CpuTimer0, 60, 10000); // base 10 ms (voir cpu_timer0_isr)
    StartCpuTimer0(); // ConfigCpuTimer laisse TSS=1 (timer a l'arret) par conception

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; // TINT0

    EINT;
    ERTM;

    for (;;)
    {
        command_state_t cmd;
        safety_faults_t faults;

        if (uart_link_poll(&cmd))
        {
            g_last_cmd = cmd;
            s_ticks_since_cmd = 0U;
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
        if (faults.stage1_fault || faults.stage2_fault
            || s_overtemp_t1 || s_overtemp_t2)
        {
            enter_safe_state();
        }
        else
        {
            // Inhiber la decharge AVANT d'autoriser les etages : jamais
            // l'inverse, sinon on ferait travailler la conversion contre le
            // circuit de decharge.
            hv_discharge_set(false);
            stage_enable_set(STAGE_1, true);
            stage_enable_set(STAGE_2, true);
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
            t.duty1_pct = 50.0f;
            t.duty2_pct = 50.0f;
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
interrupt void cpu_timer0_isr(void)
{
    static uint16_t tick = 0;

    CpuTimer0.InterruptCount++;
    tick++;

    status_led_tick();

    if (s_ticks_since_cmd < LINK_TIMEOUT_TICKS)
    {
        s_ticks_since_cmd++; // sature au seuil, pas de rebouclage
    }

    if ((tick % 30U) == 0U)
    {
        s_send_telemetry = true; // telemetrie toutes les 300 ms
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
