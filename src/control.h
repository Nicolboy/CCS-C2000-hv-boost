#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

// Emplacement de la future regulation (PROMPT §6 etape 8). Pour l'instant
// ce module ne contient PAS de regulation : il execute un balayage de duty
// en boucle OUVERTE, destine a caracteriser la vitesse de correction
// atteignable. Aucune mesure n'influence encore le duty.
//
// Le balayage va de CONTROL_SWEEP_MIN_PCT a CONTROL_SWEEP_MAX_PCT puis
// revient, par pas de 1 LSB de CMPA a chaque conversion ADC -- c'est-a-dire
// le plus petit increment realisable, et la cadence la plus rapide dont on
// dispose sans ajouter d'interruption.

void control_init(void);

// Arme le balayage et repositionne les deux etages au duty minimal.
void control_start(void);

// Fige le balayage. Le duty n'est pas modifie : c'est pwm_enable() et le
// Trip Zone qui coupent reellement les sorties.
void control_stop(void);

// A appeler a CADENCE FIXE depuis l'ISR ADC (PROMPT §6 etape 8 : "prevoir
// l'emplacement d'appel cadence, dt constant"). Ne fait rien si le balayage
// n'est pas arme. Sans flottant ni division : un increment et une ecriture
// de registre par etage.
void control_tick(void);

#endif
