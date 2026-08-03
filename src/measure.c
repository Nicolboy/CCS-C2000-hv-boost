#include <math.h>
#include "measure.h"
#include "calib.h"

// Brut -> tension a la broche. Reference interne, pleine echelle 3,3 V.
static float raw_to_volts(uint16_t raw)
{
    return (float)raw * ADC_VREF_V / 4096.0f;
}

float measure_vin(uint16_t raw)
{
    return raw_to_volts(raw) * MEAS_VIN_GAIN_V_PER_V;
}

float measure_iin(uint16_t raw)
{
    return raw_to_volts(raw) * MEAS_IIN_A_PER_V;
}

float measure_v1(uint16_t raw)
{
    return raw_to_volts(raw) * MEAS_V1_GAIN_V_PER_V;
}

float measure_vout(uint16_t raw)
{
    return raw_to_volts(raw) * MEAS_VOUT_GAIN_V_PER_V;
}

float measure_iout(uint16_t raw)
{
    return raw_to_volts(raw) * MEAS_IOUT_A_PER_V;
}

// Vadc = offset + I * gain  ->  I = (Vadc - offset) / gain.
// Le resultat peut etre legerement negatif a courant nul (bruit autour de
// l'offset) : on ne le sature pas, une valeur negative en telemetrie est un
// indice utile de derive de l'offset apres changement d'ampli.
float measure_i1(uint16_t raw)
{
    return (raw_to_volts(raw) - MEAS_I1_OFFSET_V) / MEAS_I1_GAIN_V_PER_A;
}

float measure_i2(uint16_t raw)
{
    return (raw_to_volts(raw) - MEAS_I2_OFFSET_V) / MEAS_I2_GAIN_V_PER_A;
}

// Pont 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
//   R_ntc = R_fixe * (VREF - Vadc) / Vadc
//   T(K)  = 1 / ( 1/T25 + ln(R_ntc / R25) / beta )
//
// Precision attendue : la tolerance de +/-3 % sur beta donne environ
// +/-2 degC vers 100 degC sans calibration individuelle. Suffisant pour de
// la protection thermique, pas pour de la mesure fine (PROMPT §5).
float measure_temp(uint16_t raw)
{
    float vadc = raw_to_volts(raw);
    float r_ntc;
    float inv_t;

    // Bornes : hors de cette plage le pont est en butee (broche en l'air,
    // NTC en court-circuit ou coupee) et le calcul n'a plus de sens.
    if (vadc <= 0.01f || vadc >= (ADC_VREF_V - 0.01f))
    {
        return 0.0f;
    }

    r_ntc = MEAS_NTC_R_FIXED_OHM * (ADC_VREF_V - vadc) / vadc;

    inv_t = 1.0f / MEAS_NTC_T25_K
            + logf(r_ntc / MEAS_NTC_R25_OHM) / MEAS_NTC_BETA_K;

    return (1.0f / inv_t) - MEAS_KELVIN_OFFSET;
}
