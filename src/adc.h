#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>

// Ordre des voies = ordre des SOC0..SOC8. Brochage LQFP48 PT, cf. PROMPT §2.
//
// L'ORDRE EST FONCTIONNEL, pas arbitraire.
//
// La sequence est declenchee de sorte que les QUATRE PREMIERES voies tombent
// au milieu de la conduction du MOSFET (ePWM1 CTRU=CMPB, voir pwm.c). C'est
// l'instant le plus eloigne des deux fronts de commutation, donc le plus
// calme du cycle, et c'est aussi celui ou le shunt -- place dans la source du
// MOSFET -- donne la moyenne du courant sur la phase de conduction.
//
// Ces quatre voies sont celles dont une mesure fausse a des consequences :
// V1 est la grandeur regulee, VIN et IIN portent le calcul de rendement, I1
// surveille le courant. Elles occupent 4 creneaux de 550 ns, soit 2,2 us.
//
// Les cinq suivantes defilent ensuite, sans instant privilegie. Un echantillon
// bruite n'y fait qu'un point de telemetrie aberrant. Les thermistances sont
// en DERNIER : ce sont les seules voies non tamponnees, elles exigent une
// fenetre d'acquisition longue, et les mettre en fin de sequence evite de
// decaler tout ce qui les suit.
//
// LIMITE CONNUE : les 2,2 us du groupe critique ne tiennent dans la phase de
// conduction que si celle-ci depasse ~2,5 us, soit un rapport cyclique
// superieur a 0,5 a 200 kHz. En dessous, les dernieres voies du groupe
// debordent sur le blocage. C'est une limite de l'ADC, pas du reglage : neuf
// conversions sequentielles ne rentrent pas dans une fenetre plus courte.
typedef enum
{
    ADC_CH_I1 = 0,  // broche  9, ADCINA2 / COMP1A / AIO2   -- groupe critique
    ADC_CH_V1,      // broche 16, ADCINB4 / AIO12           -- groupe critique
    ADC_CH_VIN,     // broche 10, ADCINA0 (partagee VREFHI) -- groupe critique
    ADC_CH_IIN,     // broche  8, ADCINA1                   -- groupe critique
    ADC_CH_IOUT,    // broche  7, ADCINA3
    ADC_CH_I2,      // broche  5, ADCINA4 / COMP2A / AIO4
    ADC_CH_VOUT,    // broche 14, ADCINB2 / AIO10
    ADC_CH_T1,      // broche 17, ADCINB6 / AIO14  -- non tamponnee, lente
    ADC_CH_T2,      // broche 18, ADCINB7          -- non tamponnee, lente
    ADC_CH_COUNT
} adc_channel_t;

// Nombre de voies du groupe critique, converties en tete de sequence avec la
// fenetre courte. Les suivantes sont les voies lentes.
#define ADC_CRITICAL_COUNT  4U

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
