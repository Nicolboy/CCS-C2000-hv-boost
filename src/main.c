#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "safety.h"

extern uint16_t RamfuncsLoadStart;
extern uint16_t RamfuncsLoadSize;
extern uint16_t RamfuncsRunStart;

interrupt void cpu_timer0_isr(void);

void main(void)
{
#ifdef _FLASH
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
#endif

    bsp_clock_init();
    bsp_gpio_leds_init();

    DINT;
    InitPieCtrl();
    IER = 0x0000;
    IFR = 0x0000;
    InitPieVectTable();

    bsp_gpio_analog_init();
    safety_init();

    EALLOW;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    EDIS;

    InitCpuTimers();
    ConfigCpuTimer(&CpuTimer0, 60, 500000); // 500 ms -> 1 Hz sur la LED

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; // TINT0

    EINT;
    ERTM;

    for (;;)
    {
        // Etape 1-2 : validation clock/LED/securite. Rien d'autre pour
        // l'instant, tout se passe dans les ISR.
    }
}

interrupt void cpu_timer0_isr(void)
{
    static bool blue_on = false;

    CpuTimer0.InterruptCount++;

    blue_on = !blue_on;
    led_set(LED_BLUE, blue_on);

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
