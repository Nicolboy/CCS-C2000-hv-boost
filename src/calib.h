#ifndef CALIB_H
#define CALIB_H

// Horloge : INTOSC1 (10 MHz nominal) + PLL (DSP28_PLLCR=12, DSP28_DIVSEL=2,
// deja les valeurs par defaut de f2802x_examples.h pour ce device) -> 60 MHz.
// PLLLOCKPRD doit etre ecrit AVANT InitSysCtrl() (qui attend le verrouillage
// PLL en interne) car l'oscillateur interne impose un minimum de 10000.
#define CLK_PLLLOCKPRD      10000
#define CLK_SYSCLKOUT_HZ    60000000UL

// LEDs (LQFP48 PT) : bleue = GPIO12 (broche 47), rouge = GPIO33 (broche 36)
// Point ouvert §9 du prompt : polarite a confirmer au premier test.
// Observation bring-up (carte 2) : GPIO33 haut = LED rouge visuellement allumee
// -> cablage actif-haut, pas actif-bas comme suppose initialement.
#define LED_ACTIVE_LOW      0

// Valeur de champ AIOMUX1 pour basculer une broche AIOx en mode analogique
#define GPIO_ANALOG_MODE    2

// =====================================================================
// Constantes de calibration des chaines de mesure.
// Source : docs/mesure-cartepuissance.md (mesures reelles sur la carte
// elevateur). Aucune de ces valeurs ne doit apparaitre ailleurs.
// =====================================================================

// ---- Chaines de tension (doc §1) ------------------------------------
// Gain = Vreel / Vadc, soit l'inverse du rapport du pont diviseur.
// VIN et V1 tombent sur les valeurs theoriques ; VOUT s'en ecarte de
// 1,8 % (tolerance des resistances), sans consequence : la pleine
// echelle reelle (600 V) reste au-dessus des 400 V vises.
#define MEAS_VIN_GAIN_V_PER_V    11.11f   // broche 10, coef mesure 0,09
#define MEAS_V1_GAIN_V_PER_V     31.25f   // broche 16, coef mesure 0,032
#define MEAS_VOUT_GAIN_V_PER_V   181.82f  // broche 14, coef mesure 0,0055

// ---- Courant d'entree (doc §2) --------------------------------------
// Un seul point mesure (250 mA -> 0,208 V) : la caracteristique est
// SUPPOSEE passer par zero, offset non verifie. Un second point reste
// a faire.
#define MEAS_IIN_A_PER_V         1.2019f

// ---- Courant de sortie (doc §4) -------------------------------------
// NON MESURE : valeur theorique de conception (3,0 V @ 50 mA).
// A remplacer des qu'une mesure reelle sera disponible.
#define MEAS_IOUT_A_PER_V        0.016667f

// ---- Shunts MOSFET 0,02 ohm (doc §3) --------------------------------
// PROVISOIRE : mesures faites avec les MCP6001 de banc. Le gain, fixe
// par le reseau RF/RG, doit rester valable apres passage au TLV9151 ;
// l'OFFSET en revanche est domine par le Vos de l'ampli et devra etre
// ENTIEREMENT REMESURE. Les deux voies different d'environ 1 % :
// garder deux jeux de constantes, ne jamais moyenner.
#define MEAS_I1_OFFSET_V         0.0473f
#define MEAS_I1_GAIN_V_PER_A     0.631f
#define MEAS_I2_OFFSET_V         0.0340f
#define MEAS_I2_GAIN_V_PER_A     0.637f

// ---- NTC B57451V5103J062 (PROMPT §5) --------------------------------
// Montage : 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
// La NTC etant du cote 3,3 V, la tension MONTE avec la temperature :
//     R_ntc = R_fixe * (VREF - Vadc) / Vadc
// (le PROMPT §5 donne cette relation inversee ; verifie sur ses propres
// points de repere : 0,75 V a 0 C -> 34,2 kOhm, ce que beta=4000 predit,
// alors que la forme inversee donnerait 2,94 kOhm, donc du chaud.)
// R_fixe suppose a 10 kOhm : reste a confirmer sur la carte (doc §6).
#define MEAS_NTC_R_FIXED_OHM     10000.0f
#define MEAS_NTC_R25_OHM         10000.0f
#define MEAS_NTC_BETA_K          4000.0f
#define MEAS_NTC_T25_K           298.15f
#define MEAS_KELVIN_OFFSET       273.15f

