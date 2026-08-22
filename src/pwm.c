#include "DSP28x_Project.h"
#include "pwm.h"
#include "calib.h"
#include "sfo_v6.h"

// Mode up-count, TBCLK = SYSCLKOUT = 60 MHz (HSPCLKDIV = CLKDIV = /1).
// TBPRD = SYSCLKOUT / Fpwm - 1  ->  299 a 200 kHz, 599 a 100 kHz.
//
// HRPWM : implemente le 22/08/2026. Le POURQUOI est dans calib.h (bloc
// "HRPWM -- RESOLUTION FINE DU RAPPORT CYCLIQUE") -- en un mot, a D = 0,9 un
// count de l'etage 2 vaut 8,3 V de sortie, et la regulation ne peut alors
// que battre entre deux counts. Ici, le COMMENT.

typedef struct
{
    uint32_t freq_hz;
    float duty;
    bool enabled;
} pwm_stage_t;

static pwm_stage_t s_stage[2];

// =====================================================================
// SYMBOLES EXIGES PAR LA BIBLIOTHEQUE SFO
// =====================================================================
//
// SFO_TI_Build_V6.lib laisse exactement trois symboles indefinis :
// _EPwm1Regs, _MEP_ScaleFactor et _ePWM (releve a nm2000). Les deux
// derniers sont a notre charge, et leurs NOMS SONT IMPOSES -- ni statiques,
// ni renommables.
//
// MEP_ScaleFactor : nombre de pas MEP dans un cycle de TBCLK, ecrit par
// SFO() et recopie par elle dans EPwm1Regs.HRMSTEP. C'est HRMSTEP que le
// materiel utilise pour convertir la fraction de CMPAHR en pas MEP quand
// AUTOCONV est arme. Valeur attendue a 60 MHz : 16,67 ns / ~150 ps, soit
// de l'ordre de 100 a 110. Au-dela de 255 la conversion automatique ne
// fonctionne plus et SFO() renvoie SFO_ERROR.
int MEP_ScaleFactor;

// Tableau impose par la bibliotheque, PWM_CH = 5 entrees (sfo_v6.h).
// L'indice 0 est un emplacement mort dans la convention de TI, et la
// calibration se fait sur l'indice 1.
//
// LE F28027 A QUATRE MODULES ePWM, MAIS SEULS LES DEUX PREMIERS ONT LEUR
// HORLOGE ARMEE ICI (PCLKCR1, voir pwm_init). L'exemple de TI place
// EPwm3Regs et EPwm4Regs dans les deux dernieres cases ; les reprendre
// telles quelles ferait acceder la bibliotheque a des peripheriques non
// horloges, dont les acces sont sans effet et les lectures sans garantie.
// On y remet donc EPwm1Regs : si la bibliotheque ne s'en sert pas, c'est
// sans consequence ; si elle s'en sert, elle tombe sur un module valide.
volatile struct EPWM_REGS *ePWM[PWM_CH] = {
    &EPwm1Regs, &EPwm1Regs, &EPwm2Regs, &EPwm1Regs, &EPwm1Regs};

#if PWM_HRPWM_EDGE_TEST
// ESSAI DE FRONT, BANC UNIQUEMENT (PWM_HRPWM_EDGE_TEST, calib.h).
//
// A ecrire depuis le debogueur. g_hr_test_coarse fige la partie entiere de
// CMPA sur l'etage 1 ; g_hr_test_frac balaie la SEULE partie fractionnaire,
// de 0 a 255. Le front qui se deplace au scope designe celui que le MEP
// commande, donc la bonne valeur de PWM_HRPWM_EDGMODE.
//
// volatile et NON static : le symbole doit survivre a l'optimisation et
// rester accessible au debogueur.
volatile uint16_t g_hr_test_coarse = 150U; // 50 % a 200 kHz
volatile uint16_t g_hr_test_frac = 0U;
#endif

static bool stage_has_hrpwm(stage_id_t stage)
{
#if PWM_HRPWM_STAGE1 && PWM_HRPWM_STAGE2
    (void)stage;
    return true;
#elif PWM_HRPWM_STAGE1
    return (stage == STAGE_1);
#elif PWM_HRPWM_STAGE2
    return (stage == STAGE_2);
#else
    (void)stage;
    return false;
#endif
}

