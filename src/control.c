#include "control.h"
#include "pwm.h"
#include "bsp_gpio.h"
#include "calib.h"

// Bornes du balayage, en counts, calculees une fois a l'init depuis la
// periode reelle de chaque etage. Elles different d'un etage a l'autre :
// 300 counts a 200 kHz contre 600 a 100 kHz.
static uint16_t s_min_counts[2];
static uint16_t s_max_counts[2];
static uint16_t s_counts[2];
static bool s_rising[2];
static volatile bool s_armed = false;

// Compteur d'allers-retours complets, lisible au debogueur : permet de
// verifier que le balayage tourne sans avoir a sonder les broches.
static volatile uint32_t s_sweep_cycles = 0;

static uint16_t pct_to_counts(uint16_t period, float pct)
{
    uint16_t c = (uint16_t)((pct / 100.0f) * (float)period + 0.5f);

    // Au moins 1 LSB : un duty de 0 ne serait pas un balayage mais un arret.
    if (c == 0U)
    {
        c = 1U;
    }
    if (c > period)
    {
        c = period;
    }
    return c;
}

void control_init(void)
{
    uint16_t i;
    stage_id_t stages[2] = {STAGE_1, STAGE_2};

    for (i = 0U; i < 2U; i++)
    {
        uint16_t period = pwm_get_period_counts(stages[i]);

        s_min_counts[i] = pct_to_counts(period, CONTROL_SWEEP_MIN_PCT);
        s_max_counts[i] = pct_to_counts(period, CONTROL_SWEEP_MAX_PCT);
        s_counts[i] = s_min_counts[i];
        s_rising[i] = true;
    }

    s_armed = false;
    s_sweep_cycles = 0;
}

void control_start(void)
{
    uint16_t i;
    stage_id_t stages[2] = {STAGE_1, STAGE_2};

    // Les deux etages repartent ensemble du minimum.
    for (i = 0U; i < 2U; i++)
    {
        s_counts[i] = s_min_counts[i];
        s_rising[i] = true;
        pwm_set_duty_counts(stages[i], s_counts[i]);
    }

    s_armed = true;
}

void control_stop(void)
{
    s_armed = false;
}

// Appelee en ISR : que des comparaisons, incrementations et decrementations.
// Le tableau des identifiants est en static const, pas en local : une
// initialisation locale serait recopiee sur la pile a chaque appel, soit
// plusieurs milliers de fois par seconde pour rien.
static const stage_id_t k_stages[2] = {STAGE_1, STAGE_2};

void control_tick(void)
{
    uint16_t i;

    if (!s_armed)
    {
        return;
    }

    for (i = 0U; i < 2U; i++)
    {
        if (s_rising[i])
        {
            if (s_counts[i] < s_max_counts[i])
            {
                s_counts[i]++;
            }
            else
            {
                s_rising[i] = false;
            }
        }
        else
        {
            if (s_counts[i] > s_min_counts[i])
            {
                s_counts[i]--;
            }
            else
            {
                s_rising[i] = true;
                if (i == 0U)
                {
                    s_sweep_cycles++; // compte les allers-retours de l'etage 1
                }
            }
        }

        pwm_set_duty_counts(k_stages[i], s_counts[i]);
    }
}
