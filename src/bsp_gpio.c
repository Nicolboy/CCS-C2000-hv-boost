#include "DSP28x_Project.h"
#include "bsp_gpio.h"
#include "calib.h"

void bsp_gpio_leds_init(void)
{
    EALLOW;
    GpioCtrlRegs.GPAMUX1.bit.GPIO12 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO12 = 1;
    GpioCtrlRegs.GPBMUX1.bit.GPIO33 = 0;
    GpioCtrlRegs.GPBDIR.bit.GPIO33 = 1;
    EDIS;

    led_set(LED_BLUE, false);
    led_set(LED_RED, false);
}

void bsp_gpio_analog_init(void)
{
    EALLOW;
    GpioCtrlRegs.AIOMUX1.bit.AIO2 = GPIO_ANALOG_MODE;
    GpioCtrlRegs.AIOMUX1.bit.AIO4 = GPIO_ANALOG_MODE;
    GpioCtrlRegs.AIOMUX1.bit.AIO10 = GPIO_ANALOG_MODE;
    GpioCtrlRegs.AIOMUX1.bit.AIO12 = GPIO_ANALOG_MODE;
    GpioCtrlRegs.AIOMUX1.bit.AIO14 = GPIO_ANALOG_MODE;
    EDIS;
}

void bsp_gpio_control_init(void)
{
    EALLOW;
    // Stage1-EN = GPIO16 (broche 27), Stage2-EN = GPIO17 (broche 26).
    // GPIO16/17 ne sont pas des broches PWM : leurs pull-ups sont actives au
    // reset, ce qui presente un 1 sur une entree de chaque porte ET avant que
    // le firmware ne tourne. On les coupe des l'init (PROMPT §6 etape 3).
    GpioCtrlRegs.GPAPUD.bit.GPIO16 = 1;
    GpioCtrlRegs.GPAPUD.bit.GPIO17 = 1;
    GpioCtrlRegs.GPAMUX2.bit.GPIO16 = 0;
    GpioCtrlRegs.GPAMUX2.bit.GPIO17 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO16 = 1;
    GpioCtrlRegs.GPADIR.bit.GPIO17 = 1;

    // HV_EN = GPIO32 (broche 31)
    GpioCtrlRegs.GPBMUX1.bit.GPIO32 = 0;
    GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1;

    // DISCHARGE = GPIO5 (broche 40)
    GpioCtrlRegs.GPAMUX1.bit.GPIO5 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO5 = 1;
    EDIS;

    // Etat sur avant toute autre configuration. La decharge est laissee
    // ACTIVE : au demarrage, le condensateur de sortie peut encore etre
    // charge par une session precedente, rien ne justifie de l'inhiber
    // avant que la conversion ne soit reellement demandee.
    stage_enable_set(STAGE_1, false);
    stage_enable_set(STAGE_2, false);
    hv_enable_set(false);
    hv_discharge_set(true);
}

void stage_enable_set(stage_id_t stage, bool enabled)
{
    if (stage == STAGE_1)
    {
        if (enabled)
        {
            GpioDataRegs.GPASET.bit.GPIO16 = 1;
        }
        else
        {
            GpioDataRegs.GPACLEAR.bit.GPIO16 = 1;
        }
    }
    else
    {
        if (enabled)
        {
            GpioDataRegs.GPASET.bit.GPIO17 = 1;
        }
        else
        {
            GpioDataRegs.GPACLEAR.bit.GPIO17 = 1;
        }
    }
}

void hv_enable_set(bool enabled)
{
#if ADC_TIMING_PROBE
    // GPIO32 appartient a l'instrumentation de timing ADC (adc.c). La boucle
    // principale repasse par enter_safe_state() a chaque tour tant que la
    // marche n'est pas demandee, soit quelques dizaines de milliers de fois
    // par seconde : ses ecritures tronqueraient l'impulsion en plein milieu
    // de la conversion. On les ignore.
    //
    // SANS DANGER uniquement parce que l'etage 2 et la sortie haute tension
    // ne sont pas peuples. Des qu'ils le seront, ADC_TIMING_PROBE doit
    // repasser a 0 -- sinon HV_EN n'obeit plus a l'etat sur.
    (void)enabled;
#else
    if (enabled)
    {
        GpioDataRegs.GPBSET.bit.GPIO32 = 1;
    }
    else
    {
        GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;
    }
#endif
}

void hv_discharge_set(bool active)
{
    // Niveau BAS = decharge active (NPN bloque, grille du MOSFET libre).
    // Voir bsp_gpio.h pour le detail de la polarite inversee.
    if (active)
    {
        GpioDataRegs.GPACLEAR.bit.GPIO5 = 1;
    }
    else
    {
        GpioDataRegs.GPASET.bit.GPIO5 = 1;
    }
}

void bsp_gpio_stage_default_override_init(void)
{
    EALLOW;
    // GPIO1 = COMP1OUT (broche 28), GPIO3 = COMP2OUT (broche 38).
    // Mux 0 = GPIO : on reprend la main sur des broches normalement pilotees
    // par les comparateurs. Bring-up uniquement, voir bsp_gpio.h.
    GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 0;
    GpioCtrlRegs.GPAMUX1.bit.GPIO3 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO1 = 1;
    GpioCtrlRegs.GPADIR.bit.GPIO3 = 1;
    EDIS;

    stage_default_set(STAGE_1, false);
    stage_default_set(STAGE_2, false);
}

void stage_default_set(stage_id_t stage, bool level)
{
    if (stage == STAGE_1)
    {
        if (level)
        {
            GpioDataRegs.GPASET.bit.GPIO1 = 1;
        }
        else
        {
            GpioDataRegs.GPACLEAR.bit.GPIO1 = 1;
        }
    }
    else
    {
        if (level)
        {
            GpioDataRegs.GPASET.bit.GPIO3 = 1;
        }
        else
        {
            GpioDataRegs.GPACLEAR.bit.GPIO3 = 1;
        }
    }
}

void led_set(led_id_t led, bool on)
{
#if LED_ACTIVE_LOW
    bool pin_high = !on;
#else
    bool pin_high = on;
#endif

    if (led == LED_BLUE)
    {
        if (pin_high)
        {
            GpioDataRegs.GPASET.bit.GPIO12 = 1;
        }
        else
        {
            GpioDataRegs.GPACLEAR.bit.GPIO12 = 1;
        }
    }
    else
    {
        if (pin_high)
        {
            GpioDataRegs.GPBSET.bit.GPIO33 = 1;
        }
        else
        {
            GpioDataRegs.GPBCLEAR.bit.GPIO33 = 1;
        }
    }
}
