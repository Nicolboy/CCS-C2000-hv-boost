#include "DSP28x_Project.h"
#include "safety.h"
#include "bsp_gpio.h"
#include "calib.h"

static volatile bool s_stage1_fault = false;
static volatile bool s_stage2_fault = false;
static volatile bool s_overcurrent = false;
static volatile bool s_emustop = false;

interrupt void epwm1_tzint_isr(void);
interrupt void epwm2_tzint_isr(void);

void safety_init(void)
{
    EALLOW;

    // Le comparateur partage la reference bandgap de l'ADC : elle doit etre
    // alimentee meme si l'ADC lui-meme n'est pas encore utilise (etape 4).
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1;
    SysCtrlRegs.PCLKCR3.bit.COMP1ENCLK = 1;
    SysCtrlRegs.PCLKCR3.bit.COMP2ENCLK = 1;
    AdcRegs.ADCCTL1.bit.ADCBGPWD = 1;
    EDIS;
    DELAY_US(1000L);

    EALLOW;

    // Comparateur 1 : shunt etage 1 sur COMP1A (entree non-inverseuse fixe),
    // seuil DAC sur l'entree inverseuse (COMPSOURCE=0).
    Comp1Regs.COMPCTL.bit.COMPDACEN = 1;
    Comp1Regs.COMPCTL.bit.COMPSOURCE = 0;
    Comp1Regs.COMPCTL.bit.SYNCSEL = 0;  // asynchrone -> ~30 ns vers le Trip Zone
    Comp1Regs.COMPCTL.bit.CMPINV = 1;   // driver actif haut : 1=OK, 0=defaut
    Comp1Regs.COMPCTL.bit.QUALSEL = 0;  // pas de qualification pour l'instant
    // Seuil propre a l'etage 1 : les deux chaines de mesure different d'environ
    // 1 % en gain et n'ont pas le meme offset (docs/mesure-cartepuissance.md).
    Comp1Regs.DACVAL.bit.DACVAL = SAFETY_DAC_CODE_STAGE1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 3; // COMP1OUT

    // Comparateur 2 : idem, shunt etage 2 sur COMP2A.
    Comp2Regs.COMPCTL.bit.COMPDACEN = 1;
    Comp2Regs.COMPCTL.bit.COMPSOURCE = 0;
    Comp2Regs.COMPCTL.bit.SYNCSEL = 0;
    Comp2Regs.COMPCTL.bit.CMPINV = 1;
    Comp2Regs.COMPCTL.bit.QUALSEL = 0;
    Comp2Regs.DACVAL.bit.DACVAL = SAFETY_DAC_CODE_STAGE2;
    GpioCtrlRegs.GPAMUX1.bit.GPIO3 = 3; // COMP2OUT

    // TZ_FORCE_HI et non LO : le driver de grille est INVERSEUR (voir la
    // configuration DBCTL.POLSEL dans pwm.c). La Trip Zone agit en aval de
    // cette inversion, directement sur la broche -- forcer l'etat bas y
    // rendrait le MOSFET PASSANT sur defaut, transformant la protection en
    // court-circuit franc. Les deux reglages sont indissociables : changer
    // la polarite du driver impose de revoir cette ligne.
    //
    // Digital Compare : COMPxOUT -> DCAEVT1 -> Trip Zone one-shot (latche)
    EPwm1Regs.DCTRIPSEL.bit.DCAHCOMPSEL = DC_COMP1OUT;
    EPwm1Regs.TZDCSEL.bit.DCAEVT1 = TZ_DCAH_LOW;
    EPwm1Regs.TZSEL.bit.DCAEVT1 = 1;
    EPwm1Regs.TZCTL.bit.TZA = TZ_FORCE_HI;
    EPwm1Regs.TZEINT.bit.OST = 1;
    // TZ6 = EMUSTOP (signal cable en dur depuis le CPU, TRM SPRUI09A section
    // 3.2.7) : coupe le PWM des que le debugger arrete le coeur. FREE_SOFT
    // seul ne suffit pas, il ne fait que geler le compteur de base de temps,
    // la broche reste figee dans son dernier etat.
    EPwm1Regs.TZSEL.bit.OSHT6 = 1;
    EPwm1Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm2Regs.DCTRIPSEL.bit.DCAHCOMPSEL = DC_COMP2OUT;
    EPwm2Regs.TZDCSEL.bit.DCAEVT1 = TZ_DCAH_LOW;
    EPwm2Regs.TZSEL.bit.DCAEVT1 = 1;
    EPwm2Regs.TZCTL.bit.TZA = TZ_FORCE_HI;
    EPwm2Regs.TZEINT.bit.OST = 1;
    EPwm2Regs.TZSEL.bit.OSHT6 = 1;
    EPwm2Regs.TBCTL.bit.FREE_SOFT = 0;

    // Efface un trip herite d'AVANT ce demarrage. TZFLG.OST est latche dans
    // le materiel : il survit a un reset logiciel comme a un simple
    // rechargement de programme, et sans ce clear le PWM resterait coupe
    // sans raison visible.
    //
    // Sans danger : a ce stade pwm_init() a laisse les deux sorties forcees
    // a l'etat bas (AQCSFRC), donc rien ne demarre. Et si la condition de
    // defaut est toujours presente, le trip se re-verrouille immediatement
    // -- on n'efface qu'un etat perime, jamais un defaut reel.
    EPwm1Regs.TZCLR.bit.DCAEVT1 = 1;
    EPwm1Regs.TZCLR.bit.OST = 1;
    EPwm1Regs.TZCLR.bit.INT = 1;
    EPwm2Regs.TZCLR.bit.DCAEVT1 = 1;
    EPwm2Regs.TZCLR.bit.OST = 1;
    EPwm2Regs.TZCLR.bit.INT = 1;

    PieVectTable.EPWM1_TZINT = &epwm1_tzint_isr;
    PieVectTable.EPWM2_TZINT = &epwm2_tzint_isr;
    EDIS;

    IER |= M_INT2;
    PieCtrlRegs.PIEIER2.bit.INTx1 = 1; // EPWM1_TZINT
    PieCtrlRegs.PIEIER2.bit.INTx2 = 1; // EPWM2_TZINT
}