static volatile struct EPWM_REGS *pwm_regs(stage_id_t stage)
{
    return (stage == STAGE_1) ? &EPwm1Regs : &EPwm2Regs;
}

static uint16_t stage_index(stage_id_t stage)
{
    return (stage == STAGE_1) ? 0U : 1U;
}

// Place CMPB, qui declenche la sequence ADC, au MILIEU de la conduction du
// MOSFET. La conduction va de CTR=0 a CMPA, le point vise est donc CMPA>>1.
//
// ADC_TRIG_LEAD_COUNTS avance le declenchement de la fenetre d'acquisition,
// l'ADC echantillonnant a sa FIN. I1 etant en tete de sequence, son instant
// d'echantillonnage tombe alors bien au point vise.
//
// Uniquement un decalage, une soustraction et une comparaison : appelable
// depuis l'ISR ADC sans enfreindre la regle "ni multiplication ni division".
//
// A duty faible le groupe deborde sur le blocage -- limite structurelle
// documentee dans adc.h, pas un defaut de reglage. La protection contre les
// surintensites n'en depend pas : elle passe par les comparateurs
// analogiques, en continu, et n'echantillonne jamais.
//
// ---------------------------------------------------------------------
// ESSAI DU 17/08/2026 AUX TROIS QUARTS : ANNULE.
//
// Motif de l'essai : au scope (200,85 kHz, duty 38,1 %), la sortie de l'ampli
// de shunt met environ 1 us a s'etablir apres l'amorcage -- pointe a
// -149,84 V/us sur le front, puis une bosse et un creux avant la rampe
// propre. A mi-conduction (949 ns dans ce cas) l'ADC echantillonne donc dans
// ce transitoire, et I1 lit environ 15 % SOUS le courant d'entree.
//
// Cette microseconde n'est PAS la commutation du MOSFET : a 46 V et 1 A, une
// transition de 1 us dissiperait 4,6 W a 200 kHz, soit plus du double des
// 1,9 W de pertes totales mesurees. C'etait la chaine de mesure qui
// s'etablissait : l'ampli, depourvu de toute resistance serie en entree,
// encaissait la pointe L.di/dt du shunt en direct. RESOLU le 18/08/2026 par
// un RC d'entree 1 kOhm / 100 pF (tau = 100 ns, contre 530 ns de QUALSEL
// deja assumes) -- la rampe est lineaire des la sortie du front.
//
// ---------------------------------------------------------------------
// ECART RESIDUEL SUR L'INSTANT REEL D'ECHANTILLONNAGE -- non elucide.
//
// Le code vise la mi-conduction : CMPB = CMPA/2 - ADC_TRIG_LEAD_COUNTS, et
// I1 etant en tete de sequence, sa fenetre d'acquisition se referme donc a
// CMPA/2. Mesure au banc (18/08/2026, Vin 24,1 V, V1 45,42 V, I_in 1,00 A,
// L 47 uH) : I1 remonte 1,23 A, soit 0,731 V a la broche, soit un courant de
// 1,207 A -- alors que la mi-conduction vaut exactement I_in = 1,00 A. Sur
// une rampe de 0,405 a 1,595 A, cela place l'echantillon aux ENVIRON DEUX
// TIERS de la conduction, soit ~400 ns plus tard que vise.
//
// Le RC d'entree ne l'explique pas : sur une rampe, un retard de 100 ns fait
// lire PLUS BAS, pas plus haut. Piste restante : latence de declenchement
// entre l'evenement SOC de l'ePWM et l'ouverture reelle du S/H.
//
// CONSEQUENCE, A CONNAITRE ET A NE PAS PRENDRE POUR UNE PANNE : I1 remonte
// systematiquement ~20 % AU-DESSUS du courant d'entree. Ce n'est pas une
// erreur de gain -- le gain est mesure par deux routes concordantes -- c'est
// l'endroit de la rampe qui est echantillonne. I1 NE DOIT DONC PAS ETRE
// COMPARE A IIN. Sans consequence pour la securite : la protection est
// analogique et continue, elle n'echantillonne jamais. Deplacer ce point ne decale
// pas seulement I1, il decale LES NEUF VOIES de la sequence, qui s'enchainent
// a 550 ns d'intervalle. A D = 0,478 (conduction 2,39 us) V1 passait de
// 0,65 us AVANT le blocage a 50 ns avant : la variable regulee tombait sur le
// front d'extinction. Constate au banc dans la minute : le rapport cyclique
// s'est mis a osciller, et avec lui le courant d'entree.
//
// Le bouclage est direct : bruit de commutation sur V1 -> l'integrateur
// corrige -> le duty bouge -> le front se deplace -> le bruit change.
//
// LEÇON A RETENIR. Un seul point de declenchement (CMPB) sert toute la
// sequence, et les quatre voies rapides occupent 132 counts alors que la
// conduction n'en dure que ~143 a ce rapport cyclique : il n'y a pas de place
// pour placer I1 ailleurs qu'au debut sans expulser les autres. Tant que
// l'ADC est declenche par un compare unique, l'instant de I1 et celui de V1
// ne sont pas reglables separement.
//
// I1 lit donc environ 15 % sous le courant d'entree, l'echantillonnage
// tombant dans l'etablissement de la chaine de mesure (~1 us apres
// l'amorcage, releve au scope). C'EST ACCEPTE : I1 est une voie de
// DIAGNOSTIC, la protection est analogique et continue, et elle
// n'echantillonne jamais. On ne destabilise pas la regulation pour ameliorer
// un affichage. Si la valeur doit etre juste, c'est le gain qu'il faut
// etalonner A CET INSTANT-LA, pas l'instant qu'il faut deplacer.
static void pwm_apply_adc_trigger(volatile struct EPWM_REGS *p)
{
    uint16_t mid = (uint16_t)(p->CMPA.half.CMPA >> 1);

    p->CMPB = (mid > (uint16_t)ADC_TRIG_LEAD_COUNTS)
                  ? (uint16_t)(mid - (uint16_t)ADC_TRIG_LEAD_COUNTS)
                  : 1U;
}

