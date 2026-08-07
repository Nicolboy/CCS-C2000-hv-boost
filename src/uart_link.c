#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "DSP28x_Project.h"
#include "uart_link.h"
#include "calib.h"

static volatile uint8_t s_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0; // ecrit par l'ISR uniquement
static uint16_t s_rx_tail = 0;          // ecrit par uart_link_poll() uniquement

static char s_line[UART_LINE_MAX];
static uint16_t s_line_len = 0;

interrupt void scia_rx_isr(void);

// Profondeur de la FIFO d'emission du SCI (TRM SPRUI09A) : on ne pousse
// jamais au-dela, ce qui garantit que uart_link_service_tx() rend la main
// immediatement au lieu d'attendre le debit de la ligne.
#define SCI_TX_FIFO_DEPTH   4U

static uint8_t checksum_of(const char *s, int len)
{
    uint8_t cs = 0;
    int i;
    for (i = 0; i < len; i++)
    {
        cs ^= (uint8_t)s[i];
    }
    return cs;
}

void uart_link_init(void)
{
    EALLOW;
    GpioCtrlRegs.GPAPUD.bit.GPIO28 = 0;   // pull-up sur RX
    GpioCtrlRegs.GPAPUD.bit.GPIO29 = 1;   // pas de pull-up sur TX (sortie)
    GpioCtrlRegs.GPAQSEL2.bit.GPIO28 = 3; // RX asynchrone, pas de qualification
    GpioCtrlRegs.GPAMUX2.bit.GPIO28 = 1;  // GPIO28 -> SCIRXDA
    GpioCtrlRegs.GPAMUX2.bit.GPIO29 = 1;  // GPIO29 -> SCITXDA
    EDIS;

    // 1 stop bit, pas de parite, 8 bits, mode async, protocole idle-line
    SciaRegs.SCICCR.all = 0x0007;
    // TX/RX actives, horloge SCI interne
    SciaRegs.SCICTL1.all = 0x0003;
    SciaRegs.SCICTL2.bit.TXINTENA = 0;   // TX en polling, pas d'interruption
    SciaRegs.SCICTL2.bit.RXBKINTENA = 1;

    SciaRegs.SCIHBAUD = (UART_SCIBRR >> 8) & 0xFF;
    SciaRegs.SCILBAUD = UART_SCIBRR & 0xFF;

    SciaRegs.SCIFFTX.all = 0xE040; // FIFO active, TX en polling (pas d'IT FIFO)
    SciaRegs.SCIFFRX.all = 0x2021; // FIFO active, RXFFIENA=1, seuil=1 octet
    SciaRegs.SCIFFCT.all = 0x0;

    SciaRegs.SCICTL1.all = 0x0023; // sort le SCI de son reset local

    EALLOW;
    PieVectTable.SCIRXINTA = &scia_rx_isr;
    EDIS;

    IER |= M_INT9;
    PieCtrlRegs.PIEIER9.bit.INTx1 = 1; // SCIRXINTA
}

// Buffer d'emission statique et non local : evite 160 mots de pile a chaque
// envoi (la pile ne fait que 1024 mots et sprintf en consomme deja beaucoup).
static char s_tx_frame[UART_LINE_MAX];
static uint16_t s_tx_len = 0; // longueur de la trame en attente d'emission
static uint16_t s_tx_pos = 0; // prochain octet a pousser dans la FIFO
static uint16_t s_tx_dropped = 0; // trames abandonnees (ligne trop lente)
static uint16_t s_tx_truncated = 0; // trames ayant perdu des champs (borne)

// Place a reserver en fin de buffer : "*XX\n" + le NUL ecrit par snprintf.
#define UART_TX_TAIL_LEN    5

// Garde-fou sur les valeurs formatees : un flottant aberrant (capteur non
// encore cable, ADC non initialise, NaN) rendrait le cast en int32
// indefini, donc un champ de longueur arbitraire.
#define UART_FIELD_ABS_MAX  999999.0f

