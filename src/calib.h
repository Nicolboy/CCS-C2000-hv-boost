#ifndef CALIB_H
#define CALIB_H

// Horloge : INTOSC1 (10 MHz nominal) + PLL (DSP28_PLLCR=12, DSP28_DIVSEL=2,
// deja les valeurs par defaut de f2802x_examples.h pour ce device) -> 60 MHz.
// PLLLOCKPRD doit etre ecrit AVANT InitSysCtrl() (qui attend le verrouillage
// PLL en interne) car l'oscillateur interne impose un minimum de 10000.
#define CLK_PLLLOCKPRD      10000
#define CLK_SYSCLKOUT_HZ    60000000UL

// LEDs (LQFP48 PT) : bleue = GPIO12 (broche 47), rouge = GPIO33 (broche 36)
#define LED_ACTIVE_LOW      1

// Valeur de champ AIOMUX1 pour basculer une broche AIOx en mode analogique
#define GPIO_ANALOG_MODE    2

// Seuil de protection courant shunt (identique etage 1 et 2)
// Shunt 0.02 ohm, gain x30 -> Vadc = I * 0.02 * 30 = I * 0.6
#define SAFETY_ISHUNT_THRESHOLD_A   3.0f
#define SAFETY_DAC_VREF_V           3.3f
#define SAFETY_DAC_CODE \
    ((uint16_t)((SAFETY_ISHUNT_THRESHOLD_A * 0.6f) / SAFETY_DAC_VREF_V * 1023.0f + 0.5f))

#endif