// Calibration MEP valide ? Tant qu'elle ne l'est pas, la partie
// fractionnaire est IGNOREE et la carte se comporte exactement comme avant
// HRPWM. C'est le repli voulu : HRMSTEP mal renseigne ne donne pas une
// resolution approximative, il donne des pas MEP de taille fausse, donc une
// commande potentiellement non monotone -- pire que pas de HRPWM du tout.
static bool s_hrpwm_ok = false;

// Recalcule CMPA a partir du duty et du TBPRD courants.
static void pwm_apply_duty(stage_id_t stage)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);
    float duty = s_stage[stage_index(stage)].duty;
    uint32_t period_q8 = ((uint32_t)(p->TBPRD + 1U)) << PWM_DUTY_FRAC_BITS;

    // En up-count avec AQ_SET a zero et AQ_CLEAR sur CMPA, la sortie est
    // haute pendant CMPA cycles : duty = CMPA / (TBPRD + 1).
    pwm_set_duty_q8(stage, (uint32_t)(duty * (float)period_q8 + 0.5f));
}

void pwm_init(void)
{
    EALLOW;
    SysCtrlRegs.PCLKCR1.bit.EPWM1ENCLK = 1;
    SysCtrlRegs.PCLKCR1.bit.EPWM2ENCLK = 1;

    // Les bases de temps sont figees pendant la configuration puis relachees
    // ensemble, pour que les deux etages demarrent leur compteur en phase.
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;

    EDIS;

    s_stage[0].freq_hz = PWM_STAGE1_FREQ_HZ;
    s_stage[1].freq_hz = PWM_STAGE2_FREQ_HZ;

    {
        stage_id_t stages[2] = {STAGE_1, STAGE_2};
        uint16_t i;

        for (i = 0U; i < 2U; i++)
        {
            stage_id_t s = stages[i];
            volatile struct EPWM_REGS *p = pwm_regs(s);

            p->TBCTL.bit.CTRMODE = TB_COUNT_UP;
            p->TBCTL.bit.PHSEN = TB_DISABLE;
            p->TBCTL.bit.PRDLD = TB_SHADOW;
            p->TBCTL.bit.HSPCLKDIV = TB_DIV1;
            p->TBCTL.bit.CLKDIV = TB_DIV1;
            // FREE_SOFT = 0 : le compteur gele quand le CPU s'arrete. Ne
            // suffit PAS a mettre la sortie a l'etat bas (l'Action Qualifier
            // a deja positionne la broche, qui reste figee) -- c'est TZ6 =
            // EMUSTOP dans safety.c qui garantit la mise a zero.
            p->TBCTL.bit.FREE_SOFT = 0;

            // DECALAGE DE PHASE ENTRE LES DEUX ETAGES.
            //
            // Les deux compteurs sont charges ICI, TBCLKSYNC etant a zero
            // (voir plus haut) : ils demarreront donc ensemble, sur la meme
            // TBCLK, avec un rapport de periode exactement 2:1. La phase qui
            // en resulte est FIXE et reproductible -- ce n'est pas un alea de
            // demarrage, c'est un choix, et il doit etre fait.
            //
            // Le laisser a zero sur les deux, comme c'etait le cas jusqu'au
            // 20/08/2026, met les deux commutations AU MEME INSTANT une
            // periode sur deux : les parasites des deux grilles s'ajoutent.
            // Visible au scope sur I1, dont une impulsion sur deux porte une
            // amplitude differente de l'autre.
            //
            // PHSEN reste a TB_DISABLE : on ne se sert pas de la chaine de
            // synchronisation (elle imposerait de toute facon sa cadence a
            // l'esclave, ce qui ramenerait l'etage 2 a 200 kHz). Le decalage
            // initial suffit : rien ne resynchronise ensuite, et deux
            // compteurs issus de la meme horloge ne derivent pas.
            p->TBCTR = (s == STAGE_2) ? PWM_STAGE2_PHASE_COUNTS : 0U;
            p->TBPHS.half.TBPHS = 0;

            p->CMPCTL.bit.SHDWAMODE = CC_SHADOW;
            p->CMPCTL.bit.LOADAMODE = CC_CTR_ZERO;

            // CMPB porte l'instant de declenchement de l'ADC, pas une sortie.
            // Meme regime d'ombre que CMPA : les deux doivent basculer au meme
            // passage a zero, sinon l'instant de mesure correspondrait a un
            // duty different de celui reellement applique dans la periode.
            p->CMPCTL.bit.SHDWBMODE = CC_SHADOW;
            p->CMPCTL.bit.LOADBMODE = CC_CTR_ZERO;

            // Haut a zero, bas sur CMPA -> impulsion en debut de periode.
            p->AQCTLA.bit.ZRO = AQ_SET;
            p->AQCTLA.bit.CAU = AQ_CLEAR;

            // INVERSION DE POLARITE DE SORTIE (driver UCC27517 inverseur :
            // IN bas -> OUT haut -> MOSFET passant). On inverse via le
            // sous-module Dead-Band, qui porte le seul vrai bit de polarite
            // de l'ePWM, plutot qu'en retournant l'Action Qualifier.
            //
            // C'est ce placement dans la chaine qui compte. L'ordre des
            // sous-modules est AQ -> DB -> chopper -> TZ -> broche :
            //  - AQCSFRC (dans l'AQ) est EN AMONT : son forcage a l'etat bas
            //    ressort haut sur la broche, donc MOSFET bloque. Inchange.
            //  - TZCTL est EN AVAL : TZ_FORCE_LO mettrait la broche a l'etat
            //    bas, donc le MOSFET PASSANT sur defaut. Bascule en
            //    TZ_FORCE_HI dans safety.c -- indissociable de cette ligne.
            //
            // DB_ACTV_LO inverse les deux sorties. EPWMxB n'etant pas route
            // sur une broche, inverser les deux est sans effet de bord et
            // leve toute ambiguite sur celui des codes qui vise la voie A :
            // se tromper mettrait le MOSFET passant en permanence.
            p->DBRED = 0U; // aucun temps mort : un seul interrupteur par etage
            p->DBFED = 0U;
            p->DBCTL.bit.IN_MODE = DBA_ALL;
            p->DBCTL.bit.POLSEL = DB_ACTV_LO;
            p->DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // POLSEL sans effet si bypass

            // ---- HRPWM -------------------------------------------------
            //
            // CTLMODE = HR_CMP : la fraction est prise dans CMPAHR (et non
            // dans TBPHSHR, qui sert au controle haute resolution de la
            // PERIODE -- ce n'est pas ce qu'on fait).
            //
            // HRLOAD = HR_CTR_ZERO : la fraction bascule de l'ombre au
            // registre actif au passage a zero, comme CMPA et CMPB
            // (LOADAMODE/LOADBMODE ci-dessus). Les trois doivent basculer au
            // MEME instant, sinon une periode sortirait avec la partie
            // entiere d'une consigne et la fraction d'une autre.
            //
            // AUTOCONV = 1 : le materiel multiplie lui-meme la fraction par
            // HRMSTEP pour obtenir le nombre de pas MEP. Sans ce bit il
            // faudrait faire la multiplication en logiciel, dans l'ISR de
            // regulation, sur un coeur sans unite flottante.
            //
            // HRPE = 0 : pas de controle haute resolution de la periode.
            //
            // EDGMODE : voir calib.h, c'est le point a verifier au banc.
            EALLOW;
            p->HRCNFG.all = 0U;
            if (stage_has_hrpwm(s))
            {
                p->HRCNFG.bit.EDGMODE = PWM_HRPWM_EDGMODE;
                p->HRCNFG.bit.CTLMODE = HR_CMP;
                p->HRCNFG.bit.HRLOAD = HR_CTR_ZERO;
                p->HRCNFG.bit.AUTOCONV = 1;
            }
            p->HRPCTL.bit.HRPE = 0;
            EDIS;

            s_stage[i].duty = 0.0f;
            s_stage[i].enabled = false;

            pwm_set_freq(s, s_stage[i].freq_hz);
            pwm_enable(s, false);
        }
    }

    // Les broches ne sont confiees a l'ePWM qu'ICI, la configuration faite et
    // les sorties deja forcees a l'etat bloque par pwm_enable(s, false).
    //
    // Auparavant le multiplexage etait fait en tete de fonction : avec un
    // driver inverseur, la broche pilotee par un ePWM encore vierge (Action
    // Qualifier a zero, donc sortie basse) aurait rendu le MOSFET PASSANT
    // pendant toute la configuration. Jusqu'au multiplexage la broche reste
    // une entree GPIO avec son pull-up de reset, soit l'etat bloque.
    EALLOW;
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1; // EPWM1A
    GpioCtrlRegs.GPAMUX1.bit.GPIO2 = 1; // EPWM2A

    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;
    EDIS;

    // Premiere calibration MEP. Placee ICI, en toute fin d'initialisation :
    // le module de calibration a besoin des horloges ePWM, donc apres
    // TBCLKSYNC, et les sorties sont deja forcees a l'etat bloque par
    // pwm_enable(s, false) -- la boucle d'attente ne laisse donc rien
    // commuter.
    //
    // Le resultat n'est pas teste : un echec laisse simplement s_hrpwm_ok a
    // false, donc la resolution entiere d'avant. Rien a signaler a
    // l'operateur en urgence, et surtout rien qui doive empecher la carte
    // de demarrer.
    (void)pwm_hrpwm_init();
}

