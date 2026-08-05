#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>

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
    FAULT_OVERTEMP_T2 = 5
} fault_code_t;

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
} telemetry_t;

typedef struct
{
    bool ht_enabled;
    bool pwm1_enabled;
    bool pwm2_enabled;
} command_state_t;

#endif
