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
// Amplis TLV9151 montes sur la carte (remplacent les MCP6001 de banc).
//
// OFFSETS REMESURES a courant nul, carte alimentee au repos : la tension
// lue EST l'offset, aucune injection n'est necessaire. Deux releves ont
// donne 4 puis 0 count sur I1, et 0 count sur I2 -- soit AU PLUS 3,2 mV,
// donc sous la resolution de l'ADC (0,8 mV/LSB). Contre 47,3 et 34,0 mV
// avec les MCP6001 : le Vos d'entree est passe sous 0,1 mV une fois
// ramene par le gain de 31,5.
//
// Retenir zero est le choix JUSTE et le choix SUR : c'est la meilleure
// estimation, et pour le seuil de protection ci-dessous un offset
// sous-estime abaisse le code DAC, donc fait declencher un peu plus tot.
//
// Limite a connaitre : l'ampli est en alimentation simple, sa sortie ne
// peut pas descendre sous 0 V. Un Vos negatif serait donc ecrete et
// indiscernable de zero -- et les tres faibles courants sont perdus dans
// ce plancher. Sans consequence pour la protection (seuil a 1,9 V), mais
// la mesure de courant n'est pas exploitable pres de zero.
//
// Le GAIN est fixe par le reseau RF/RG et n'a pas ete retouche : il reste
// donc la seule partie non reverifiee de cette chaine. Les deux voies
// different d'environ 1 %, garder deux jeux de constantes, ne jamais
// moyenner.
#define MEAS_I1_OFFSET_V         0.0f
#define MEAS_I1_GAIN_V_PER_A     0.631f
#define MEAS_I2_OFFSET_V         0.0f
#define MEAS_I2_GAIN_V_PER_A     0.637f

// ---- NTC B57451V5103J062 (PROMPT §5) --------------------------------
// Montage : 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
// La NTC etant du cote 3,3 V, la tension MONTE avec la temperature :
//     R_ntc = R_fixe * (VREF - Vadc) / Vadc
// (le PROMPT §5 donne cette relation inversee ; verifie sur ses propres
// points de repere : 0,75 V a 0 C -> 34,2 kOhm, ce que beta=4000 predit,
// alors que la forme inversee donnerait 2,94 kOhm, donc du chaud.)
//
// SENS VALIDE sur les vraies thermistances montees : rechauffer une NTC a
// la main fait MONTER la valeur affichee. Le point restait indecidable
// avec les 10 k fixes de test, qui donnent 25 C que la formule soit droite
// ou inversee. C'etait un vrai trou de securite : inversee, une surchauffe
// se serait lue comme un refroidissement et la coupure a 80 C n'aurait
// jamais declenche.
// Releve de coherence a l'ambiante : 2219 et 2208 counts -> 28,8 et
// 28,5 C sur une carte alimentee, thermistances a 5 mm des MOSFET.
//
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

// =====================================================================
// Regulation et bornes d'exploitation
// =====================================================================

// ---- Bornes des consignes acceptees ---------------------------------
// Une consigne hors de ces bornes est REFUSEE : l'ancienne est conservee
// et le rejet est signale en telemetrie. On ne sature pas silencieusement,
// sinon une erreur de commande passerait inapercue.
#define CTRL_V1_SET_MIN_V       15.0f
#define CTRL_V1_SET_MAX_V       50.0f
#define CTRL_VOUT_SET_MIN_V    200.0f
#define CTRL_VOUT_SET_MAX_V    500.0f

// Consigne de sortie EXACTEMENT nulle : convention signifiant "etage 2
// desactive". La machine s'arrete alors a l'etage 1 etabli, l'etage 2 reste
// a duty 0 et HV_EN est interdit. C'est le mode de validation du seul
// premier etage, quand le MOSFET, la diode et l'inductance de l'etage 2 ne
// sont pas montes -- sans lui la machine resterait bloquee en START_S2, la
// sortie ne pouvant jamais atteindre 200 V.
//
// Zero est sans ambiguite : ce n'est pas une consigne de sortie plausible,
// et toute valeur strictement comprise entre 0 et la borne basse reste
// refusee comme avant.
#define CTRL_VOSET_DISABLED      0.0f

// ---- Seuils de coupure en survoltage --------------------------------
// Verifies dans l'ISR ADC par simple comparaison sur la valeur brute.
// Restent sous les pleines echelles mesurees (103 V et 600 V), donc la
// mesure ne sature jamais avant que la protection n'agisse.
#define CTRL_V1_OV_TRIP_V        55.0f
#define CTRL_VOUT_OV_TRIP_V     520.0f

// ---- Limites de rapport cyclique ------------------------------------
// duty max < 1 imperativement : a 500 V depuis 35 V il faut deja D = 0,93,
// la marge est donc mince. duty min a 0 : un boost a 0 % laisse malgre tout
// passer Vin par L et la diode, ce n'est pas une coupure.
#define CTRL_DUTY_MIN            0.0f
#define CTRL_DUTY_MAX            0.95f

// ---- Loi de commande : integrateur pur, virgule fixe -----------------
// duty_counts = accumulateur >> CTRL_SHIFT, l'accumulateur recevant
// l'erreur brute a chaque pas. Aucune multiplication ni division, donc
// utilisable en ISR (regle : que des comparaisons, additions, decalages).
//
// Le gain integral vaut 1 / 2^CTRL_SHIFT par pas de regulation. Volontaire-
// ment tres faible pour demarrer : le boost a fort gain presente un zero
// dans le demi-plan droit qui limite la bande passante atteignable, et on
// n'a aucun modele du convertisseur. A augmenter par paliers apres mesure
// de la reponse reelle. Le terme proportionnel viendra ensuite.
#define CTRL_SHIFT               12U

// Decimation depuis l'ISR ADC (66,7 kHz) : une regulation toutes les
// CTRL_DECIM sequences, soit ~5,1 kHz. Donne un dt rigoureusement constant.
#define CTRL_DECIM               13U

// ---- Rampe de demarrage ----------------------------------------------
// On rampe la CONSIGNE et non le duty : la boucle reste fermee pendant
// toute la montee. Exprimee en volts par pas de regulation.
// 0,01 V/pas a 5,1 kHz -> ~51 V/s sur l'etage 1, la montee de 10 a 50 V
// prend donc environ 0,8 s.
#define CTRL_RAMP_V1_V_PER_STEP     0.01f
#define CTRL_RAMP_VOUT_V_PER_STEP   0.10f

// ---- Criteres de passage d'etat --------------------------------------
// L'etage est declare etabli quand l'ecart reste sous tolerance pendant
// cette duree, exprimee en pas de regulation (~5,1 kHz).
#define CTRL_SETTLE_TOL_V1_V        1.0f
#define CTRL_SETTLE_TOL_VOUT_V     10.0f
#define CTRL_SETTLE_STEPS         500U   // ~100 ms

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
