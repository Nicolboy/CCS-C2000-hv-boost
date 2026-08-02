#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>

// Ordre des voies = ordre des SOC0..SOC8. Brochage LQFP48 PT, cf. PROMPT §2.
typedef enum
{
    ADC_CH_VIN = 0, // broche 10, ADCINA0 (partagee avec VREFHI)
    ADC_CH_IIN,     // broche  8, ADCINA1
    ADC_CH_I1,      // broche  9, ADCINA2 / COMP1A / AIO2
    ADC_CH_IOUT,    // broche  7, ADCINA3
    ADC_CH_I2,      // broche  5, ADCINA4 / COMP2A / AIO4
    ADC_CH_VOUT,    // broche 14, ADCINB2 / AIO10
    ADC_CH_V1,      // broche 16, ADCINB4 / AIO12
    ADC_CH_T1,      // broche 17, ADCINB6 / AIO14
    ADC_CH_T2,      // broche 18, ADCINB7
    ADC_CH_COUNT
} adc_channel_t;

// Reference interne (ADCREFSEL = 0, pleine echelle 3,3 V) : imposee car
// VREFHI est partagee avec ADCINA0, utilisee pour VIN (PROMPT §2).
// Les SOC sont declenches par ePWM1, jamais en free-run, pour echantillonner
// a un instant stable loin des fronts de commutation (PROMPT §6 etape 4).
//
// A appeler APRES bsp_gpio_analog_init() (bascule des AIO en mode analogique)
// et APRES pwm_init() (c'est ePWM1 qui fournit le declenchement).
void adc_init(void);

// Derniere valeur brute (0..4095) rangee par l'ISR ADCINT1.
uint16_t adc_get_raw(adc_channel_t ch);

// Meme valeur convertie en volts a l'entree de la broche (0..3,3 V).
// Ce n'est PAS la grandeur physique : la conversion vers V/A/°C (ponts
// diviseurs, gain de shunt, NTC) sera le role de measure.c.
float adc_get_volts(adc_channel_t ch);

// true si au moins une voie est sous le seuil donne, exprime en volts.
bool adc_any_below(float volts);

// Compteur de conversions completes, pour verifier au debogueur que les SOC
// tournent bien.
uint32_t adc_get_sequence_count(void);

#endif
