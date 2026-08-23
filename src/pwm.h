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

// ---- Reglage fin, en Q8 counts ---------------------------------------
// Meme unite que ci-dessus mais avec PWM_DUTY_FRAC_BITS bits de partie
// fractionnaire. Sur un etage ou HRPWM est actif, cette fraction est
// portee par CMPAHR et vaut environ 1/111 de count reel ; ailleurs elle
// est simplement tronquee, et la fonction se comporte exactement comme
// pwm_set_duty_counts(). C'est le point d'entree de la regulation.
//
// Appelable depuis l'ISR : deux decalages, un masque, une ecriture 32 bits.
void pwm_set_duty_q8(stage_id_t stage, uint32_t duty_q8);

// Avance du declenchement de la sequence ADC, en counts de TBCLK, SIGNEE
// (negatif = retarde). Initialisee a ADC_TRIG_LEAD_COUNTS. Ecrivable au
// debogueur, et balayee automatiquement quand ADC_TRIG_SWEEP est actif.
// Voir pwm.c pour ce qu'elle sert a diagnostiquer.
extern volatile int16_t g_adc_trig_lead;

// ---- Calibration MEP (SFO) -------------------------------------------
// Le pas du Micro Edge Positioner depend de la temperature et de la
// tension d'alimentation. La bibliotheque SFO de TI mesure ce pas et
// entretient HRMSTEP, dont le materiel se sert pour convertir la fraction
// de CMPAHR en pas MEP.
//
// pwm_hrpwm_init() boucle jusqu'a la premiere calibration complete. Elle
// est appelee par pwm_init(), sorties encore inhibees.
//
// pwm_hrpwm_service() doit etre appelee REGULIEREMENT depuis la boucle de
// fond, jamais depuis une ISR : la bibliotheque n'est pas reentrante.
// Sans elle, HRMSTEP se fige a sa valeur de demarrage et la finesse
// derive avec la temperature de la puce. Renvoie false sur erreur de
// calibration (pas MEP au-dela de 255, cf. sfo_v6.h).
bool pwm_hrpwm_init(void);
bool pwm_hrpwm_service(void);

// Inhibition logicielle par forcage continu de la sortie a l'etat bas
// (AQCSFRC). N'a rien a voir avec la protection materielle : le Trip Zone
// reste le seul mecanisme de securite (PROMPT §8).
void pwm_enable(stage_id_t stage, bool enabled);

#endif
