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
//
// vout_set_v == 0 (CTRL_VOSET_DISABLED) desactive l'etage 2 : la machine
// s'arrete a l'etage 1 etabli et HV_EN reste interdit. Ce mode sert a
// valider le premier etage seul, etage 2 non monte.
//
// Bascule activer/desactiver l'etage 2 : REFUSEE tant que la machine n'est
// pas a l'arret (RUN=0). Changer cela en marche produirait un a-coup de
// rapport cyclique ou une coupure hors etat sur.
bool control_set_setpoints(float v1_set_v, float vout_set_v);

// Demande de marche. A false, retour immediat a l'arret.
void control_set_run(bool run);

// Reprise apres EMUSTOP : ramene la machine d'etat au repos et vide
// l'integrateur, de sorte que la conversion reparte par la sequence de
// demarrage complete et non la ou elle s'etait arretee.
//
// Indispensable, et pas seulement pour la proprete : pendant la halte du
// coeur la sortie se vide dans la charge, alors que l'integrateur conserve
// le duty d'avant. Le reappliquer tel quel sur une sortie effondree emballe
// le courant d'inductance -- la desaimantation pendant le temps bloque,
// proportionnelle a (V1 - Vin), devient quasi nulle alors que la
// magnetisation reste entiere.
//
// A appeler depuis la boucle principale. control_set_run(false) ne
// conviendrait PAS : ce drapeau n'est relu qu'au prochain pas de regulation,
// et la boucle principale le repasserait a true avant que l'ISR ne l'ait vu.
void control_restart(void);

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

// false quand la consigne de sortie est nulle : l'etage 2 est desactive et
// son duty reste a zero. La boucle principale s'en sert pour ne pas armer la
// porte ET de l'etage 2 inutilement.
bool control_s2_enabled(void);

// true quand l'etat autorise la mise sous tension de la sortie HT. Toujours
// false si l'etage 2 est desactive : il n'y a alors pas de sortie HT.
bool control_hv_allowed(void);

#endif