// Formatage decimal a la main, SANS "%f". Sur C28x le support flottant de
// printf passe par du long double 64 bits emule (frexpl/scalbnl/L$$DIV) :
// tres couteux en pile et en temps, et constate au bring-up comme faisant
// partir le CPU dans le decor (PC=0) des le premier envoi de telemetrie.
// Voir aussi PROMPT §7 etape 7, qui prevoyait deja ce repli.
//
// `room` = octets disponibles pour ce champ, NUL compris. Renvoie 0 (champ
// entierement abandonne, rien d'ecrit) s'il ne tient pas : le protocole
// autorise les champs manquants, l'ESP32 garde alors sa derniere valeur
// (docs/ESP32-UART.md). Mieux vaut une trame courte mais valide qu'un
// debordement de s_tx_frame.
static int append_field(char *out, int room, const char *tag, float value,
                        uint16_t decimals)
{
    static const int32_t k_pow10[3] = {1L, 10L, 100L};
    const char *sign = "";
    int32_t scaled;
    int32_t ip;
    int32_t fp;
    int n;

    if (room <= 1)
    {
        return 0;
    }

    // value != value n'est vrai que pour un NaN.
    if (value != value)
    {
        value = 0.0f;
    }
    else if (value < -UART_FIELD_ABS_MAX)
    {
        value = -UART_FIELD_ABS_MAX;
    }
    else if (value > UART_FIELD_ABS_MAX)
    {
        value = UART_FIELD_ABS_MAX;
    }

    scaled = (int32_t)(value * (float)k_pow10[decimals]
                       + ((value >= 0.0f) ? 0.5f : -0.5f));
    if (scaled < 0)
    {
        scaled = -scaled;
        sign = "-";
    }
    ip = scaled / k_pow10[decimals];
    fp = scaled % k_pow10[decimals];

    if (decimals == 0)
    {
        n = snprintf(out, (size_t)room, ",%s=%s%ld", tag, sign, ip);
    }
    else if (decimals == 1)
    {
        n = snprintf(out, (size_t)room, ",%s=%s%ld.%01ld", tag, sign, ip, fp);
    }
    else
    {
        n = snprintf(out, (size_t)room, ",%s=%s%ld.%02ld", tag, sign, ip, fp);
    }

    if (n < 0 || n >= room)
    {
        out[0] = '\0'; // tronque : on annule le champ au lieu de le laisser coupe
        return 0;
    }
    return n;
}

void uart_link_restart(void)
{
    // Reset logiciel du SCI : vide les FIFO et efface les etats d'erreur.
    SciaRegs.SCICTL1.bit.SWRESET = 0;
    SciaRegs.SCICTL1.bit.SWRESET = 1;

    SciaRegs.SCIFFRX.bit.RXFIFORESET = 0;
    SciaRegs.SCIFFRX.bit.RXFIFORESET = 1;
    SciaRegs.SCIFFRX.bit.RXFFOVRCLR = 1;
    SciaRegs.SCIFFRX.bit.RXFFINTCLR = 1;

    SciaRegs.SCIFFTX.bit.TXFIFOXRESET = 0;
    SciaRegs.SCIFFTX.bit.TXFIFOXRESET = 1;

    // On jette le contenu de l'anneau en avancant seulement la QUEUE.
    // s_rx_head appartient a l'ISR : ne pas y toucher evite toute course.
    s_rx_tail = s_rx_head;
    s_line_len = 0;

    // Trame d'emission en cours abandonnee : elle date d'avant l'arret.
    s_tx_len = 0;
    s_tx_pos = 0;
}

// Champ entier (FAULT). Meme convention de bornage que append_field :
// renvoie 0 et n'ecrit rien si le champ ne tient pas.
static int append_field_int(char *out, int room, const char *tag, int value)
{
    int n;

    if (room <= 1)
    {
        return 0;
    }

    n = snprintf(out, (size_t)room, ",%s=%d", tag, value);
    if (n < 0 || n >= room)
    {
        out[0] = '\0';
        return 0;
    }
    return n;
}

