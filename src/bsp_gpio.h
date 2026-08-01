#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

typedef enum
{
    LED_BLUE = 0,
    LED_RED
} led_id_t;

void bsp_gpio_leds_init(void);
void led_set(led_id_t led, bool on);

// Bascule AIO2, AIO4, AIO10, AIO12, AIO14 en mode analogique (shunts,
// COMP1A/COMP2A, VOUT, V1, NTC1). A appeler avant safety_init()/adc_init().
void bsp_gpio_analog_init(void);

#endif