void safety_arm_emustop_only(void)
{
    EALLOW;
    // TZ6 = EMUSTOP, cable en dur depuis le CPU (TRM SPRUI09A section 3.2.7).
    // One-shot : le flag latche, il ne se rearme pas seul.
    EPwm1Regs.TZSEL.bit.OSHT6 = 1;
    EPwm1Regs.TZCTL.bit.TZA = TZ_FORCE_HI;
    EPwm1Regs.TZEINT.bit.OST = 1;
    EPwm1Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm2Regs.TZSEL.bit.OSHT6 = 1;
    EPwm2Regs.TZCTL.bit.TZA = TZ_FORCE_HI;
    EPwm2Regs.TZEINT.bit.OST = 1;
    EPwm2Regs.TBCTL.bit.FREE_SOFT = 0;

    PieVectTable.EPWM1_TZINT = &epwm1_tzint_isr;
    PieVectTable.EPWM2_TZINT = &epwm2_tzint_isr;
    EDIS;

    IER |= M_INT2;
    PieCtrlRegs.PIEIER2.bit.INTx1 = 1; // EPWM1_TZINT
    PieCtrlRegs.PIEIER2.bit.INTx2 = 1; // EPWM2_TZINT
}

safety_faults_t safety_get_fault_flags(void)
{
    safety_faults_t f;
    f.stage1_fault = s_stage1_fault;
    f.stage2_fault = s_stage2_fault;
    f.overcurrent = s_overcurrent;
    f.emustop = s_emustop;
    return f;
}

void safety_clear_faults(void)
{
    EALLOW;
    // OST libere la sortie PWM, DCAEVT1 remet a zero la cause (sinon le
    // prochain trip serait attribue a une surintensite meme s'il vient
    // d'EMUSTOP), INT autorise a nouveau la generation d'interruptions, et
    // TZEINT.OST rearme la notification desarmee par l'ISR.
    EPwm1Regs.TZCLR.bit.DCAEVT1 = 1;
    EPwm1Regs.TZCLR.bit.OST = 1;
    EPwm1Regs.TZCLR.bit.INT = 1;
    EPwm1Regs.TZEINT.bit.OST = 1;

    EPwm2Regs.TZCLR.bit.DCAEVT1 = 1;
    EPwm2Regs.TZCLR.bit.OST = 1;
    EPwm2Regs.TZCLR.bit.INT = 1;
    EPwm2Regs.TZEINT.bit.OST = 1;
    EDIS;

    s_stage1_fault = false;
    s_stage2_fault = false;
    s_overcurrent = false;
    s_emustop = false;
    // Les LED ne sont plus pilotees ici : c'est status_led.c qui detient
    // l'affichage, a partir de ces drapeaux.
}

void safety_force_trip_test(void)
{
    EALLOW;
    EPwm1Regs.TZFRC.bit.OST = 1;
    EPwm2Regs.TZFRC.bit.OST = 1;
    EDIS;
}

// Diagnostic uniquement : la coupure est deja faite en materiel (PROMPT §6
// etape 2 point 7). On se contente de relever l'origine du trip.
//
// Deux subtilites, decouvertes au banc :
//
//  1. TZFLG.INT est le drapeau GLOBAL d'interruption Trip Zone. Tant qu'il
//     n'est pas efface via TZCLR.INT, plus AUCUNE interruption TZ n'est
//     generee. Sans ce clear, seul le tout premier trip de la session etait
//     signale au logiciel : les suivants coupaient bien le PWM en materiel,
//     mais en silence, sans lever de drapeau.
//
//  2. Effacer INT alors que la condition de trip persiste (surintensite
//     toujours presente) regenere l'evenement immediatement -> tempete
//     d'interruptions qui affamerait la boucle principale. On desarme donc
//     TZEINT.OST ici : une seule notification par episode de defaut. Le
//     rearmement est fait par safety_clear_faults(), c'est-a-dire jamais
//     automatiquement (PROMPT §8).
//
// Le drapeau DCAEVT1 n'est volontairement PAS efface ici : il porte la
// cause du defaut et doit rester lisible jusqu'a l'effacement explicite.
interrupt void epwm1_tzint_isr(void)
{
    s_stage1_fault = true;

    if (EPwm1Regs.TZFLG.bit.DCAEVT1)
    {
        s_overcurrent = true;
    }
    else
    {
        s_emustop = true;
    }

    EALLOW;
    EPwm1Regs.TZEINT.bit.OST = 0;
    EPwm1Regs.TZCLR.bit.INT = 1;
    EDIS;

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP2;
}

interrupt void epwm2_tzint_isr(void)
{
    s_stage2_fault = true;

    if (EPwm2Regs.TZFLG.bit.DCAEVT1)
    {
        s_overcurrent = true;
    }
    else
    {
        s_emustop = true;
    }

    EALLOW;
    EPwm2Regs.TZEINT.bit.OST = 0;
    EPwm2Regs.TZCLR.bit.INT = 1;
    EDIS;

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP2;
}
