#include "DSP28x_Project.h"
#include "safety.h"
#include "bsp_gpio.h"
#include "calib.h"

static volatile bool s_stage1_fault = false;
static volatile bool s_stage2_fault = false;

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

    // Digital Compare : COMPxOUT -> DCAEVT1 -> Trip Zone one-shot (latche)
    EPwm1Regs.DCTRIPSEL.bit.DCAHCOMPSEL = DC_COMP1OUT;
    EPwm1Regs.TZDCSEL.bit.DCAEVT1 = TZ_DCAH_LOW;
    EPwm1Regs.TZSEL.bit.DCAEVT1 = 1;
    EPwm1Regs.TZCTL.bit.TZA = TZ_FORCE_LO;
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
    EPwm2Regs.TZCTL.bit.TZA = TZ_FORCE_LO;
    EPwm2Regs.TZEINT.bit.OST = 1;
    EPwm2Regs.TZSEL.bit.OSHT6 = 1;
    EPwm2Regs.TBCTL.bit.FREE_SOFT = 0;

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
    EPwm1Regs.TZCTL.bit.TZA = TZ_FORCE_LO;
    EPwm1Regs.TZEINT.bit.OST = 1;
    EPwm1Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm2Regs.TZSEL.bit.OSHT6 = 1;
    EPwm2Regs.TZCTL.bit.TZA = TZ_FORCE_LO;
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
    return f;
}

void safety_clear_faults(void)
{
    EALLOW;
    EPwm1Regs.TZCLR.bit.OST = 1;
    EPwm2Regs.TZCLR.bit.OST = 1;
    EDIS;
    s_stage1_fault = false;
    s_stage2_fault = false;
    led_set(LED_RED, false);
}

void safety_force_trip_test(void)
{
    EALLOW;
    EPwm1Regs.TZFRC.bit.OST = 1;
    EPwm2Regs.TZFRC.bit.OST = 1;
    EDIS;
}

interrupt void epwm1_tzint_isr(void)
{
    s_stage1_fault = true;
    led_set(LED_RED, true);
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP2;
}

interrupt void epwm2_tzint_isr(void)
{
    s_stage2_fault = true;
    led_set(LED_RED, true);
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP2;
}
