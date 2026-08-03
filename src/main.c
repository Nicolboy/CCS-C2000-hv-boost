#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "safety.h"
#include "uart_link.h"
#include "protocol.h"
#include "pwm.h"
#include "adc.h"
#include "measure.h"
#include "calib.h"

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

    // safety_init() complet volontairement NON appele : le comparateur a
    // deja ete valide (trip observe en passant une entree de 0 a 3 V), et
    // avec les entrees shunt flottantes il declencherait aussitot, ce qui
    // masquerait le test d'EMUSTOP. On arme donc uniquement TZ6.
    // A REMPLACER par safety_init() avant toute mise sous tension de la
    // puissance (PROMPT §8).
    safety_arm_emustop_only();

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
        }

        // Test EMUSTOP : LED rouge = defaut latche, bleue = nominal.
        // Le flag ne se rearme jamais seul (PROMPT §8).
        faults = safety_get_fault_flags();
        {
            bool tripped = faults.stage1_fault || faults.stage2_fault;
            led_set(LED_RED, tripped);
            led_set(LED_BLUE, !tripped);
        }

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

    if ((tick % 30U) == 0U)
    {
        s_send_telemetry = true; // telemetrie toutes les 300 ms
    }

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
