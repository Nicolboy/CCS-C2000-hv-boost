#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include "protocol.h"

void uart_link_init(void);

// Construit une trame $T,...*XX et la met en attente (docs/ESP32-UART.md).
// NE BLOQUE PAS : rien n'est emis ici, c'est uart_link_service_tx() qui
// alimente la ligne ensuite. Renvoie false si la trame precedente n'est pas
// encore partie -- la nouvelle est alors abandonnee.
bool uart_link_send_telemetry(const telemetry_t *t);

// Pousse dans la FIFO du SCI ce qui y rentre, puis rend la main
// immediatement. A appeler depuis la boucle principale, en tache de fond :
// l'emission ne doit jamais passer avant la regulation.
void uart_link_service_tx(void);

// true tant qu'une trame reste a emettre.
bool uart_link_tx_busy(void);

// A appeler dans la boucle principale (jamais en ISR). Si une trame
// $C,...*XX complete et de checksum valide a ete recue depuis le dernier
// appel, decode son contenu dans *cmd et renvoie true. Les trames
// corrompues sont ignorees silencieusement (voir docs/ESP32-UART.md).
bool uart_link_poll(command_state_t *cmd);

#endif
