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
