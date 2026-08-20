#include "DSP28x_Project.h"
#include "pwm.h"
#include "calib.h"

// Mode up-count, TBCLK = SYSCLKOUT = 60 MHz (HSPCLKDIV = CLKDIV = /1).
// TBPRD = SYSCLKOUT / Fpwm - 1  ->  299 a 200 kHz, 599 a 100 kHz.
//
// HRPWM : volontairement NON implemente a ce stade (PROMPT §6 etape 6), mais
// le routage et la structure s'y pretent. Notes pour l'ajout ulterieur :
//  - HRPWM n'existe que sur les sorties EPWMxA : les deux etages sont donc
//    compatibles tels que routes (GPIO0 et GPIO2).
//  - SYSCLKOUT >= 50 MHz requis (60 MHz ici), MEP 150-310 ps, SFO obligatoire.
//  - Limitation de 3 cycles SYSCLK sur le rapport cyclique : a 200 kHz cela
//    donne un duty min ~0,67 % et max ~99 %, a confronter aux points de
//    fonctionnement de l'etage 1 (D ~ 0,8).
//  - A 100 kHz sans HRPWM : 600 pas ~ 9,2 bits, risque de limit cycling en
//    boucle fermee. C'est l'etage 2 (D ~ 0,875) qui est le plus concerne.

typedef struct
{
    uint32_t freq_hz;
    float duty;
    bool enabled;
} pwm_stage_t;

static pwm_stage_t s_stage[2];

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

// Recalcule CMPA a partir du duty et du TBPRD courants.
static void pwm_apply_duty(stage_id_t stage)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);
    float duty = s_stage[stage_index(stage)].duty;
    uint16_t prd = p->TBPRD;

    // En up-count avec AQ_SET a zero et AQ_CLEAR sur CMPA, la sortie est
    // haute pendant CMPA cycles : duty = CMPA / (TBPRD + 1).
    p->CMPA.half.CMPA = (uint16_t)(duty * (float)(prd + 1U) + 0.5f);
    pwm_apply_adc_trigger(p);
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
    volatile struct EPWM_REGS *p = pwm_regs(stage);
    uint16_t period = (uint16_t)(p->TBPRD + 1U);

    if (counts > period)
    {
        counts = period;
    }

    p->CMPA.half.CMPA = counts;
    pwm_apply_adc_trigger(p);

    // PAS de mise a jour de la consigne flottante ici : cette fonction est
    // appelee depuis l'ISR ADC a plusieurs dizaines de kHz, et le F28027
    // n'a pas d'unite flottante -- une division y coutait plusieurs
    // centaines de cycles et effondrait la cadence de l'ISR.
    // pwm_get_duty() relit CMPA, la telemetrie reste donc juste.
}

void pwm_enable(stage_id_t stage, bool enabled)
{
    volatile struct EPWM_REGS *p = pwm_regs(stage);

    s_stage[stage_index(stage)].enabled = enabled;

    // CSFA : 1 = forcage continu a l'etat bas, 0 = action normale de l'AQ.
    p->AQCSFRC.bit.CSFA = enabled ? 0U : 1U;
}
