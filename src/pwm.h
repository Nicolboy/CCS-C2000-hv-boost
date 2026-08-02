#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_gpio.h" // stage_id_t

// ePWM1 -> etage 1 (EPWM1A, GPIO0, broche 29)
// ePWM2 -> etage 2 (EPWM2A, GPIO2, broche 37)
//
// Initialise les deux etages a leur frequence nominale (calib.h), duty a 0
// et sorties inhibees. PROMPT §6 etape 6 : ne jamais demarrer au duty
// nominal, le condensateur de sortie vide provoquerait un appel de courant
// destructeur. La rampe de demarrage (soft-start) est du ressort de
// l'appelant, pas de ce module.
void pwm_init(void);

void pwm_set_freq(stage_id_t stage, uint32_t hz);

// duty exprime en 0..1. Sature hors bornes.
void pwm_set_duty(stage_id_t stage, float duty_0_1);

// Inhibition logicielle par forcage continu de la sortie a l'etat bas
// (AQCSFRC). N'a rien a voir avec la protection materielle : le Trip Zone
// reste le seul mecanisme de securite (PROMPT §8).
void pwm_enable(stage_id_t stage, bool enabled);

#endif
