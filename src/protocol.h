#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

// Squelette du protocole UART TMS320<->ESP32 (voir docs/ESP32-UART.md).
// Rempli/utilise a partir de l'etape 7 (uart_link.c).

// Codes du champ FAULT de la trame $T (docs/ESP32-UART.md).
//
// Attention a la priorite d'affichage quand plusieurs defauts coexistent :
// elle est 2 > 3 > 4 > 5 > 1, donc EMUSTOP a la priorite la PLUS BASSE
// malgre son numero. C'est voulu : en developpement EMUSTOP se declenche a
// chaque halte du debogueur et ne doit jamais masquer une surintensite.
//
// Il n'existe volontairement AUCUN code de court-circuit : un court-circuit
// franchit le meme comparateur et le meme seuil qu'une surintensite, le
// firmware ne dispose d'aucune information permettant de les distinguer.
typedef enum
{
    FAULT_NONE = 0,
    FAULT_EMUSTOP = 1,
    FAULT_OVERCURRENT_I1 = 2,
    FAULT_OVERCURRENT_I2 = 3,
    FAULT_OVERTEMP_T1 = 4,
    FAULT_OVERTEMP_T2 = 5,
    FAULT_OVERVOLTAGE_V1 = 6,
    FAULT_OVERVOLTAGE_VOUT = 7,
    FAULT_LINK_LOST = 8,
    FAULT_UNDERVOLTAGE_VIN = 9
} fault_code_t;

// Etat de la machine de conduite (control.c). Le demarrage est CASCADE :
// on etablit V_inter avant de lancer l'etage 2, pour qu'il demarre avec une
// tension d'entree connue et donc un rapport cyclique initial coherent.
typedef enum
{
    CTRL_STATE_IDLE = 0,     // tout coupe, decharge active
    CTRL_STATE_START_S1 = 1, // etage 1 : consigne rampee depuis VIN
    CTRL_STATE_RUN_S1 = 2,   // etage 1 etabli, etage 2 toujours a 0
    CTRL_STATE_START_S2 = 3, // etage 2 : consigne rampee depuis V1
    CTRL_STATE_RUN = 4,      // les deux regules
    CTRL_STATE_FAULT = 5     // etat sur verrouille
} ctrl_state_t;

typedef struct
{
    float freq1_hz;
    float freq2_hz;
    float duty1_pct;
    float duty2_pct;
    float vin_v;
    float iin_a;
    float v1_v;
    float i1_a;
    float t1_c;
    float vout_v;
    float i2_a;
    float t2_c;
    float iout_a;
    fault_code_t fault;

    ctrl_state_t state;   // etat de la machine de conduite
    float v1_setpoint_v;  // consigne REELLEMENT appliquee, pas celle recue :
    float vout_setpoint_v; // l'ecart revele un rejet cote ESP32
    uint16_t rejected;    // nombre de consignes refusees depuis le demarrage
} telemetry_t;

typedef struct
{
    bool ht_enabled;   // commande HV_EN
    bool run;          // demande de marche
    float v1_set_v;    // consigne V_inter
    float vout_set_v;  // consigne sortie HT

    // Champs historiques, conserves pour compatibilite : l'ESP32 peut encore
    // les emettre, ils ne pilotent plus rien (RUN les remplace).
    bool pwm1_enabled;
    bool pwm2_enabled;
} command_state_t;

#endif
