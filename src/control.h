#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

// Conduite de l'alimentation : machine d'etat de demarrage et regulation
// des deux etages (PROMPT §6 etape 8).
//
// Le TMS ne connait qu'UNE consigne a la fois : c'est l'ESP32 qui orchestre
// les sequences d'essai en envoyant les consignes une par une. Aucun
// sequenceur ici, donc rien a verifier de ce cote, et une liaison perdue
// coupe tout immediatement.
//
// Regulation : integrateur pur en virgule fixe, execute dans l'ISR ADC.
// Ni multiplication ni division -- uniquement comparaisons, additions et
// decalages, conformement a la discipline d'interruption du projet.

void control_init(void);

// Consignes, en volts. Renvoie false et NE MODIFIE RIEN si l'une des deux
// est hors bornes : l'ancienne consigne reste appliquee et l'appelant doit
// comptabiliser le rejet. On ne sature pas silencieusement, sinon une
// erreur de commande passerait inapercue.
bool control_set_setpoints(float v1_set_v, float vout_set_v);

// Demande de marche. A false, retour immediat a l'arret.
void control_set_run(bool run);

// Passage en defaut verrouille : les sorties sont coupees et plus rien ne
// redemarre sans repasser par control_init() ou un cycle d'alimentation.
void control_trip(void);

// A appeler a CADENCE FIXE depuis l'ISR ADC. Decime en interne pour obtenir
// le pas de regulation. Ne fait rien tant que la marche n'est pas demandee.
void control_tick(void);

// Lecture d'etat pour la telemetrie et l'affichage (boucle principale).
ctrl_state_t control_get_state(void);

// Defaut detecte PAR control.c lui-meme : survoltage V1 ou VOUT, surveille
// dans l'ISR par comparaison sur la valeur brute. FAULT_NONE sinon. Les
// defauts materiels (surintensite, EMUSTOP) remontent par safety.c.
fault_code_t control_get_fault(void);
float control_get_v1_setpoint(void);
float control_get_vout_setpoint(void);

// true quand l'etat autorise la mise sous tension de la sortie HT.
bool control_hv_allowed(void);

#endif
