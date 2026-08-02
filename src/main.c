#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "safety.h"
#include "uart_link.h"
#include "protocol.h"

extern uint16_t RamfuncsLoadStart;
extern uint16_t RamfuncsLoadSize;
extern uint16_t RamfuncsRunStart;

interrupt void cpu_timer0_isr(void);

// Bring-up etape 7 (UART) : dernier CommandState recu, pour inspection au
// debogueur. Valeurs de telemetrie figees tant que measure.c n'existe pas.
static volatile command_state_t g_last_cmd = {false, false, false};
static volatile bool s_send_telemetry = false;

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
    // safety_init() desactive temporairement : bring-up isole sur LED+UART
    // uniquement (voir §8 du prompt -- sans risque ici, pwm.c n'existe pas
    // encore, donc aucun PWM ne peut sortir avec ou sans TZ arme).
    uart_link_init();

    EALLOW;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    EDIS;

    InitCpuTimers();
    ConfigCpuTimer(&CpuTimer0, 60, 300000); // 300 ms -> rythme d'envoi bring-up UART
    StartCpuTimer0(); // ConfigCpuTimer laisse TSS=1 (timer a l'arret) par conception

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; // TINT0

    EINT;
    ERTM;

    for (;;)
    {
        command_state_t cmd;

        if (uart_link_poll(&cmd))
        {
            g_last_cmd = cmd;
        }

        if (s_send_telemetry)
        {
            telemetry_t t;

            // Bring-up : valeurs figees, seul le lien TX/RX est valide ici.
            t.freq1_hz = 100000.0f;
            t.freq2_hz = 100000.0f;
            t.duty1_pct = 0.0f;
            t.duty2_pct = 0.0f;
            t.vin_v = 12.3f;
            t.iin_a = 0.5f;
            t.v1_v = 20.0f;
            t.i1_a = 0.1f;
            t.t1_c = 25.0f;
            t.vout_v = 0.0f;
            t.i2_a = 0.0f;
            t.t2_c = 25.0f;
            t.iout_a = 0.0f;

            uart_link_send_telemetry(&t);
            s_send_telemetry = false;
        }

        // Emission en tache de fond : pousse au plus 4 octets (profondeur de
        // la FIFO) puis rend la main. Ne doit jamais retarder la regulation.
        uart_link_service_tx();
    }
}

interrupt void cpu_timer0_isr(void)
{
    static bool blue_on = false;

    CpuTimer0.InterruptCount++;
    s_send_telemetry = true;

    blue_on = !blue_on;
    led_set(LED_BLUE, blue_on);

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
