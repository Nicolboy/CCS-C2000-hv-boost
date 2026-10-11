# Prompt pour Claude Code — Firmware TMS320F28027 « dual boost »

> Version 2 — brochage corrigé, constantes de calibration réelles.
> À coller dans Claude Code (CCS v21) après avoir ouvert le dépôt
> `TMS320F28027-dualboost`.

---

## 1. Contexte

Firmware d'une alimentation haute tension à deux étages boost en cascade,
pour préamplis à tubes (charge ~10-15 mA, modulée à fréquence audio).

- **MCU** : TMS320F28027, LQFP48 (PT), carte sur mesure
- **IDE** : Code Composer Studio v21 (Theia), compilateur C2000
- **Bibliothèque** : C2000Ware, headers **bitfield** (`DSP28x_Project.h`,
  `F2802x_Device.h`) — **pas driverlib**
- **Sonde** : Olimex TMS320-XDS100-V3
- **Horloge** : **oscillateur interne INTOSC1 (10 MHz)**, PLL → SYSCLKOUT = 60 MHz
- **Dépôt** : `git@git.syoul.fr:alim-High-Volt-boost/TMS320F28027-dualboost.git`

Un ESP32-C3 (dépôt `ESP32-C3-dualboost`, déjà fonctionnel : OLED SSD1322
256x64 + serveur web) gère l'IHM. Protocole dans `docs/ESP32-UART.md` —
**source de vérité, ne rien inventer**.

Puissance par étage : MOSFET IPD60R360 (600 V), diode SiC STPSC406 (600 V/4 A),
inductance 47-100 µH. Sortie HT commutée par MOSFET N piloté par optocoupleur
photovoltaïque VOM1271 (~265-325 µs — coupure de sécurité lente, **pas** une
protection rapide).

---

## 2. Brochage définitif (LQFP48 PT)

### Entrées analogiques

| Signal | Broche | Fonction MCU | Rôle | Échelle |
|---|---|---|---|---|
| `V-Batt-adc` | 10 | ADCINA0 / **VREFHI** | VIN | 11,11 V/V (PE 36,7 V) |
| `I-Batt-adc` | 8 | ADCINA1 | IIN | 1,125 A/V (PE 3,71 A) |
| `I-shunt1-adc` | **9** | ADCINA2 / **COMP1A** / AIO2 | I1 + **protection étage 1** | 0,02 Ω × 30 → 1,667 A/V (PE 5,5 A) |
| `I-HV-adc` | 7 | ADCINA3 | IOUT | 16,67 mA/V (PE 55 mA) |
| `I-shunt2-adc` | **5** | ADCINA4 / **COMP2A** / AIO4 | I2 + **protection étage 2** | idem |
| `V-HV-adc` | **14** | ADCINB2 / AIO10 | VOUT | 178,6 V/V (PE 589 V) |
| `V-inter-adc` | **16** | ADCINB4 / AIO12 | V1 | 31,25 V/V (PE 103 V, V1max = 50 V → 1,6 V) |
| `Temp1` | 17 | ADCINB6 / AIO14 | NTC étage 1 | voir §5 |
| `Temp2` | 18 | ADCINB7 | NTC étage 2 | voir §5 |
| *libre* | 13 | ADCINB1 | — | |
| *libre* | 15 | ADCINB3 | — | |

**Points d'attention obligatoires :**

1. Les broches 5, 9, 14, 16, 17 sont des **AIO** (AIO4, AIO2, AIO10, AIO12,
   AIO14) et sont en **mode numérique par défaut au reset**. Sans écriture
   explicite dans `AIOMUX1` (valeur du champ = **2**), ni l'ADC ni les
   comparateurs ne voient le signal. **À faire en tout début d'init.**
2. **Les shunts sont obligatoirement sur les entrées `COMPxA` (pins 9 et 5).**
   Contrainte de silicium : `COMPxA` est câblée en dur sur l'entrée
   non-inverseuse (+) du comparateur ; `COMPxB` n'existe que comme
   alternative au DAC sur l'entrée inverseuse (−). Il n'y a aucun mux
   permettant de les échanger. Seules les entrées A sont donc utilisées,
   avec `COMPSOURCE = 0` (référence = DAC interne).