// Construit la trame et la met en attente. NE BLOQUE PAS : l'emission reelle
// se fait ensuite par uart_link_service_tx(), appelee quand la boucle
// principale a du temps disponible. Renvoie false si la trame precedente
// n'est pas encore partie (nouvelle trame abandonnee, la telemetrie est par
// nature perissable : mieux vaut la suivante que du retard accumule).
bool uart_link_send_telemetry(const telemetry_t *t)
{
    int n;
    int fields_dropped;
    uint8_t cs;

    if (uart_link_tx_busy())
    {
        s_tx_dropped++;
        return false;
    }

    s_tx_frame[0] = '$';
    s_tx_frame[1] = 'T';
    n = 2;
    fields_dropped = 0;

#define TX_ROOM()   ((int)UART_LINE_MAX - UART_TX_TAIL_LEN - n)
#define TX_ADD(tag_, val_, dec_)                                             \
    do {                                                                     \
        int added_ = append_field(&s_tx_frame[n], TX_ROOM(), (tag_), (val_), \
                                  (dec_));                                   \
        if (added_ == 0) { fields_dropped++; } else { n += added_; }          \
    } while (0)

#define TX_ADD_INT(tag_, val_)                                               \
    do {                                                                     \
        int added_ = append_field_int(&s_tx_frame[n], TX_ROOM(), (tag_),     \
                                      (val_));                               \
        if (added_ == 0) { fields_dropped++; } else { n += added_; }          \
    } while (0)

    // ETAT DE SECURITE EN TETE, avant toute mesure. Un champ qui ne tient
    // pas dans la trame est ABANDONNE, et l'ESP32 conserve alors sa derniere
    // valeur connue : si FAULT etait emis en dernier, un debordement le
    // ferait disparaitre et l'ESP32 continuerait d'afficher 0 pendant qu'un
    // defaut reel est actif. Les grandeurs analogiques, elles, peuvent etre
    // perdues sans consequence.
    TX_ADD_INT("FAULT", (int)t->fault);
    TX_ADD_INT("STATE", (int)t->state);
    TX_ADD_INT("REJ", (int)t->rejected);

    TX_ADD("FREQ1", t->freq1_hz, 0);
    TX_ADD("FREQ2", t->freq2_hz, 0);
    TX_ADD("DUTY1", t->duty1_pct, 1);
    TX_ADD("DUTY2", t->duty2_pct, 1);
    TX_ADD("VIN", t->vin_v, 1);
    TX_ADD("IIN", t->iin_a, 2);
    TX_ADD("V1", t->v1_v, 1);
    TX_ADD("I1", t->i1_a, 2);
    TX_ADD("T1", t->t1_c, 1);
    TX_ADD("VOUT", t->vout_v, 1);
    TX_ADD("I2", t->i2_a, 2);
    TX_ADD("T2", t->t2_c, 1);
    TX_ADD("IOUT", t->iout_a, 2);

    // Consignes REELLEMENT appliquees : leur ecart avec ce que l'ESP32 a
    // envoye est la seule facon pour lui de voir qu'une consigne a ete
    // refusee. REJ compte les refus depuis le demarrage.
    // VOSP = 0 signifie etage 2 DESACTIVE, pas une consigne de 0 V.
    TX_ADD("V1SP", t->v1_setpoint_v, 1);
    TX_ADD("VOSP", t->vout_setpoint_v, 1);

#undef TX_ADD_INT
#undef TX_ADD
#undef TX_ROOM

    if (fields_dropped != 0)
    {
        s_tx_truncated++;
    }

    // Checksum XOR sur le corps seul, entre '$' et '*' (docs/ESP32-UART.md).
    // La place a ete reservee par UART_TX_TAIL_LEN, l'ecriture tient toujours.
    cs = checksum_of(&s_tx_frame[1], n - 1);
    n += snprintf(&s_tx_frame[n], (size_t)((int)UART_LINE_MAX - n),
                  "*%02X\n", cs);

    s_tx_len = (uint16_t)n;
    s_tx_pos = 0;
    return true;
}

bool uart_link_tx_busy(void)
{
    return (s_tx_pos < s_tx_len);
}

void uart_link_service_tx(void)
{
    while ((s_tx_pos < s_tx_len)
           && (SciaRegs.SCIFFTX.bit.TXFFST < SCI_TX_FIFO_DEPTH))
    {
        SciaRegs.SCITXBUF = (uint16_t)(uint8_t)s_tx_frame[s_tx_pos];
        s_tx_pos++;
    }
}

