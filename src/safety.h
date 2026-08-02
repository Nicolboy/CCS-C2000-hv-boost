#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>

typedef struct
{
    bool stage1_fault;
    bool stage2_fault;
} safety_faults_t;

void safety_init(void);

// BRING-UP UNIQUEMENT -- arme seulement TZ6 (EMUSTOP) et l'interruption de
// diagnostic, SANS les comparateurs ni le Digital Compare. Permet de valider
// isolement la coupure du PWM a l'arret du debugger, tant que les entrees
// shunt ne sont pas cablees : avec safety_init() complet, elles flottent
// au-dessus du seuil et le trip comparateur masquerait le test.
//
// safety_init() arme deja TZ6 : ces deux fonctions sont exclusives.
void safety_arm_emustop_only(void);

safety_faults_t safety_get_fault_flags(void);

// Ecrit TZCLR[OST] sur les deux etages. Ne doit JAMAIS etre appele
// automatiquement par le firmware (voir document PROMPT, section 8) --
// uniquement sur commande explicite (UART, etape 7).
void safety_clear_faults(void);

// Force un trip logiciel (TZFRC[OSHT]) pour valider le mecanisme de
// protection sans provoquer de vrai court-circuit (critere de test etape 2).
void safety_force_trip_test(void);

#endif