void pwm_set_freq(stage_id_t stage, uint32_t hz)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);

    if (hz == 0UL)
    {
        return;
    }

    s_stage[stage_index(stage)].freq_hz = hz;
    p->TBPRD = (uint16_t)((CLK_SYSCLKOUT_HZ / hz) - 1UL);

    // TBPRD change -> CMPA doit suivre pour conserver le meme duty.
    pwm_apply_duty(stage);
}

void pwm_set_duty(stage_id_t stage, float duty_0_1)
{
    if (duty_0_1 < 0.0f)
    {
        duty_0_1 = 0.0f;
    }
    else if (duty_0_1 > 1.0f)
    {
        duty_0_1 = 1.0f;
    }

    s_stage[stage_index(stage)].duty = duty_0_1;
    pwm_apply_duty(stage);
}

float pwm_get_duty(stage_id_t stage)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);
    uint16_t period = (uint16_t)(p->TBPRD + 1U);

    if (period == 0U)
    {
        return 0.0f;
    }
    return (float)p->CMPA.half.CMPA / (float)period;
}

uint16_t pwm_get_period_counts(stage_id_t stage)
{
    return (uint16_t)(pwm_regs(stage)->TBPRD + 1U);
}

void pwm_set_duty_counts(stage_id_t stage, uint16_t counts)
{
    pwm_set_duty_q8(stage, ((uint32_t)counts) << PWM_DUTY_FRAC_BITS);
}