// ---- Seuil de protection rapide (doc §5) ----------------------------
// Le seuil est defini en AMPERES ; le code DAC est calcule par macro a
// partir du gain et de l'offset mesures, jamais ecrit en dur. Ainsi le
// futur passage au TLV9151 ne touchera que les constantes ci-dessus.
//
// Formule TI : V = DACVAL * (VDDA - VSSA) / 1023  -> 1023, pas 4096.
// Seuil nominal (PROMPT §5). Le declenchement a ete valide au banc a 1,5 A
// (courant injectable), puis le seuil remis a sa valeur de service.
#define SAFETY_ISHUNT_THRESHOLD_A   3.0f
#define SAFETY_DAC_VREF_V           3.3f

#define SAFETY_DAC_CODE_FROM_V(v_) \
    ((uint16_t)((v_) / SAFETY_DAC_VREF_V * 1023.0f + 0.5f))

// Seuil ramene a la sortie de l'ampli : Vadc = offset + I * gain.
#define SAFETY_DAC_CODE_STAGE1                                            \
    SAFETY_DAC_CODE_FROM_V(MEAS_I1_OFFSET_V                               \
                           + SAFETY_ISHUNT_THRESHOLD_A * MEAS_I1_GAIN_V_PER_A)
#define SAFETY_DAC_CODE_STAGE2                                            \
    SAFETY_DAC_CODE_FROM_V(MEAS_I2_OFFSET_V                               \
                           + SAFETY_ISHUNT_THRESHOLD_A * MEAS_I2_GAIN_V_PER_A)

// ---- Protection thermique (logicielle, PROMPT §6 etape 4) -----------
// Contrairement a la surintensite, la thermique est lente : le logiciel
// suffit, aucun chemin materiel n'est requis.
// PROVISOIRE : seuil a confirmer selon la tenue reelle du MOSFET et
// l'implantation des NTC sur la carte.
// L'hysteresis evite que l'etat oscille autour du point de bascule.
#define SAFETY_OVERTEMP_C        80.0f
#define SAFETY_OVERTEMP_HYST_C   10.0f

// ---- Timeout de la liaison ESP32 (PROMPT §6 etape 7) ----------------
// Sans trame $C valide au-dela de ce delai, la liaison est declaree
// perdue. La securite ne depend jamais de l'ESP32.
#define UART_LINK_TIMEOUT_MS     2000U

// ADC : reference interne obligatoire (VREFHI partage avec ADCINA0/VIN),
// pleine echelle 3,3 V. Conversion brut -> volts : V = raw * 3.3 / 4096.
#define ADC_VREF_V          3.3f

// Fenetre d'echantillonnage : ACQPS + 1 cycles ADCCLK (30 MHz ici).
// 25 -> 26 cycles ~ 0,87 us, confortable meme avec une source de quelques
// kilo-ohms (ponts diviseurs, pull-down de test).
#define ADC_ACQPS_CYCLES    25

// Seuil du test de bring-up ADC : LED bleue si une voie passe SOUS cette
// valeur, rouge sinon. 300 mV = 372 LSB en pleine echelle 3,3 V.
#define ADC_TEST_THRESHOLD_V   0.3f

// ---- Balayage de caracterisation (BRING-UP, boucle ouverte) ---------
// Rampe montante puis descendante entre ces deux bornes, par pas de 1 LSB
// de CMPA a chaque conversion ADC. Sert a mesurer la vitesse de correction
// atteignable, pas a reguler. A supprimer avec le reste du harnais.
#define CONTROL_SWEEP_MIN_PCT    1.0f
#define CONTROL_SWEEP_MAX_PCT   50.0f

// Frequences de decoupage par etage (point ouvert §9.1 du PROMPT, tranche au
// bring-up). TBCLK = SYSCLKOUT = 60 MHz, TBPRD = SYSCLKOUT/Fpwm - 1 :
//   etage 1 : 200 kHz -> TBPRD = 299
//   etage 2 : 100 kHz -> TBPRD = 599
#define PWM_STAGE1_FREQ_HZ  200000UL
#define PWM_STAGE2_FREQ_HZ  100000UL

// Liaison UART SCI-A (voir docs/ESP32-UART.md) : 57600 8N1.
// LSPCLK = SYSCLKOUT/4 (LOSPCP laisse a sa valeur par defaut par InitSysCtrl).
#define UART_BAUD_RATE      57600UL
#define UART_LSPCLK_HZ      (CLK_SYSCLKOUT_HZ / 4UL)
// BRR = round(LSPCLK / (8 * baud)) - 1 (TRM SPRUI09A, registre SCIHBAUD:SCILBAUD)
#define UART_SCIBRR \
    ((uint16_t)(((UART_LSPCLK_HZ + 4UL * UART_BAUD_RATE) / (8UL * UART_BAUD_RATE)) - 1U))

#define UART_RX_RING_SIZE   64U
#define UART_LINE_MAX       200U   // limite cote ESP32 (g_lineBuf[200])

#endif
