#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>

// Conversion des valeurs brutes de l'ADC (0..4095) vers les grandeurs
// physiques, a partir des seules constantes de calib.h (elles-memes issues
// de docs/mesure-cartepuissance.md).
//
// Ces fonctions utilisent du flottant et, pour les NTC, un logarithme :
// a n'appeler QUE depuis la boucle principale, jamais depuis une ISR
// (PROMPT §7). La protection rapide ne passe pas par ici.

float measure_vin(uint16_t raw);   // V   - tension d'entree batterie
float measure_iin(uint16_t raw);   // A   - courant d'entree
float measure_v1(uint16_t raw);    // V   - tension intermediaire (etage 1)
float measure_vout(uint16_t raw);  // V   - tension de sortie HT
float measure_iout(uint16_t raw);  // A   - courant de sortie HT

// Courant shunt MOSFET. Deux fonctions distinctes et non une seule
// parametree : les deux voies ont leur propre gain ET leur propre offset,
// mesures separement.
float measure_i1(uint16_t raw);    // A   - shunt etage 1
float measure_i2(uint16_t raw);    // A   - shunt etage 2

// Temperature NTC, en degres Celsius. Meme chaine de mesure sur les deux
// etages, donc une seule fonction.
float measure_temp(uint16_t raw);  // degC

#endif