3. Les broches 9 et 5 servent **simultanément** d'entrée ADC et d'entrée
   comparateur — c'est la même broche physique, le signal alimente les deux
   chemins en parallèle. Pas de conflit, `I1`/`I2` restent disponibles en
   télémétrie.
4. `VREFHI` est partagé avec `ADCINA0`, utilisé pour VIN → **référence ADC
   interne obligatoire** (`ADCCTL1.bit.ADCREFSEL = 0`, pleine échelle 3,3 V).
   Conséquences documentées : erreur de gain ±60 LSB, tempco −50 ppm/°C.
   Le signal sur ADCINA0 ne doit jamais dépasser VDDA.
5. `V-inter` : pleine échelle 103 V pour une tension intermédiaire maximale
   de **50 V** → ~48 % de la plage ADC utilisée, ~2000 codes sur la plage
   utile. Pas de saturation, diviseur conservé tel quel.

### Répartition des tensions

- **Étage 1** : 10 V → **50 V max** (D ≈ 0,8)
- **Étage 2** : 50 V → **400 V max** (D ≈ 0,875), consigne programmable
  200 / 250 / … / 400 V

### Entrées / sorties numériques

| Signal | Broche | Fonction MCU | Sens |
|---|---|---|---|
| `UART-RX` | 48 | GPIO28 / SCIRXDA | Entrée (← ESP32 GPIO7) |
| `UART-TX` | 1 | GPIO29 / SCITXDA | Sortie (→ ESP32 GPIO6) |
| `Stage1-PWM` | 29 | GPIO0 / **EPWM1A** | Sortie PWM étage 1 (HRPWM possible) |
| `Stage1-default` | 28 | GPIO1 / **COMP1OUT** | Sortie comparateur 1 |
| `Stage2-PWM` | 37 | GPIO2 / **EPWM2A** | Sortie PWM étage 2 (HRPWM possible) |
| `Stage2-default` | 38 | GPIO3 / **COMP2OUT** | Sortie comparateur 2 |
| `Stage1-EN` | 27 | GPIO16 | Sortie — enable logiciel étage 1 |
| `Stage2-EN` | 26 | GPIO17 | Sortie — enable logiciel étage 2 |
| `HV_EN` | 31 | GPIO32 | Sortie — commande sortie HT (VOM1271) |
| LED bleue | 47 | GPIO12 | Sortie |
| LED rouge | 35 | GPIO33 | Sortie |
| JTAG | 20-23 | TDI/TMS/TDO/TCK | **Ne pas reconfigurer en GPIO** |

Les LEDs sont câblées avec une résistance de 1 kΩ vers le 3,3 V → **logique
probablement inversée** (LED allumée quand le GPIO est bas) : à vérifier au
premier test et à encapsuler dans `led_set()` pour ne pas polluer le reste
du code.