// Decode un nombre decimal positif de la forme "123" ou "123.45", borne a
// MAX_DIGITS chiffres de part et d'autre du point. S'arrete au premier
// caractere non numerique (virgule de separation, '*', fin de chaine).
//
// Volontairement PAS atof/strtod : meme raisonnement que pour le checksum,
// s_line n'est pas terminee et ces fonctions liraient au-dela. Et le
// support flottant complet du RTS C28x est couteux (voir l'historique de
// %f dans ce fichier).
//
// Renvoie false si aucun chiffre n'est trouve.
#define PARSE_MAX_DIGITS  6U

static bool parse_float(const char *s, float *out)
{
    uint32_t integer_part = 0U;
    uint32_t frac_part = 0U;
    uint32_t frac_scale = 1U;
    uint16_t digits = 0U;
    uint16_t i = 0U;

    while ((s[i] >= '0') && (s[i] <= '9') && (digits < PARSE_MAX_DIGITS))
    {
        integer_part = (integer_part * 10U) + (uint32_t)(s[i] - '0');
        digits++;
        i++;
    }

    if (digits == 0U)
    {
        return false;
    }

    if (s[i] == '.')
    {
        i++;
        digits = 0U;
        while ((s[i] >= '0') && (s[i] <= '9') && (digits < PARSE_MAX_DIGITS))
        {
            frac_part = (frac_part * 10U) + (uint32_t)(s[i] - '0');
            frac_scale *= 10U;
            digits++;
            i++;
        }
    }

    *out = (float)integer_part + ((float)frac_part / (float)frac_scale);
    return true;
}

// Cherche "TAG=" et decode la valeur numerique qui suit.
static bool find_float(const char *body, const char *tag, float *out)
{
    const char *p = strstr(body, tag);

    if (p == NULL)
    {
        return false;
    }
    return parse_float(p + strlen(tag), out);
}

// body pointe juste apres "C," ; len = nb de caracteres avant le '*'.
//
// Les tags sont tous OPTIONNELS et un tag inconnu est ignore : l'ESP32 peut
// donc evoluer sans casser le TMS, et inversement. Une trame est acceptee
// des lors qu'elle contient au moins un tag reconnu -- sinon on la rejette,
// pour ne pas rafraichir le timeout de liaison sur une trame vide de sens.
static bool parse_command(const char *body, command_state_t *cmd)
{
    const char *p;
    bool found = false;
    float value;

    p = strstr(body, "HT=");
    if (p != NULL)
    {
        cmd->ht_enabled = (p[3] == '1');
        found = true;
    }

    p = strstr(body, "RUN=");
    if (p != NULL)
    {
        cmd->run = (p[4] == '1');
        found = true;
    }

    if (find_float(body, "V1SET=", &value))
    {
        cmd->v1_set_v = value;
        found = true;
    }

    if (find_float(body, "VOSET=", &value))
    {
        cmd->vout_set_v = value;
        found = true;
    }

    // Tags historiques : encore acceptes, ne pilotent plus rien.
    p = strstr(body, "PWM1=");
    if (p != NULL)
    {
        cmd->pwm1_enabled = (p[5] == '1');
        found = true;
    }

    p = strstr(body, "PWM2=");
    if (p != NULL)
    {
        cmd->pwm2_enabled = (p[5] == '1');
        found = true;
    }

    return found;
}

// Decode EXACTEMENT deux chiffres hexadecimaux. Renvoie false si l'un des
// deux n'en est pas un.
//
// Remplace strtol, qui etait doublement piegeux ici :
//  - s_line n'est jamais terminee par un '\0', donc strtol lisait au-dela
//    de la trame, dans les residus de la ligne precedente ;
//  - sur C28x, uint8_t fait 16 bits (pas d'adressage par octet), donc le
//    cast (uint8_t) ne tronquait pas le resultat. Un "7D" suivi d'un "7D"
//    residuel donnait 0x7D7D, retenu tel quel, et toute trame etait
//    rejetee. Sur une architecture a octets le bug serait passe inapercu.
static bool parse_hex2(const char *s, uint8_t *out)
{
    uint16_t value = 0U;
    uint16_t i;

    for (i = 0U; i < 2U; i++)
    {
        char c = s[i];
        uint16_t digit;

        if ((c >= '0') && (c <= '9'))
        {
            digit = (uint16_t)(c - '0');
        }
        else if ((c >= 'A') && (c <= 'F'))
        {
            digit = (uint16_t)(c - 'A' + 10);
        }
        else if ((c >= 'a') && (c <= 'f'))
        {
            digit = (uint16_t)(c - 'a' + 10);
        }
        else
        {
            return false;
        }

        value = (uint16_t)((value << 4) | digit);
    }

    *out = (uint8_t)(value & 0xFFU);
    return true;
}

