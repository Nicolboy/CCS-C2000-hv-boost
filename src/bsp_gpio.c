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

    // Broches libres 39 et 41 = GPIO4 et GPIO6, mises en ENTREE avec le
    // PULL-UP INTERNE ACTIF (GPAPUD = 0).
    //
    // ATTENTION -- LE F2802x N'A PAS DE PULL-DOWN INTERNE. Le silicium ne
    // propose que des pull-UP. Un niveau bas au repos ne peut donc venir que
    // d'une RESISTANCE EXTERNE, et il n'existe aucun moyen logiciel de
    // l'obtenir.
    //
    // L'ecriture est INDISPENSABLE malgre l'apparence : GPIO4 et GPIO6 sont
    // des broches a fonction PWM (EPWM3A, EPWM4A), dont le pull-up est
    // DESACTIVE au reset -- contrairement aux GPIO ordinaires. Sans cette
    // ligne elles resteraient donc en l'air.
    //
    // POURQUOI LE HAUT PLUTOT QUE LE BAS : ces deux broches ne pilotent rien
    // et ne sont raccordees a rien. Le seul risque reel est qu'elles
    // FLOTTENT -- niveau lu aleatoire, et courant de traversee permanent dans
    // l'etage d'entree CMOS tant que le potentiel stationne a mi-tension. Un
    // niveau haut defini vaut mieux qu'un niveau bas espere d'une resistance
    // dont rien ne garantit qu'elle est montee.
    //
    // Si le pull-down externe 10 kOhm est effectivement pose, les deux se
    // combattent : ~100 kOhm contre 10 kOhm donne environ 0,3 V, soit un
    // niveau bas toujours valide, au prix de ~33 uA permanents par broche.
    // Sans danger, mais c'est alors GPAPUD = 1 qu'il faut remettre ici.
    GpioCtrlRegs.GPAPUD.bit.GPIO4 = 0;
    GpioCtrlRegs.GPAPUD.bit.GPIO6 = 0;
    GpioCtrlRegs.GPAMUX1.bit.GPIO4 = 0;
    GpioCtrlRegs.GPAMUX1.bit.GPIO6 = 0;
    GpioCtrlRegs.GPADIR.bit.GPIO4 = 0; // entree
    GpioCtrlRegs.GPADIR.bit.GPIO6 = 0; // entree
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

#if CTRL_TIMING_PROBE
    // SONDE DE TIMING ACTIVE : GPIO33 sert de marqueur pour l'ISR du CPU
    // Timer 1, la LED rouge est donc CONFISQUEE. Le garde-fou est place ici,
    // au point d'ecriture UNIQUE de la broche, et non chez les appelants :
    // trois endroits ecrivent LED_RED (bsp_gpio_leds_init, status_led_init,
    // status_led_tick a 100 Hz), et il suffirait d'en oublier un pour que
    // les deux se battent -- l'impulsion mesuree serait alors tronquee au
    // hasard, sans que rien ne le signale.
    //
    // CONSEQUENCE A ACCEPTER : plus aucun motif de defaut n'est visible sur
    // la LED rouge pendant la mesure. La telemetrie reste le seul indicateur
    // d'etat. C'est acceptable pour un essai de quelques minutes, pas au-dela.
    if (led == LED_RED)
    {
        return;
    }
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
