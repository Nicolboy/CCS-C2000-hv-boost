#ifndef STATUS_LED_H
#define STATUS_LED_H

// Affichage d'etat sur la LED bicolore (bleue = GPIO12, rouge = GPIO33).
// Les motifs sont volontairement tous distinguables a l'oeil :
//
//   STARTUP      bleu fixe
//   NOMINAL      bleu 0,2 s ON / 1,8 s OFF   (battement discret)
//   LINK_LOST    bleu 0,2 s ON / 0,2 s OFF   (clignotement rapide)
//   EMUSTOP      alternance bleu/rouge 0,5 s / 0,5 s
//   OVERTEMP     rouge 0,5 s ON / 0,5 s OFF
//   OVERCURRENT  rouge fixe
//
// L'ordre de l'enumeration EST l'ordre de priorite croissante : quand
// plusieurs conditions sont vraies, on affiche la plus grande valeur.
// La surintensite passe donc devant EMUSTOP -- pendant le developpement
// EMUSTOP se declenche en permanence et ne doit jamais masquer un vrai
// defaut de puissance.
typedef enum
{
    LED_STATE_STARTUP = 0,
    LED_STATE_NOMINAL,
    LED_STATE_LINK_LOST,
    LED_STATE_EMUSTOP,
    LED_STATE_OVERTEMP,
    LED_STATE_OVERCURRENT
} led_state_t;

void status_led_init(void);

// Etat souhaite, calcule dans la boucle principale. Tout changement
// redemarre le motif a zero. Les demandes de sortie de STARTUP sont
// ignorees tant que la duree minimale d'affichage n'est pas ecoulee,
// sans quoi cet etat ne serait jamais visible (l'init dure quelques ms).
void status_led_set_state(led_state_t state);

// A appeler a chaque tick de 10 ms. Ne fait qu'ecrire GPASET/GPACLEAR,
// donc utilisable depuis une ISR.
void status_led_tick(void);

#endif