**Aucune broche TZ externe n'est disponible** (GPIO12 = TZ1 → LED, GPIO16/17
= TZ2/TZ3 → Stage-EN). Tous les trips passent par le Digital Compare interne
depuis `COMPxOUT`. Conséquence assumée : pas de possibilité d'ajouter un trip
externe (arrêt d'urgence, thermostat matériel) sans reprendre le routage.

### Logique de sécurité câblée (hors MCU)

Deux portes ET 74HVC1G08 (IC8, IC9), une par étage :

```
COMPxOUT (Stagex-default, CMPINV=1 : 1=OK, 0=défaut) ──┐
                                                        ├─ AND ─→ PWMx-EN (driver)
GPIO Stagex-EN (logiciel : 1=autorisé, 0=inhibé)     ──┘
```

Les drivers sont **actifs à l'état haut**, d'où `CMPINV = 1` sur les deux
comparateurs. Le logiciel peut inhiber en plus, **jamais** outrepasser la
protection matérielle.

---

## 3. Horloge — contraintes liées à l'oscillateur interne

Pas de quartz : INTOSC1 (10 MHz nominal). PLL pour 60 MHz :
`PLLCR = 12`, `DIVSEL = /2` → 10 × 12 / 2 = 60 MHz.

**Obligatoire** : quand la source est l'oscillateur interne, `PLLLOCKPRD`
doit être écrit avec **10000 minimum** (sinon le verrouillage PLL est mal
temporisé).

**Dérive à documenter et à gérer** : 3,03 à 4,85 kHz/°C, plage 9,6-10,6 MHz
sur −40/+125 °C, soit ~2 % sur 40 °C d'échauffement. Impacts :

- **UART fixé à 57600 bauds** (au lieu de 115200) pour élargir la marge de
  tolérance sur le timing des trames. Côté ESP32, `Serial1.begin(57600, ...)`
  doit être mis à jour en conséquence.
- Fréquence de découpage réelle à ±2 % → `FREQ1`/`FREQ2` en télémétrie sont
  indicatifs, à annoter comme tels.
- Prévoir un point d'accroche (fonction vide + commentaire) pour ajouter plus
  tard la compensation d'oscillateur via le capteur de température interne
  (voir *Oscillator Compensation Guide* + code C2000Ware).

---

## 4. Architecture logicielle demandée

```
src/
  main.c            boucle principale, ordonnancement
  bsp_clock.c/h     InitSysCtrl, INTOSC1 + PLL 60 MHz, PLLLOCKPRD, watchdog
  bsp_gpio.c/h      mux GPIO, AIOMUX1, LEDs, HV_EN, StageX-EN
  safety.c/h        comparateurs + DAC, Digital Compare, Trip Zone, EMUSTOP
  pwm.c/h           init ePWM1/ePWM2, duty, fréquence (HRPWM plus tard)
  adc.c/h           init ADC (réf interne), SOC déclenché par ePWM, lecture brute
  measure.c/h       conversion brut -> unités physiques (V, A, °C)
  control.c/h       régulation — SQUELETTE UNIQUEMENT pour l'instant
  uart_link.c/h     SCI-A 57600 8N1, trames $T,...*XX et $C,...*XX
  calib.h           TOUTES les constantes matérielles, rien ailleurs
  protocol.h        tags, structures Telemetry / CommandState
docs/
  ESP32-UART.md     protocole (copié depuis le dépôt ESP32)
```

---

## 5. Constantes de calibration (`calib.h`)

Aucune de ces valeurs ne doit apparaître ailleurs que dans ce header.

### Tensions et courants

| Grandeur | Relation | Coefficient |
|---|---|---|
| VIN | 1,8 V @ 20 V | `VIN = Vadc × 11,111` |
| IIN | 2,4 V @ 2,7 A | `IIN = Vadc × 1,125` |
| V1 (inter) | 3,2 V @ 100 V | `V1 = Vadc × 31,25` |
| VOUT | 2,8 V @ 500 V | `VOUT = Vadc × 178,571` |
| IOUT | 3,0 V @ 50 mA | `IOUT = Vadc × 0,016667` (A) |
| I1, I2 | shunt 0,02 Ω, gain ×30 | `I = Vadc / 0,6 = Vadc × 1,6667` |

Conversion brut → volts : `Vadc = raw × 3.3f / 4096.0f` (référence interne).

### Seuil de protection (identique sur les deux étages)

- Seuil : **3 A** → `Vadc = 3 × 0,02 × 30 = 1,8 V`
- Code DAC 10 bits : `round(1.8 / 3.3 × 1023)` = **558**
- Marges connues : gain DAC −1,5 %, offset DAC 10 mV, offset comparateur
  ±5 mV, hystérésis 35 mV (≈ 58 mA ramenés au courant) → précision réelle du
  seuil de l'ordre de ±2 %, largement suffisant face à la marge des composants.
- Temps de réponse comparateur → Trip Zone : 30 ns (asynchrone).
- Définir le seuil en **ampères** dans `calib.h`, et calculer le code DAC par
  macro — ne jamais écrire 558 en dur.

### NTC (B57451V5103J062, boîtier 0805)

Montage : `3,3 V — NTC 10 k — R 10 k — 0 V`, mesure au point milieu.
R25 = 10 kΩ, B25/100 = 4000 K ±3 %.

```
R_ntc = R_fixe × Vadc / (3.3 - Vadc)
T(K)  = 1 / ( 1/298.15 + ln(R_ntc / 10000) / 4000 )
```

Points de repère attendus : 0,75 V à 0 °C · 1,65 V à 25 °C · 2,94 V à 80 °C ·
3,09 V à 100 °C. La tension **monte** avec la température. Sensibilité vers
100 °C : ~7,7 mV/°C, soit ~10 LSB/°C — suffisant pour de la surveillance.
La tolérance ±3 % sur B donne ~±2 °C d'erreur vers 100 °C sans calibration
individuelle : acceptable pour de la protection thermique, pas pour de la
mesure de précision. À documenter.

Résistance fixe confirmée à **10 kΩ** (pont 10 k NTC / 10 k).

---

## 6. Étapes d'implémentation (un commit par étape)

### Étape 1 — Squelette + « hello LED »

- Projet CCS F28027, fichier `.cmd` Flash
- `InitSysCtrl()` adapté : INTOSC1, `PLLLOCKPRD = 10000`, PLLCR 12, DIVSEL /2
- Watchdog désactivé au début (à réactiver plus tard)
- Clignotement LED pour valider compilation + flash + XDS100v3

**Critère** : LED à 1 Hz, compilation sans warning, SYSCLKOUT vérifié à 60 MHz
(sortie XCLKOUT ou mesure d'un timer).

### Étape 2 — Sécurité (AVANT toute génération PWM)

Aucun PWM ne doit sortir tant que les protections ne sont pas armées **et
testées**.

1. `AIOMUX1` : basculer AIO2, AIO4, AIO10, AIO12, AIO14 en mode analogique
   (valeur du champ = **2**).
2. **Horloges de module** : `PCLKCR3.bit.COMP1ENCLK = 1` et `COMP2ENCLK = 1`,
   sinon `COMPSTS` ne se met jamais à jour.
3. **Comparateurs COMP1 / COMP2** (shunt sur l'entrée A, DAC en référence) :
   - `COMPCTL.bit.COMPDACEN = 1` — **sans ça le bloc reste éteint**
   - `COMPCTL.bit.COMPSOURCE = 0` (entrée inverseuse = DAC interne)
   - `DACVAL.bit.DACVAL` = code du seuil 3 A. Formule TI :
     `V = DACVAL × (VDDA − VSSA) / 1023` → **1023, pas 4096**.
     Calculé par macro depuis `SAFETY_ISHUNT_THRESHOLD_A`, jamais en dur.
   - `COMPCTL.bit.CMPINV = 1` (driver actif haut : sortie 1 = OK, 0 = défaut)
   - `COMPCTL.bit.SYNCSEL = 0` (**asynchrone**) — c'est ce qui donne les 30 ns
     de temps de réponse vers le Trip Zone. `QUALSEL` n'a d'effet qu'en mode
     synchrone, qui ajoute de la latence : n'activer la qualification qu'après
     avoir observé le bruit réel au scope, jamais par précaution a priori.
   - Hystérésis d'entrée activée par défaut (~100 kΩ de contre-réaction,
     35 mV) : compatible avec la sortie basse impédance de l'ampli.
     `COMPHYSTCTL` (module ADC) permet de la désactiver si besoin.
   - Sorties routées : GPIO1 (COMP1OUT, mux 3) et GPIO3 (COMP2OUT, mux 3)
4. **Trip Zone via Digital Compare**
   - `DCTRIPSEL.bit.DCAHCOMPSEL` = COMP1OUT (code `1000b`) pour ePWM1,
     COMP2OUT (`1001b`) pour ePWM2
   - `TZDCSEL.bit.DCAEVT1` : configurer l'événement sur DCAH bas
   - `TZSEL.bit.DCAEVT1 = 1` → **one-shot** (latché), pas cycle-by-cycle
   - `TZCTL.bit.TZA = TZ_FORCE_LO`
5. **TZ6 = EMUSTOP** sur ePWM1 et ePWM2 :
   ```c
   EPwm1Regs.TZSEL.bit.OSHT6 = 1;
   EPwm1Regs.TZCTL.bit.TZA   = TZ_FORCE_LO;
   ```
   Le signal `EMUSTOP` du CPU **existe bien** et est câblé en dur sur TZ6 sur
   cette famille (TRM SPRUI09A §3.2.7) ; il n'y a simplement pas de macro
   nommée `EMUSTOP` dans les headers, on écrit directement `OSHT6`.
   Indispensable : sans ça, un breakpoint posé pendant que le MOSFET conduit
   laisse l'inductance se charger sans limite.
6. **`TBCTL.bit.FREE_SOFT = 0`** sur les deux ePWM, **en plus et non à la
   place** du point 5. Attention : `FREE_SOFT` gèle seulement le compteur de
   base de temps — l'Action Qualifier a déjà positionné la sortie, qui reste
   figée dans son dernier état. Si le CPU s'arrête pendant le temps de
   conduction, **la broche reste haute**. Seul TZ6 + `TZ_FORCE_LO` garantit
   la mise à l'état bas.
7. **`EPWMx_TZINT`** : diagnostic logiciel uniquement (log, LED rouge,
   notification ESP32). La coupure est déjà faite en matériel.
8. API : `safety_get_fault_flags()`, `safety_clear_faults()` (écriture
   `TZCLR[OST]`, **jamais appelée automatiquement**).
9. **Test** : `TZFRC[OSHT]` pour forcer un trip logiciel, sans provoquer de
   vrai court-circuit.

**Critère** : `TZFRC` force les deux sorties à 0 et lève le flag ; un
breakpoint coupe le PWM ; le flag ne se réarme pas seul.

### Étape 3 — GPIO de commande, état sûr au reset

- `HV_EN`, `Stage1-EN`, `Stage2-EN` à 0 **avant toute autre configuration**
- ⚠️ **L'état au reset n'est PAS fail-safe** : GPIO1 et GPIO3 (COMPxOUT) sont
  des broches à fonction PWM, dont **les pull-ups ne sont pas activées au
  reset** → elles flottent. GPIO16/17 (Stage-EN) ne sont pas des broches PWM
  → **pull-ups activées** → état haut. Résultat : à la mise sous tension, une
  entrée de chaque porte ET flotte et l'autre est à 1 → **sortie
  indéterminée, le driver peut être validé avant que le firmware ne tourne**.
  - Correctif **matériel obligatoire** : pull-down 10 kΩ sur les deux entrées
    de chaque porte ET, et sur les lignes PWM vers les drivers. Le firmware
    ne peut rien pour la fenêtre pré-boot.
  - Correctif logiciel complémentaire : désactiver les pull-ups internes de
    GPIO16/17 via `GPAPUD` dès l'init.
- LED rouge = défaut latché, LED bleue = fonctionnement nominal
- Encapsuler la polarité des LED dans `led_set()`

### Étape 4 — ADC

- Référence **interne** (`ADCREFSEL = 0`), `Device_cal()` appelé
- 9 canaux (§2), **SOC déclenché par ePWM**, jamais en free-run :
  échantillonner à un instant stable, loin des fronts de commutation
- ISR `ADCINT1` courte : stockage des valeurs brutes, rien de plus
- Moyenne glissante côté `measure.c` pour la télémétrie. La protection rapide
  ne passe **pas** par l'ADC.

### Étape 5 — Conversion physique (`measure.c`)

Implémenter les relations du §5. Signature du type
`float measure_vin(uint16_t raw)`, unités documentées dans les prototypes.

### Étape 6 — PWM

- **ePWM1** → étage 1 (EPWM1A, GPIO0) · **ePWM2** → étage 2 (EPWM2A, GPIO2)
- Mode up-count, TBCLK = SYSCLKOUT = 60 MHz
- `TBPRD = 60e6 / Fpwm - 1` (599 à 100 kHz, 299 à 200 kHz)
- **Fréquences par étage : À CONFIRMER**
- **Duty de départ = 0 %**, puis rampe logicielle montante (soft-start),
  indépendante de la régulation. Ne jamais démarrer au duty nominal : le
  condensateur de sortie vide provoquerait un appel de courant destructeur.

> **Important — un boost à 0 % de duty ne donne pas 0 V en sortie.** Le chemin
> `Vin → L → diode → Cout` reste passant en permanence. Avec les deux étages à
> 0 %, la sortie HT est déjà à ~10 V ; si l'étage 1 régule à 50 V pendant que
> l'étage 2 est à 0 %, la sortie est à **50 V**. Le seul organe qui isole
> réellement la charge est `HV_EN` (VOM1271). Conséquence pour le firmware :
> toute séquence d'arrêt, de mise en sécurité ou de timeout UART doit couper
> `HV_EN` **en plus** d'inhiber les PWM, jamais seulement les PWM.
- API : `pwm_set_duty(stage, float duty_0_1)`, `pwm_set_freq(stage, hz)`,
  `pwm_enable(stage, bool)`
- **HRPWM : ne pas l'implémenter maintenant**, mais structurer `pwm.c` pour
  l'accueillir. Notes à consigner en commentaire :
  - disponible uniquement sur les sorties **EPWMxA** → les deux étages sont
    compatibles tels que routés
  - SYSCLKOUT ≥ 50 MHz requis (OK à 60), MEP 150-310 ps, SFO obligatoire
  - **limitation de 3 cycles SYSCLK** sur le rapport cyclique : à 200 kHz,
    duty min 0,67 % et max 99 % — à confronter aux points de fonctionnement
  - à 100 kHz sans HRPWM : 600 pas ≈ 9,2 bits, risque de limit cycling en
    boucle fermée

### Étape 7 — Liaison UART

Conforme à `docs/ESP32-UART.md`, avec **57600 bauds 8N1** (et non 115200 —
mettre à jour le document et le firmware ESP32).

- SCI-A, GPIO28 (RX) / GPIO29 (TX)
- Émission `$T,...*XX` toutes les **200-500 ms** (l'ESP32 déclare la liaison
  perdue au-delà de 2 s)
- Réception `$C,HT=x,PWM1=x,PWM2=x*XX` toutes les 500 ms
- Checksum **XOR** entre `$` et `*`, format `%02X` **majuscules** (sinon rejet
  silencieux côté ESP32), terminaison `\n`, ligne ≤ 160 caractères
- Réception par ISR + buffer circulaire, **parsing dans la boucle principale**
- Trame au checksum invalide : ignorée silencieusement
- **Timeout côté TMS320** : sans trame `$C` valide depuis > 2 s → retour en
  état sûr (PWM inhibés, HT coupée). La sécurité ne dépend jamais de l'ESP32.
- Vérifier le support `%f` du compilateur C28x dans `sprintf` ; si coût trop
  élevé, formater les flottants manuellement (entier + décimales)

Champs : `FREQ1 FREQ2 DUTY1 DUTY2 VIN IIN V1 I1 T1 VOUT I2 T2 IOUT`

### Étape 8 — Régulation : squelette seulement

**Aucune régulation active à ce stade.** Le PWM reste à sa valeur de départ.

```c
typedef struct {
    float setpoint;   // consigne (V)
    float measured;
    float duty;       // 0..1
    float duty_min, duty_max;
    bool  enabled;
} control_stage_t;

void  control_init(void);
void  control_set_setpoint(int stage, float volts);
float control_update(int stage, float measured, float dt); // renvoie duty inchangé
```

Prévoir l'emplacement d'appel cadencé (`dt` constant) dans l'ISR ADC ou PWM.
Documenter en commentaire : le boost à fort gain présente un **zéro dans le
demi-plan droit** qui limite la bande passante atteignable ; la charge
(~10-15 mA, modulée à fréquence audio) impose surtout un bon **PSRR en bande
audio** ; une structure ADRC ou un PI cascadé sera évalué ensuite.

---

## 7. Règles

- **Headers bitfield uniquement**, pas de driverlib, pas de mélange sur un
  même périphérique
- `EALLOW`/`EDIS` autour de tous les registres protégés (TZSEL, TZCTL, TZEINT,
  TZCLR, TZFRC, HRCNFG, GPxMUX, AIOMUX1, INTOSCnTRIM, table des vecteurs PIE…)
- Aucune valeur magique : tout dans `calib.h`, commenté avec son origine
- ISR courtes : pas de `sprintf`, pas de flottant lourd, pas d'attente
- Ordre d'init strict : **GPIO en état sûr → AIOMUX1 → horloge → sécurité →
  ADC → UART → PWM en dernier**
- Commits atomiques, un par étape

## 8. Interdits

- Activer le PWM avant que l'étape 2 soit testée
- Effacer automatiquement un flag de défaut latché
- Faire dépendre une protection du CPU, de l'ADC ou de l'UART
- Implémenter la régulation en boucle fermée maintenant
- Toucher aux broches JTAG (20-23)
- Inventer le protocole UART

## 9. Points encore ouverts

1. Fréquence de découpage par étage (100 ou 200 kHz)
2. Polarité réelle des LED (à confirmer au premier test)
3. Valeur d'inductance retenue par étage (47 ou 100 µH) — influe sur le mode
   CCM/DCM et sur la vitesse de montée d'un courant de défaut

## 10. Première action attendue

Avant d'écrire du code : lire le dépôt, lister l'existant, **poser les
questions sur les points du §9**, puis proposer un plan et attendre validation
avant de commencer l'étape 1.
