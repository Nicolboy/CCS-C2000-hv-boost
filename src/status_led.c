#include <stdbool.h>
#include <stdint.h>
#include "status_led.h"
#include "bsp_gpio.h"

// Durees exprimees en ticks de 10 ms. Ce sont des constantes de
// presentation, pas de calibration materielle : elles restent ici plutot
// que dans calib.h, qui decrit la carte.
#define TICKS_PER_100MS      10U

#define STARTUP_MIN_TICKS    100U // 1 s : rend l'etat de demarrage visible

#define NOMINAL_PERIOD       200U // 2,0 s
#define NOMINAL_ON            20U // 0,2 s

#define LINK_LOST_PERIOD      40U // 0,4 s
#define LINK_LOST_ON          20U // 0,2 s

#define EMUSTOP_PERIOD       100U // 1,0 s
#define EMUSTOP_HALF          50U // 0,5 s bleu puis 0,5 s rouge

#define OVERTEMP_PERIOD      100U // 1,0 s
#define OVERTEMP_ON           50U // 0,5 s

static led_state_t s_state = LED_STATE_STARTUP;
static uint16_t s_phase = 0;         // position dans le motif courant
static uint16_t s_startup_hold = STARTUP_MIN_TICKS;

void status_led_init(void)
{
    s_state = LED_STATE_STARTUP;
    s_phase = 0;
    s_startup_hold = STARTUP_MIN_TICKS;

    led_set(LED_BLUE, true);
    led_set(LED_RED, false);
}

void status_led_set_state(led_state_t state)
{
    if (s_startup_hold != 0U)
    {
        return; // duree minimale de l'etat de demarrage non ecoulee
    }

    if (state != s_state)
    {
        s_state = state;
        s_phase = 0; // le motif repart proprement a son debut
    }
}

void status_led_tick(void)
{
    bool blue = false;
    bool red = false;

    if (s_startup_hold != 0U)
    {
        s_startup_hold--;
    }

    s_phase++;

    switch (s_state)
    {
    case LED_STATE_NOMINAL:
        blue = ((s_phase % NOMINAL_PERIOD) < NOMINAL_ON);
        break;

    case LED_STATE_LINK_LOST:
        blue = ((s_phase % LINK_LOST_PERIOD) < LINK_LOST_ON);
        break;

    case LED_STATE_EMUSTOP:
        blue = ((s_phase % EMUSTOP_PERIOD) < EMUSTOP_HALF);
        red = !blue;
        break;

    case LED_STATE_OVERTEMP:
        red = ((s_phase % OVERTEMP_PERIOD) < OVERTEMP_ON);
        break;

    case LED_STATE_OVERCURRENT:
        red = true;
        break;

    case LED_STATE_STARTUP:
    default:
        blue = true;
        break;
    }

    led_set(LED_BLUE, blue);
    led_set(LED_RED, red);
}