// s_line contient une ligne complete (sans le '\n' terminal).
static bool process_line(command_state_t *cmd)
{
    int len = (int)s_line_len;
    int i;
    int star = -1;
    uint8_t cs_calc, cs_recv;

    if (len > 0 && s_line[len - 1] == '\r')
    {
        len--;
    }

    if (len < 4 || s_line[0] != '$')
    {
        return false;
    }

    for (i = 1; i < len; i++)
    {
        if (s_line[i] == '*')
        {
            star = i;
            break;
        }
    }
    if (star < 0 || (len - star) < 3)
    {
        return false; // pas de checksum complet
    }

    cs_calc = checksum_of(&s_line[1], star - 1);
    if (!parse_hex2(&s_line[star + 1], &cs_recv))
    {
        return false; // checksum malforme -> trame ignoree
    }
    if (cs_calc != cs_recv)
    {
        return false; // trame corrompue -> ignoree silencieusement
    }

    if (s_line[1] != 'C' || s_line[2] != ',')
    {
        return false; // pas une trame de commande
    }

    s_line[star] = '\0';
    return parse_command(&s_line[3], cmd);
}

// Compteur de recuperations, expose au debogueur : une valeur qui grimpe
// signale un probleme physique sur la ligne (baud, masse, niveaux), pas un
// simple alea.
static uint16_t s_rx_error_recoveries = 0;

// Sur C28x, un FE / OE / PE / BRKDT positionne SCIRXST.RXERROR, qui est
// LATCHE : le recepteur reste bloque tant qu'on ne fait pas un SW RESET du
// SCI (TRM SPRUI09A, SCICTL1.SWRESET) ou un reset systeme. Sans ce
// traitement, une seule perturbation sur la ligne arrete definitivement la
// reception -- constate au bring-up (RXERROR+FE+BRKDT latches, plus aucune
// trame $C recue ensuite).
static void scia_recover_if_rx_error(void)
{
    if (SciaRegs.SCIRXST.bit.RXERROR == 0)
    {
        return;
    }

    SciaRegs.SCICTL1.bit.SWRESET = 0;
    SciaRegs.SCICTL1.bit.SWRESET = 1;

    SciaRegs.SCIFFRX.bit.RXFIFORESET = 0;
    SciaRegs.SCIFFRX.bit.RXFIFORESET = 1;
    SciaRegs.SCIFFRX.bit.RXFFOVRCLR = 1;
    SciaRegs.SCIFFRX.bit.RXFFINTCLR = 1;

    // La ligne en cours d'assemblage est forcement tronquee : on repart propre.
    s_line_len = 0;
    s_rx_error_recoveries++;
}

bool uart_link_poll(command_state_t *cmd)
{
    bool got_command = false;

    scia_recover_if_rx_error();

    while (s_rx_tail != s_rx_head)
    {
        uint8_t c = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % UART_RX_RING_SIZE);

        if (c == '\n')
        {
            if (process_line(cmd))
            {
                got_command = true;
            }
            s_line_len = 0;
        }
        else if (s_line_len < (UART_LINE_MAX - 1U))
        {
            s_line[s_line_len++] = (char)c;
        }
        else
        {
            // Ligne trop longue (> limite cote ESP32) : abandonnee.
            s_line_len = 0;
        }
    }

    return got_command;
}

interrupt void scia_rx_isr(void)
{
    uint16_t next_head = (uint16_t)((s_rx_head + 1U) % UART_RX_RING_SIZE);

    if (next_head != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = (uint8_t)SciaRegs.SCIRXBUF.bit.RXDT;
        s_rx_head = next_head;
    }
    else
    {
        (void)SciaRegs.SCIRXBUF.bit.RXDT; // anneau plein : octet jete
    }

    SciaRegs.SCIFFRX.bit.RXFFOVRCLR = 1;
    SciaRegs.SCIFFRX.bit.RXFFINTCLR = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP9;
}