// Ecriture UNIQUE du rapport cyclique. Tout passe par ici -- consigne
// flottante, consigne entiere, regulation -- pour qu'il n'existe qu'un seul
// endroit ou CMPA et CMPAHR sont decides ensemble.
//
// CMPA et CMPAHR forment un mot de 32 bits, partie entiere en poids fort.
// L'ecriture se fait EN UN SEUL ACCES 32 bits : ecrire les deux moities
// separement laisserait, entre les deux instructions, une fraction
// appartenant a l'ancienne consigne collee a la partie entiere de la
// nouvelle. Le C28x sait faire cet acces, il n'y a donc rien a payer.
//
// La fraction est en Q16 dans CMPAHR alors que la consigne arrive en Q8,
// d'ou le decalage de 8 : c'est le format qu'attend la conversion
// automatique (AUTOCONV), qui la multiplie ensuite par HRMSTEP pour obtenir
// un nombre de pas MEP.
//
// PAS de mise a jour de la consigne flottante ici : cette fonction est
// appelee depuis l'ISR de regulation, et le F28027 n'a pas d'unite
// flottante -- une division y coutait plusieurs centaines de cycles et
// effondrait la cadence de l'ISR. pwm_get_duty() relit CMPA, la telemetrie
// reste donc juste (a la partie fractionnaire pres, qui ne represente
// jamais plus d'un count).
void pwm_set_duty_q8(stage_id_t stage, uint32_t duty_q8)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);
    uint16_t prd = p->TBPRD;
    uint32_t max_q8 = ((uint32_t)(prd + 1U)) << PWM_DUTY_FRAC_BITS;
    uint16_t coarse;
    uint16_t frac_q16;

    if (duty_q8 > max_q8)
    {
        duty_q8 = max_q8;
    }

    coarse = (uint16_t)(duty_q8 >> PWM_DUTY_FRAC_BITS);
    frac_q16 = (uint16_t)((duty_q8 & 0xFFUL) << 8);

