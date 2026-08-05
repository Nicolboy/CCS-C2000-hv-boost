#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

typedef enum
{
    LED_BLUE = 0,
    LED_RED
} led_id_t;

typedef enum
{
    STAGE_1 = 0,
    STAGE_2
} stage_id_t;

void bsp_gpio_leds_init(void);
void led_set(led_id_t led, bool on);

// Bascule AIO2, AIO4, AIO10, AIO12, AIO14 en mode analogique (shunts,
// COMP1A/COMP2A, VOUT, V1, NTC1). A appeler avant safety_init()/adc_init().
void bsp_gpio_analog_init(void);

// Sorties de commande dans leur etat sur : HV_EN, Stage1-EN et Stage2-EN a 0.
// Desactive aussi les pull-ups internes de GPIO16/17, qui sinon presentent un
// niveau haut sur une entree de chaque porte ET des la mise sous tension
// (PROMPT §6 etape 3).
void bsp_gpio_control_init(void);

// Stagex-EN : entree "autorisation logicielle" de la porte ET de l'etage.
void stage_enable_set(stage_id_t stage, bool enabled);

// HV_EN (GPIO32) : commande de la sortie HT via l'optocoupleur VOM1271.
// Seul organe qui isole reellement la charge -- un boost a 0 % de duty ne
// donne PAS 0 V en sortie (PROMPT §6 etape 6).
void hv_enable_set(bool enabled);

// DISCHARGE (GPIO5, broche 40) : circuit de decharge rapide du condensateur
// de sortie, sur la carte de puissance. 47 kOhm en serie avec un MOSFET,
// soit ~2 s pour passer de 500 V a 50 V (tau = 0,94 s).
//
// POLARITE INVERSEE, et c'est le coeur du montage. Le GPIO pilote un NPN
// qui court-circuite la grille du MOSFET :
//   niveau HAUT -> NPN conduit -> grille a la masse -> MOSFET bloque
//                  -> decharge INHIBEE (fonctionnement normal)
//   niveau BAS, ou broche non pilotee
//               -> NPN bloque -> grille tiree a 12 V (chaine 3x1 MOhm
//                  depuis le +500 V, ecretee par Zener) -> MOSFET conduit
//                  -> decharge ACTIVE
//
// GPIO5 est une broche a fonction PWM : ses pull-ups ne sont PAS actives au
// reset (PROMPT §6 etape 3), elle flotte donc. La decharge est par
// consequent active au reset, pendant un plantage, et surtout ALIMENTATION
// COUPEE -- c'est-a-dire exactement le cas ou le firmware ne peut plus rien
// garantir et ou l'operateur croit la carte inoffensive.
void hv_discharge_set(bool active);

// BRING-UP UNIQUEMENT -- bascule Stage1/2-default (GPIO1/GPIO3) en sortie
// GPIO pilotee par logiciel, pour verifier le cablage des portes ET sans
// etage de puissance actif. Laisse les deux broches a 0.
//
// INCOMPATIBLE avec safety_init(), qui mux ces memes broches vers COMP1OUT /
// COMP2OUT : en fonctionnement normal ce sont les comparateurs qui les
// pilotent, et le logiciel ne doit JAMAIS pouvoir outrepasser la protection
// materielle (PROMPT §2 et §8). Ne jamais appeler les deux.
void bsp_gpio_stage_default_override_init(void);

// Pilote une broche Stagex-default apres l'override ci-dessus. N'ecrit que
// GPASET/GPACLEAR (non proteges par EALLOW) : utilisable depuis une ISR.
void stage_default_set(stage_id_t stage, bool level);

#endif
