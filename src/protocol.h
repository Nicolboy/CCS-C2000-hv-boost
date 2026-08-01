#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>

// Squelette du protocole UART TMS320<->ESP32 (voir docs/ESP32-UART.md).
// Rempli/utilise a partir de l'etape 7 (uart_link.c).

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
} telemetry_t;

typedef struct
{
    bool ht_enabled;
    bool pwm1_enabled;
    bool pwm2_enabled;
} command_state_t;

#endif