#if PWM_HRPWM_EDGE_TEST
    // Essai de front : la regulation est court-circuitee sur l'etage 1, seul
    // le debogueur decide. Voir calib.h.
    if (stage == STAGE_1)
    {
        coarse = g_hr_test_coarse;
        frac_q16 = (uint16_t)(g_hr_test_frac << 8);
    }
#endif

    // Le MEP demande quelques cycles SYSCLK de marge de part et d'autre de
    // l'impulsion. Hors de cette plage on abandonne la partie fractionnaire
    // plutot que de la confier a un materiel qui ne la respectera pas : on
    // retombe alors sur la resolution entiere, sans discontinuite.
    if (!s_hrpwm_ok
        || !stage_has_hrpwm(stage)
        || (coarse < PWM_HRPWM_GUARD_COUNTS)
        || (coarse > (uint16_t)(prd - PWM_HRPWM_GUARD_COUNTS)))
    {
        frac_q16 = 0U;
    }

    p->CMPA.all = ((uint32_t)coarse << 16) | (uint32_t)frac_q16;
    pwm_apply_adc_trigger(p);
}

// ---- Calibration MEP -------------------------------------------------

// Un pas de calibration, et la recopie de son resultat dans le materiel.
//
// DEUX PIEGES, tous deux constates au banc le 23/08/2026, et tous deux
// silencieux -- rien ne signale l'erreur, seul le front ne bouge pas.
//
// 1. EALLOW. HRMSTEP est un registre protege. Sans EALLOW, toute ecriture
//    est rejetee sans le moindre retour. C'est ce qu'explique le "EALLOW;"
//    isole et jamais referme de l'exemple TI Example_2802xHRPWM_Duty_SFO_V6.
//
// 2. LA RECOPIE N'EST PAS FAITE PAR LA BIBLIOTHEQUE. Son en-tete affirme
//    pourtant que SFO() "updates HRMSTEP register with MEP_ScaleFactor
//    value". Avec SFO_TI_Build_V6.lib sur f2802x, c'est faux : mesure au
//    banc, MEP_ScaleFactor = 116 en RAM et HRMSTEP = 0 dans le peripherique,
//    EALLOW ouvert. On fait donc la recopie explicitement.
//
// Pourquoi ca compte : avec AUTOCONV, le materiel obtient le nombre de pas
// MEP en multipliant la fraction de CMPAHR par HRMSTEP. A zero, le produit
// est nul. Tout parait juste -- calibration reussie, HRCNFG correct, CMPAHR
// renseigne -- et le rapport cyclique ne bouge pas d'un millieme.
//
// HRMSTEP n'existe que dans l'espace de l'ePWM1, mais il vaut pour tous les
// canaux : une seule ecriture suffit, quel que soit l'etage concerne.
static int sfo_step(void)
{
    int status;

    EALLOW;
    status = SFO();
    EDIS;

    // EALLOW REARME ICI, ET PAS SEULEMENT AVANT SFO(). La bibliotheque
    // referme la protection avant de rendre la main : un EALLOW pose en
    // amont de l'appel ne couvre plus l'ecriture qui le suit, et celle-ci
    // est alors rejetee en silence. Constate au banc -- HRMSTEP restait
    // obstinement a 0 alors que la meme ecriture faite depuis le debogueur,
    // qui ignore la protection, passait du premier coup.
    EALLOW;
    if ((MEP_ScaleFactor > 0) && (MEP_ScaleFactor <= 255))
    {
        EPwm1Regs.HRMSTEP = (uint16_t)MEP_ScaleFactor;
    }
    EDIS;

    return status;
}

