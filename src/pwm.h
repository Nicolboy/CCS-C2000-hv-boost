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

// Duty courant (0..1), reconstruit depuis CMPA : reflete donc ce qui sort
// reellement, y compris apres un reglage en counts.
float pwm_get_duty(stage_id_t stage);

// ---- Reglage en pas de compteur --------------------------------------
// Le pas minimal realisable est 1 LSB de CMPA, et il DEPEND DE L'ETAGE :
// 1/300 = 0,333 % a 200 kHz, 1/600 = 0,167 % a 100 kHz. Une consigne en
// flottant ne permet pas d'exprimer "le plus petit pas possible" ; ces
// deux fonctions si.
uint16_t pwm_get_period_counts(stage_id_t stage); // TBPRD + 1
void pwm_set_duty_counts(stage_id_t stage, uint16_t counts);

// Inhibition logicielle par forcage continu de la sortie a l'etat bas
// (AQCSFRC). N'a rien a voir avec la protection materielle : le Trip Zone
// reste le seul mecanisme de securite (PROMPT §8).
void pwm_enable(stage_id_t stage, bool enabled);

#endif