bool pwm_hrpwm_init(void)
{
    // Borne explicite : une calibration qui n'aboutit pas ne doit pas figer
    // la carte au demarrage, sans LED, sans telemetrie et sans explication.
    // On sort au bout d'un nombre fini d'appels et on tourne sans HRPWM.
    uint16_t attempts;
    int status = SFO_INCOMPLETE;

    s_hrpwm_ok = false;

    for (attempts = 0U; attempts < 1000U; attempts++)
    {
        status = sfo_step();
        if (status == SFO_COMPLETE)
        {
            s_hrpwm_ok = true;
            return true;
        }
        if (status == SFO_ERROR)
        {
            return false; // plus de 255 pas MEP par count : conversion
                          // automatique inutilisable
        }
    }
    return false;
}

bool pwm_hrpwm_service(void)
{
    int status = sfo_step();

    if (status == SFO_ERROR)
    {
        // La finesse disparait, la carte continue. Ne JAMAIS couper la
        // puissance pour ca : une calibration ratee n'est pas un defaut de
        // puissance, et la commande entiere reste parfaitement valide.
        s_hrpwm_ok = false;
        return false;
    }

    // ARMEMENT DU DRAPEAU ICI AUSSI, ET PAS SEULEMENT DANS pwm_hrpwm_init().
    //
    // La premiere version ne le levait qu'a l'initialisation. Or celle-ci est
    // bornee a 1000 appels pour ne pas figer la carte au demarrage, et la
    // calibration ne tient pas toujours dans ce budget : le drapeau restait
    // alors faux DEFINITIVEMENT, meme une fois la calibration terminee en
    // tache de fond. Constate au banc le 23/08/2026 -- MEP_ScaleFactor valait
    // 117, donc une calibration parfaitement valide, et CMPAHR restait a zero.
    //
    // Le symptome est trompeur : tout a l'air correct sauf le resultat.
    if (status == SFO_COMPLETE)
    {
        s_hrpwm_ok = true;
    }
    return true;
}

void pwm_enable(stage_id_t stage, bool enabled)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);

    s_stage[stage_index(stage)].enabled = enabled;

    // CSFA : 1 = forcage continu a l'etat bas, 0 = action normale de l'AQ.
    p->AQCSFRC.bit.CSFA = enabled ? 0U : 1U;
}
