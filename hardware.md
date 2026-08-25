# hardware.md — Alimentation HT « dual boost » TMS320F28027

Synthèse matérielle reconstituée à partir des sources du projet
(`docs/TMS-pin-v2.txt`, `docs/PROMPT-claude-code-TMS320.md`,
`docs/mesure-cartepuissance.md`, `src/calib.h`, `src/bsp_gpio.c`).
En cas de divergence, ces fichiers font foi — celui-ci est un résumé.

> **Révision décrite : carte de puissance V0.2**, celle en service.
>
> La V0.2 a changé le brochage analogique (§3) **et** la chaîne de mesure du
> courant d'entrée (ZXCT1109 → INA293A2, §5). Tout l'étalonnage réalisé sur
> V0.1 en est invalidé : il est conservé intégralement dans
> **`docs/calibration-V0.1.md`** — constantes, points bruts, historique des
> gains et pièges de banc — à rouvrir tel quel si une carte V0.1 est remise
> en service. Les voies encore porteuses de valeurs V0.1 sont marquées comme
> telles dans le tableau du §6 et dans `src/calib.h`.

---

## 1. Vue d'ensemble

Alimentation haute tension pour préamplis à tubes, charge ~10-15 mA modulée
à fréquence audio. Deux étages boost **en cascade** :

| Étage | Entrée | Sortie | Rapport cyclique | Découpage |
|---|---|---|---|---|
| 1 | 10 V (batterie) | 15 → 50 V (`V_inter`) | ≈ 0,8 max | **200 kHz** |
| 2 | `V_inter` | 200 → 500 V (`V_HT`) | ≈ 0,93 à 500 V | **100 kHz** |

Deux cartes distinctes, sur alimentations séparées :

- **carte CPU** — TMS320F28027 (LQFP48 PT), fonctionnelle, JTAG réglé ;
- **carte puissance** — montage **en cours**, étage 2 non monté.

Supervision par un **ESP32-C3** (OLED SSD1322 256×64 + serveur web), relié
en UART 57600 8N1. L'ESP32 n'a **aucune autorité de sécurité** : il propose
des consignes, le TMS320 seul décide.

```
Orchestrateur (agent/script)  --HTTP-->  ESP32-C3  --UART 57600 $C/$T-->  TMS320F28027
                                                                              |
                                      comparateurs + Trip Zone (30 ns) --------+--> drivers
```

---

## 2. MCU et horloge

| Élément | Valeur |
|---|---|
| MCU | TMS320F28027, LQFP48 (PT), C28x |
| Sonde | Olimex TMS320-XDS100-V3 (projet configuré XDS100v3) |
| Oscillateur | **INTOSC1 interne 10 MHz** — pas de quartz (X1 à la masse, X2 NC) |
| PLL | `PLLCR = 12`, `DIVSEL = /2` → **SYSCLKOUT = 60 MHz** |
| Contrainte | `PLLLOCKPRD = 10000` minimum, écrit **avant** `InitSysCtrl()` |
| LSPCLK | SYSCLKOUT/4 = 15 MHz (UART) |

**Conséquence directe de l'oscillateur interne** : dérive 3,03 à
4,85 kHz/°C, soit ~2 % sur 40 °C d'échauffement. D'où :

- UART volontairement descendu à **57600 bauds** (et non 115200) pour
  élargir la marge de timing ;
- `FREQ1`/`FREQ2` remontés en télémétrie sont **indicatifs à ±2 %** ;
- broches X1/X2 (45-46) réservées si un quartz devient nécessaire.

---

## 3. Brochage LQFP48 (version V0.2 corrigée, celle du firmware)

### Entrées analogiques

| Signal | Broche | Fonction MCU | Rôle | Pleine échelle |
|---|---|---|---|---|
| `V-Batt-adc` | 10 | ADCINA0 / **VREFHI** | VIN | 36,7 V |
| `I-Batt-adc` | 8 | ADCINA1 | IIN | ~3,97 A |
| `I-shunt1-adc` | **9** | ADCINA2 / **COMP1A** / AIO2 | I1 + protection étage 1 | 4,04 A (mesurée) |
| `I-HV-adc` | 7 | ADCINA3 | IOUT | ~55 mA (théorique) |
| `I-shunt2-adc` | **5** | ADCINA4 / **COMP2A** / AIO4 | I2 + protection étage 2 | idem I1 |
| `V-HV-adc` | **14** | ADCINB2 / AIO10 | VOUT | 600 V |
| `V-inter-adc` | **16** | ADCINB4 / AIO12 | V1 | **96,6 V** |
| `Temp1` | 17 | ADCINB6 / AIO14 | NTC étage 1 | — |
| `Temp2` | 18 | ADCINB7 | NTC étage 2 | — |
| libre | 13, 15 | ADCINB1, ADCINB3 | réserve mesure future | — |

Trois contraintes de silicium à ne jamais perdre de vue :

1. **AIO2, AIO4, AIO10, AIO12, AIO14 sont numériques au reset.** Sans
   écriture de `AIOMUX1` (champ = 2), ni l'ADC ni les comparateurs ne
   voient le signal. Fait en tout début d'init (`bsp_gpio_analog_init()`).
2. **Les shunts doivent être sur `COMPxA`** (broches 9 et 5) : `COMPxA`
   est câblée en dur sur l'entrée non-inverseuse, `COMPxB` n'existe que
   comme alternative au DAC sur l'entrée inverseuse. Aucun mux ne permet
   de les échanger — c'est ce qui a imposé la révision V0.2 du brochage.
3. **`VREFHI` est partagé avec ADCINA0 (VIN)** → référence ADC **interne**
   obligatoire (`ADCREFSEL = 0`, PE 3,3 V). Erreur de gain ±60 LSB,
   tempco −50 ppm/°C. Le signal sur ADCINA0 ne doit jamais dépasser VDDA.
4. **Le F2802x n'a PAS de pull-down interne.** Le silicium ne propose que des
   pull-up (`GPAPUD`, 1 = désactivé), actifs au reset. Tout niveau bas au
   repos doit venir d'une **résistance externe** — 10 kΩ sur cette carte, par
   cohérence avec les pull-down des portes ET et des lignes PWM.

### Entrées / sorties numériques

| Signal | Broche | GPIO | Sens / rôle |
|---|---|---|---|
| `UART-TX` | 1 | GPIO29 / SCITXDA | → ESP32 GPIO6 |
| `UART-RX` | 48 | GPIO28 / SCIRXDA | ← ESP32 GPIO7 |
| `Stage1-PWM` | 29 | GPIO0 / EPWM1A | sortie PWM étage 1 (HRPWM possible) |
| `Stage2-PWM` | 37 | GPIO2 / EPWM2A | sortie PWM étage 2 (HRPWM possible) |
| `Stage1-default` | 28 | GPIO1 / COMP1OUT | sortie comparateur 1 → porte ET IC8 |
| `Stage2-default` | 38 | GPIO3 / COMP2OUT | sortie comparateur 2 → porte ET IC9 |
| `Stage1-EN` | 27 | GPIO16 | enable logiciel étage 1 → IC8 |
| `Stage2-EN` | 26 | GPIO17 | enable logiciel étage 2 → IC9 |
| `HV_EN` | 31 | GPIO32 | commande sortie HT (VOM1271) |
| `DISCHARGE` | **40** | GPIO5 | décharge active, **logique inversée** (bas = décharge active) |
| libre | **39** | GPIO4 | **entrée**, pull-up interne désactivé — **exige un pull-down externe** |
| libre | **41** | GPIO6 | idem |
| LED bleue | 47 | GPIO12 | état nominal / motifs de défaut |
| LED rouge | 36 | GPIO33 | idem |
| JTAG | 20-23 | TDI/TMS/TDO/TCK | **ne jamais reconfigurer en GPIO** |

Polarité des LED : **actif haut** — confirmé au bring-up sur la carte 2
(`LED_ACTIVE_LOW = 0` dans `calib.h`), contrairement à l'hypothèse initiale
de câblage via 1 kΩ vers le 3,3 V.

**Aucune broche TZ externe n'est disponible** : GPIO12 = TZ1 est pris par la
LED, GPIO16/17 = TZ2/TZ3 par les Stage-EN. Tous les trips passent donc par
le Digital Compare interne depuis `COMPxOUT`. Conséquence assumée : pas
d'arrêt d'urgence ni de thermostat matériel externe sans reprendre le
routage.

---

## 4. Chaîne de sécurité câblée (hors MCU)

Deux portes ET **74HVC1G08** (IC8, IC9), une par étage :

```
COMPxOUT (Stagex-default, CMPINV=1 : 1 = OK, 0 = défaut) ──┐
                                                            ├─ AND ─→ PWMx-EN (validation du driver)
GPIO Stagex-EN (logiciel : 1 = autorisé, 0 = inhibé)     ──┘

EPWMxA ─────────────────────────────────────────────────────────→ entrée PWM du driver (DIRECT)
```

**La porte ET n'est PAS en série avec le PWM** : elle attaque l'entrée de
validation du driver, le signal PWM lui parvenant par un chemin séparé. Le
PWM reste donc visible sur l'entrée du driver même étage inhibé — c'est
normal, et ce n'est pas une fuite. Vérifié au scope le 20/08/2026 (V0.2,
étage 2, MOSFET absent) : `Stage2-EN = 0` laisse EPWM2A commuter, et c'est
la **sortie** du driver qui se fige.

Drivers **actifs à l'état haut**, d'où `CMPINV = 1`. Le logiciel peut
**inhiber en plus, jamais outrepasser** la coupure matérielle.

**État de repli, mesuré.** Driver invalidé → sortie **basse** → grille à
0 V → MOSFET **bloqué**, quelle que soit l'activité sur l'entrée PWM. Les
trois mécanismes de coupure convergent vers ce même état :

| Origine | Sortie driver | MOSFET |
|---|---|---|
| `Stagex-EN = 0` (logiciel) | bas | bloqué |
| `COMPxOUT = 0` (surintensité câblée, 30 ns) | bas | bloqué |
| `TZ_FORCE_HI` (Trip Zone, broche forcée haute) | bas | bloqué |

**Défaut connu de l'état au reset — correctif matériel obligatoire.**
GPIO1/GPIO3 (COMPxOUT) sont des broches à fonction PWM : leurs pull-ups ne
sont pas activées au reset, elles **flottent**. GPIO16/17 ne sont pas des
broches PWM : pull-ups actives → **état haut**. À la mise sous tension, une
entrée de chaque porte ET flotte et l'autre est à 1 → sortie indéterminée,
le driver peut être validé **avant que le firmware ne démarre**.

- Pull-down **10 kΩ sur les deux entrées de chaque porte ET** (IC8, IC9)
- Pull-down 10 kΩ sur les lignes PWM vers les drivers
- Pull-down **2,2 kΩ sur TRST** (broche 2, actif haut)
- Côté logiciel : pull-ups internes de GPIO16/17 désactivées via `GPAPUD`

Le firmware ne peut rien pour la fenêtre pré-boot — c'est du matériel.

### Décharge active — failsafe indépendant du 3,3 V

Pilotée par **GPIO5 (broche 40)**, logique inversée, via un pont résistif
référencé **à la HT elle-même** et non au rail logique. Le MOSFET de
décharge devient passant dès que GPIO5 ne pilote plus activement un état
bas : CPU planté, halte debug, ou alimentation logique coupée alors que la
HT est présente. Sans elle, 400 V resteraient présents près d'une minute
sur le seul bleeder de 1 MΩ.

> **Un boost à 0 % de rapport cyclique ne donne pas 0 V.** Le chemin
> `Vin → L → diode → Cout` reste passant en permanence. `HV_EN` (VOM1271,
> 265-325 µs — coupure lente, **pas** une protection rapide) est le seul
> organe qui isole réellement la charge.

---

## 5. Composants de puissance

| Fonction | Composant | État |
|---|---|---|
| MOSFET de puissance | **NTD100N70GN1** (GaN, remplace l'IPD60R360 600 V prévu) | carte reçue, montage en cours |
| Diode | SiC **STPSC406** (600 V / 4 A) | à monter |
| Inductance | 47 ou 100 µH | **valeur non tranchée** |
| Driver de grille | **UCC27517** (remplace UCC27518/19 : seuils CMOS/TTL indépendants de VDD) | à monter |
| Alim driver | LM317 dédié carte puissance, réglé à **6 V** (point de référence datasheet du NTD100N70GN1 — ni 5 ni 7 V) | — |
| Switch de sortie HT | MOSFET N piloté par opto photovoltaïque **VOM1271** | — |
| Ampli de shunt | **TSV791** (50 MHz) — a remplacé TLV9151, lui-même remplaçant du MCP6001 de banc | monté |
| Mesure IIN | **INA293A2** (gain 50), Rsense **0,01 Ω** — *V0.1 : ZXCT1109, Rsense 0,0208 Ω, Rgain 10 kΩ* | monté **V0.2** |
| Mesure IOUT | **ZXCT1109 en version flottante**, protégé côté HT par **T13** (PNP FFMT560) ; sortie courant chargée par R3. Rsense **1 Ω**, R3 **4,7 kΩ** — *avant 26/08/2026 : 10 Ω / 1,5 kΩ, pleine échelle 54 mA* | monté **V0.2** |
| Shunt MOSFET **étage 1** | **0,01 Ω** (V0.1 : 0,02 Ω) — chaîne à **0,34 V/A**, pleine échelle 9,7 A | monté **V0.2** |
| Shunt MOSFET **étage 2** | **0,02 Ω** — inchangé depuis V0.1, gain jamais mesuré | monté |
| MOSFET de décharge | **≥ 600 V** (l'IRF840 500 V était sous-dimensionné) | **monté V0.2** |
| NTC | B57451V5103J062 (0805), pont 10 k / 10 k | monté |

### Évolution de la chaîne d'amplification — historique important

Le TLV9151 a été **abandonné pour la mesure de courant shunt** : à 144 kHz
de bande passante en boucle fermée, il n'avait pas fini de monter à
l'instant d'échantillonnage, ce qui rendait la voie **non étalonnable**. Le
TSV791 (τ = 100 ns) restitue la rampe fidèlement. Les suiveurs de tension
restent en TLV9151, dont l'offset se compte en microvolts.

**Filtres RC 1 kΩ + 10 nF ajoutés en sortie de tous les suiveurs.** Avant
cet ajout, la réinjection de charge de l'ADC faisait lire V1 jusqu'à **16 %
trop haut, de façon bimodale**. Tout étalonnage antérieur à ce filtre est à
jeter.

---

## 6. Calibration des chaînes de mesure — **carte V0.2**

> **Le passage V0.1 → V0.2 a invalidé tout l'étalonnage antérieur.** Les
> valeurs V0.1, leurs points bruts, l'historique du gain IIN et les pièges
> rencontrés sont archivés dans **`docs/calibration-V0.1.md`** — à rouvrir
> tel quel si une carte V0.1 est remise en service.

Valeurs réellement dans `src/calib.h`, étalonnées **en continu, PWM inhibé**
(le multimètre est perturbé par le découpage à 200 kHz et lisait jusqu'à
56 % de moins que l'ADC).

| Voie | Relation | Rév. | Confiance |
|---|---|---|---|
> ⚠️ **Défaut de masse corrigé le 17/08/2026** : carte de contrôle et carte de
> puissance étaient alimentées par deux alimentations dont les masses
> n'étaient pas reliées. **Tous les relevés antérieurs sont invalidés**, y
> compris ceux qui semblaient confirmer une constante.

| Voie | Relation | Rév. | Confiance |
|---|---|---|---|
| I1 | `(Vadc − 0,031) / 0,58` | **V0.2** | ✅ **deux routes indépendantes** sur signal propre (RC d'entrée 1k/100pF), offset mesuré à RUN=0. ⚠️ remonte ~20 % au-dessus de `IIN` : l'ADC échantillonne aux ~2/3 de la conduction et non à la moitié (cf. `pwm.c`) — **ne pas comparer les deux voies** |
| V1 | `Vadc × 29,27` | **V0.2** | ✅ trois points de 23 à 46 V, dispersion ~1 % |
| VIN | `Vadc × 10,909` | V0.1 | ✅ concorde au multimètre depuis la correction de masse (24,1 V des deux côtés) |
| IIN | `Vadc × 1,78` | **V0.2** | ⚠️ **un seul point** (0,97 A), lecture qui bat de ±4,5 % |
| VOUT | `Vadc × 177,0 + 1,70` | **V0.2** | ✅ **loi à deux termes** : LED1 est en série dans le pont, d'où l'offset. Trois déterminations indépendantes concordant à 1 % — rapport netlist 990 k/5,6 k = 177,8, ajustement sur deux points (12,65 et 49 V), chute de LED mesurée 1,5-1,6 V. ⚠️ points à 12 et 49 V pour une chaîne qui doit aller à 500 : à reprendre vers 200 V. **Relevés D4 déposée**, cf. ci-dessous |
| I2 | `Vadc / 0,637` | V0.1 | 🔴 **jamais mesurée** ; shunt resté à 0,02 Ω, donc gain ≈ **double** de I1 |
| IOUT | `Vadc × 0,0475` | **V0.2** | ✅ **mesurée**, offset **nul** (< 0,05 mA). ⚠️ **la chaîne compresse** : 21,07 V/A à 27 mA, 20,10 à 47,7, 18,51 à 104 — soit **−12 % sur la plage**. La constante est calée sur la **zone d'usage 10-20 mA** ; la voie sous-lit donc d'environ 12 % vers 100 mA. Ne pas l'utiliser pour un rendement à pleine charge sans corriger |
| NTC | `R = R_fixe × (VREF − Vadc)/Vadc`, β = 4000 K | V0.1 | ⚠️ sens validé seulement |

Points de mesure V0.2 retenus (relevés au multimètre contre l'affichage
serveur, PWM inhibé — VIN 24,18 V / V1 23,55 V, l'écart de 0,63 V étant la
chute directe de la FFSD2065, preuve que la conduction était continue) :

```
V1  : 43,68 V réels / 46,0 V annoncés  ->  30,82 x 43,68/46,0 = 29,266
      23,55 V réels / 24,8 V annoncés  ->  30,82 x 23,55/24,8 = 29,266
IIN : 126 mV en sortie d'INA293A2 (x50) pour 0,27 A
      -> Vshunt 2,52 mV -> Rsense 9,3 mOhm -> I = Vadc x 2,000
```

**Réserve de méthode** : ces points viennent de l'affichage du serveur,
arrondi à une décimale. Les rapports sont solides, la troisième décimale ne
l'est pas. Toute reprise doit se faire sur la **valeur brute de l'ADC** lue
au débogueur, seule façon de séparer un gain d'un offset — c'est ce qui
manque aujourd'hui à VIN.

Offsets des shunts remesurés à courant nul avec le TSV791 : **0 à 4 counts**,
soit au plus 3,2 mV — sous la résolution de l'ADC. Retenus à zéro, ce qui
est à la fois la meilleure estimation et le choix sûr (un offset
sous-estimé abaisse le code DAC, donc fait déclencher un peu plus tôt).
**Mesure V0.1, non reprise sur V0.2.**

Conversion brut → volts : `Vadc = raw × 3,3 / 4096`.

### Conséquence du nouveau gain V1 sur les seuils

> ⚠️ **PLEINE ÉCHELLE V1 = 96,6 V, PAS 103.** Les 103 V dataient du gain
> V0.1 (30,82). Avec le gain V0.2 mesuré (29,27), la conversion vaut
> `4096 / (3,3 × 29,27) = 42,4 raw/V`, soit **96,6 V**. La coupure portée à
> 82 V le 23/08/2026 est donc à **85 %** de l'échelle, pas 80. L'invariant
> « la mesure ne sature jamais avant que la protection n'agisse » tient
> toujours, avec moins de marge qu'annoncé. Au-delà de 90 V il faudrait
> revoir le pont diviseur.

Les seuils de `control.c` sont recalculés depuis le gain à la compilation,
ils redeviennent donc justes automatiquement. Mais **tant que le gain V0.1
était en place sur la carte V0.2**, ils tombaient 5,3 % trop bas :

| Seuil | Intention | Valeur réelle avec l'ancien gain |
|---|---|---|
| `CTRL_V1_OV_TRIP_V` | 55 V | déclenchait à **52,2 V** |
| `CTRL_V1_SET_MAX_V` | 50 V | plafonnait à **47,5 V** |

C'est l'explication des survoltages V1 qui partaient plus tôt que prévu.

### Seuil de protection rapide

- Seuil défini en **ampères crête** : `SAFETY_ISHUNT_THRESHOLD_A = 7,0 A`
  (**V0.2** — relevé de 3,5 A, obligatoire : le domaine visé est 3 A moyen /
  6 A crête, et 3,5 A ferait déclencher en fonctionnement normal)
- Code DAC **calculé par macro** (`SAFETY_DAC_CODE_FROM_V`), jamais en dur
- Formule TI : `V = DACVAL × (VDDA − VSSA) / 1023` — **1023, pas 4096**
- Écrêtage obligatoire : `DACVAL` est un champ 10 bits, un code > 1023 est
  **tronqué et non saturé** (3,67 V → 1138 → tronqué à 114 → déclenchement
  dès 0,45 A, l'inverse de l'effet voulu — le piège s'est réellement produit)
- Qualification `QUALSEL = 31` (~530 ns) : **indispensable**, sans elle la
  protection déclenchait sur des pointes de commutation invisibles au scope
- Temps de réponse comparateur → Trip Zone : **30 ns** (asynchrone)

**Limite d'exploitation — levée en V0.2.** Le comparateur surveille le
courant *instantané*, donc le sommet de la rampe d'inductance :

| Vin | I_moyen | ΔI c-à-c | I_crête | vs seuil 7,0 A (V0.2) | *(mémoire : vs 3,5 A en V0.1)* |
|---|---|---|---|---|---|
| 22,4 V | 2,40 A | 1,22 A | 3,01 A | 133 % de marge | *16 %* |
| 15,0 V | 3,50 A | 1,08 A | 4,04 A | 73 % de marge | *déclenchait* |
| 10,0 V | 5,26 A | 0,83 A | 5,68 A | 23 % de marge | *hors portée* |

→ **La pleine puissance (50 W) est désormais couverte sur toute la plage
d'entrée**, jusqu'aux 10 V de conception. La pleine échelle de la chaîne
étage 1 est passée de 4,04 A à **9,7 A** (shunt 0,01 Ω, 0,34 V/A).

> **Le repliement de puissance (tag `LIM`) n'est plus nécessaire pour cette
> raison.** Il était le contournement logiciel d'une limite matérielle qui
> n'existe plus (`software.md` §7 bis, `docs/tms320_agent.md`). Il peut
> rester souhaitable pour d'autres motifs — thermique, dimensionnement de
> l'inductance — mais ce n'est plus la protection contre les surintensités
> qui l'impose.

### Seuils de coupure

| Grandeur | Seuil | Nature |
|---|---|---|
| I1 crête | **7,0 A** (V0.2) — code DAC 738 | matériel (comparateur + Trip Zone one-shot) |
| I2 crête | ⚠️ **~4,9 A effectifs**, pas 7,0 A — voir ci-dessous | matériel (idem) |
| V1 | **82 V** (23/08/2026, consigne portée à 75 V) | logiciel, ISR ADC, sur valeur brute |
| VOUT | 520 V | logiciel, ISR ADC, sur valeur brute |
| VIN (sous-tension) | 9,5 V, armée après passage à 12 V, anti-rebond 5 séquences | logiciel |
| T1 / T2 | 80 °C, hystérésis 10 °C | logiciel (thermique = lente, pas de chemin matériel requis) |

> ⚠️ **`SAFETY_ISHUNT_THRESHOLD_A` est unique, mais ne vaut plus le même
> courant sur les deux étages.** Depuis que les shunts diffèrent (0,01 Ω sur
> I1, 0,02 Ω sur I2), chaque `SAFETY_DAC_CODE_STAGEx` applique le gain de
> *son* étage. Avec un I2 déduit à 0,68 V/A, 7,0 A demanderait 4,76 V — au-delà
> de la référence du DAC, donc **écrêté à 1023** par `SAFETY_DAC_CODE_FROM_V`.
> L'étage 2 déclencherait en réalité vers **4,9 A**, silencieusement.
>
> Sans conséquence tant que l'étage 2 n'est pas peuplé. **À traiter en
> scindant la constante en deux (une par étage) avant de le peupler.**

---

## 7. Points ouverts et bloquants matériels

### Bloquants matériels de l'étage 2 — **tous levés en V0.2**

Intégrés à la carte V0.2, plus rien à monter côté matériel :

- **MOSFET de décharge active ≥ 600 V** — monté. L'IRF840 500 V était
  sous-dimensionné pour une sortie qui atteint 500 V et coupe à 520 V.
- **RC 100 Ω / 100 pF sur l'entrée du driver de l'étage 2** — intégré. C'est
  le correctif qui a résolu l'oscillation de grille de l'étage 1 ; sans lui un
  MOSFET a été détruit (voir `docs/PROMPT-oscillation-grille.md`).
- **RC 1 kΩ / 100 pF à l'entrée de l'ampli de shunt I2** — intégré.
- **Retour de masse en étoile pour les ponts diviseurs** — intégré.

Reste à monter : le **MOSFET principal de l'étage 2**.

### Écart de gain sur I1 — **clos en V0.2**

Sur V0.1, le gain mesuré (0,816 V/A) donnait `0,816 / 31,3 = 25,9 mΩ` de
résistance effective pour 20 mΩ nominaux : **29 % d'écart inexpliqué**, resté
ouvert malgré plusieurs séances. La ligne de base négative (−34 mV) signalait
du cuivre partagé entre l'extrémité froide du shunt et le chemin de retour,
mais n'en représentait que 0,4 mΩ.

La question est sans objet sur V0.2 : la chaîne étage 1 a été refaite (shunt
0,01 Ω, gain ajusté) et **mesurée directement en sortie d'amplificateur**,
sans passer par le shunt ni par `Rf/Rg` — donc sans hypothèse susceptible de
porter l'écart :

```
10 A  ->  3,40 V   ->   0,34 V/A
```

**L'écart n'a jamais été expliqué, il a été contourné.** À garder en tête si
un désaccord du même ordre réapparaît sur I2, dont la chaîne n'a pas été
refaite : même schéma, mêmes références, shunt toujours à 0,02 Ω.

> **20/08/2026 — l'écart a probablement trouvé son explication.** La première
> mise sous tension de l'étage 2 a révélé sur son shunt un offset négatif de
> **−25 à −30 mV**, jumeau des −34 mV de l'étage 1. Le même défaut sur les
> **deux** étages n'est plus une coïncidence : c'est un défaut de **prise
> Kelvin systématique**, du cuivre partagé entre l'extrémité froide du shunt
> et le retour de puissance. Voir la section suivante.

### Prise Kelvin des shunts — **défaut ouvert, compris le 20/08/2026**

Les deux voies de courant présentent, pendant le découpage, des pointes de
**~100 mV** aux bornes du shunt — soit 3,4 V en sortie d'ampli, au-dessus des
seuils de coupure (2,351 V sur I1, 2,548 V sur I2). Les comparateurs
coupaient donc à juste titre sur ce qu'ils mesuraient.

**Ce n'était pas du courant.** Trois arguments concordants :

- **forme** : pointes étroites, **bipolaires**, calées sur les fronts de
  commutation — une chute résistive suivrait la rampe du courant ;
- **amplitude** : `V = L·di/dt = 2 nH × 50 A/µs = 100 mV`, soit exactement
  l'inductance propre d'un shunt CMS et de ses accès ;
- **impossibilité arithmétique** : avec L2 = **440 µH** (2 × Coilcraft
  MSS1583-224 en série, valeur confirmée au marquage) et 0,5 µs de conduction
  sous 50 V, le courant d'inductance ne peut pas dépasser `V·t/L = 57 mA`.
  Les 3,4 V correspondaient à 5,3 A — **93 fois** le maximum atteignable.

Fausses pistes écartées, pour qu'elles ne soient pas reprises : réamorçage de
grille, sonnerie DCM (la fréquence collait à 440 kHz, l'amplitude non —
`Z₀ = √(L/C) ≈ 1200 Ω` ne donne que 125 mA), amplificateur oscillant (il
amplifiait fidèlement : 100 mV × 34 = 3,4 V).

> **Correction du même soir — l'hypothèse Kelvin est réfutée.** Une paire
> Kelvin torsadée, soudée des pastilles du shunt aux entrées de l'ampli,
> **n'a rien changé** (crête 1,50 → 1,55 V). Cela élimine d'un coup le cuivre
> partagé sur le chemin de mesure et la surface de boucle — les deux seuls
> mécanismes qu'une torsade corrige.
>
> **Le parasite s'injecte en aval de l'ampli.** Le raisonnement est
> arithmétique : l'ampli est limité à τ = 3,3 µs par son condensateur de
> contre-réaction ; il lui est impossible de restituer une impulsion de
> 150 ns arrivée par ses entrées. Puisqu'elle sort quand même, elle est
> injectée sur la piste, la broche, ou l'alimentation de l'AOP.
>
> **Correctif trouvé : 10 nF sur la broche**, en aval des 100 Ω de sortie
> d'ampli (τ = 1 µs). Crête divisée par 2,5, et surtout : la montée à
> **500 V** a ensuite donné des crêtes à **0,55 V** pour un seuil à 2,351 V,
> soit une marge de 4. C'est ce condensateur qui a débloqué la tension
> nominale.

**L'offset négatif, lui, reste inexpliqué.** Il se présente comme un signal
différentiel, indiscernable du courant — aucun filtrage ne le corrigera. Les
pistes de routage (prise Kelvin, shunt quatre bornes, boucle sans surface)
restent souhaitables, mais elles ne sont plus le sujet du déclenchement.

### Le déclenchement est stochastique — ne pas filtrer davantage

Relevé sur 4 s à 500 ms/div : l'enveloppe est **stationnaire**. Pas de
dérive, pas d'oscillation croissante, pas d'escalier — **le convertisseur ne
s'emballe pas**.

Mais ses crêtes chevauchent le seuil en permanence. Ce qui empêche une
coupure immédiate est `SAFETY_COMP_QUALSEL = 31`, qui exige ~0,5 µs de
dépassement **continu** : la quasi-totalité des pointes sont plus brèves et
sont rejetées. De loin en loin l'une est assez large et passe.

D'où le délai aléatoire observé — 1 s, 4 s, 5-6 s — qui est une **loi de
probabilité**, pas un mécanisme. Et d'où le fait que chaque filtrage
supplémentaire rallonge le délai sans rien régler : on déplace la
distribution, sa queue atteint toujours le seuil.

Les deux réponses réelles : le **blanking** (masquer le comparateur pendant
la commutation — à vérifier dans le TRM, `TZSEL.DCAEVT1` prend aujourd'hui
l'événement non filtré), ou la réduction du parasite à sa source.

### Condensateurs de contre-réaction sur les amplis de shunt — **posés le 20/08/2026**

**1 nF en parallèle sur `R_f` = 3,3 kΩ, sur les deux voies.** Pôle à 48 kHz,
τ = 3,3 µs. La pointe inductive ressort à ~1 V au lieu de 3,4 V, sous les
seuils.

**Ne jamais mettre cette capacité entre les entrées.** Un essai à 1 nF entre
les entrées a fait osciller l'ampli et déclencher aussitôt, *à vide*. La
source est le shunt (20 mΩ), donc une entrée + tenue de façon très raide :
vu du nœud inverseur, ce condensateur est électriquement un condensateur vers
la **masse alternative**. Il vient en parallèle sur `R_g` et affaiblit la
contre-réaction — pôle à 1,6 MHz avec 1 nF, en pleine bande de l'AOP. À
100 pF le pôle est à 16 MHz, d'où l'innocuité de la valeur d'origine.

Le montage étant **non inverseur**, le gain ne tombe pas à zéro mais à un, ce
qui suffit largement.

**Le prix payé, à connaître :** 48 kHz est *en dessous* des fréquences de
découpage (100 et 200 kHz). La protection coupe désormais sur la **moyenne**
et non sur la **crête** — la contrainte de crête du MOSFET n'est plus
surveillée directement. Ce qui l'autorise est un calcul :
`di/dt = Vin/L = 50 V / 440 µH = 114 mA/µs`, donc 380 mA de variation
possible pendant les 3,3 µs du filtre, soit 10 % d'un seuil à 4 A. **Si
l'inductance change, ce raisonnement est à refaire.**

**Méthode d'étalonnage retenue** (la seule valide) : en conduction
continue, le courant d'inductance **à mi-conduction** vaut exactement le
courant d'entrée — aucune hypothèse sur L, le shunt ou le gain. **Ne jamais
utiliser la méthode par la pente** (`dV/dt = gain × Vin/L`) : elle confond
le gain avec l'inductance réelle, connue à ±20 % au mieux. Deux tentatives
ont donné 0,62 puis 0,73.

### Mesures restant à faire sur V0.2

Par ordre de priorité.

- [x] ~~**I1**~~ — fait : 10 A → 3,40 V, soit 0,34 V/A. Seuil relevé à 7,0 A.
- [ ] 🔴 **Confirmer que 7,0 A crête reste sous la SOA du NTD100N70GN1 et
      sous le courant de saturation de l'inductance.** Seule contrainte
      restante sur le seuil, et elle ne se lit pas au banc mais dans les
      fiches — d'autant que la valeur d'inductance n'est toujours pas tranchée.
- [ ] **Scinder `SAFETY_ISHUNT_THRESHOLD_A` en deux constantes** (une par
      étage), les shunts n'étant plus identiques — avant de peupler l'étage 2.
- [ ] **VIN** — écart de ~1 % sur V0.2 ; reprendre sur la **valeur brute de
      l'ADC** à deux tensions, seule façon de séparer gain et offset.
- [ ] **IIN** — un second point vers 1 à 2 A (le point V0.2 actuel est à
      0,27 A, il ne verrouille pas l'absence d'offset).
- [x] ~~**VOUT**~~ — **étalonnage CLOS le 20/08/2026** : `Vadc × 177,0 + 1,70`,
      vérifiée sur **quatre points de 12,65 à 500 V** — un rapport 40 — avec
      au pire 0,4 % d'écart (200,4 contre 200,0 ; 500 au multimètre contre
      500-502 affichés). Plus aucune extrapolation. La coupure à 520 V
      s'appuie donc sur une chaîne vérifiée jusqu'au voisinage immédiat du
      seuil.
- [ ] ⚠️ **Caractériser le délestage à 500 V.** Il ne reste que **20 V**
      entre la consigne maximale et le seuil, soit 4 %. Le dépassement mesuré
      sur l'étage 1 après correction du PID était de 2 % — mais l'étage 2,
      dont le condensateur est cent fois plus petit, n'a jamais été testé en
      délestage. Scope armé obligatoire.
- [ ] 🔴 **Reprendre la prise Kelvin des deux shunts** — voir la section
      dédiée. Défaut compris le 20/08/2026, présent sur les **deux** étages,
      contourné par filtrage mais non corrigé. C'est lui qui rend le seuil de
      4 A approximatif et qui explique probablement les 29 % d'écart de I1.
- [ ] **Synchroniser les deux ePWM** (`PHSEN` est à `TB_DISABLE`). Le rapport
      2:1 exact et le TBCLK commun font qu'une impulsion de l'étage 1 sur deux
      coïncide avec la commutation de l'étage 2, mais le décalage est fixé au
      hasard du démarrage et change à chaque mise sous tension. `adc.c` place
      son déclenchement à un instant précis du cycle : selon le tirage, la
      mesure est propre ou polluée.
- [x] ~~**Remettre un clamp sur la voie VOUT**~~ — fait le 20/08/2026 :
      clamp sur **3,3 V en sortie du suiveur**, et non sur le nœud du pont.
      C'est le bon emplacement : sur le nœud, D4 redressait le couplage de
      découpage et fabriquait jusqu'à +480 mV de continu, soit 130 V d'erreur
      d'affichage ; en sortie de suiveur le signal est basse impédance et
      propre. L'étalonnage à 200 V a été confirmé **avec** ce clamp en place.
- [ ] **Sortir LED1 du pont de mesure** et la mettre sur sa propre branche.
      Un boîtier 5 mm traversant en série dans une chaîne d'instrumentation
      apporte une chute non linéaire, une jonction redresseuse et une
      antenne. C'est le correctif de fond du problème ci-dessus.
- [x] ~~**IOUT — offset**~~ — mesuré **nul** le 20/08/2026 (< 0,05 mA), bien
      mieux que les 2,45 mA de pire cas redoutés. La réserve contre le shunt
      de 1 Ω est levée.
- [x] ~~**IOUT — gain**~~ — mesuré : `Vadc × 0,0475`, calé sur la zone d'usage.
- [ ] ⚠️ **IOUT — caractériser la compression.** Le gain décroît de **12 %
      entre 27 et 104 mA** (21,07 → 18,51 V/A), de façon monotone et sur des
      points PWM inhibé. Aucune constante unique ne sert toute la plage : la
      voie **sous-lit d'environ 12 % vers 100 mA**. Suspect principal, la
      compliance de sortie du ZXCT1109 (broche OUT servant de substrat,
      note 1 p.2 du DS35033). À trancher avant toute courbe de rendement à
      pleine charge.
- [ ] **IOUT — supprimer le résidu de découpage.** Le 10 nF oublié en sortie
      de suiveur a ramené l'erreur en marche de −12,6 % à −3,5 %. Passer
      **C14 de 100 pF à 10 nF** (R9 = 10 kΩ est déjà en place en amont)
      supprimerait le reste, sans aucun risque : `measure_iout()` n'alimente
      que la télémétrie, pas de latence à préserver.
- [ ] **Vérifier les 10 nF des autres suiveurs.** Celui de IOUT avait été
      oublié lors de la campagne « RC 1 kΩ + 10 nF en sortie de tous les
      suiveurs » (§5). Rien ne dit qu'il était le seul.
- [ ] **I2** — jamais mesuré, à faire comme I1.
- [ ] **NTC** — confirmer la résistance fixe réelle (10 kΩ supposé) ; un
      point à température connue ≠ 25 °C reste souhaitable (le pont
      symétrique 10 k/10 k ne discrimine pas).
- [ ] Valeur d'inductance retenue par étage (47 ou 100 µH) — influe sur le
      mode CCM/DCM et la vitesse de montée d'un courant de défaut.

**Méthode imposée** : en continu, PWM inhibé, sur la valeur brute de l'ADC
lue au débogueur — jamais sur l'affichage du serveur. Les pièges qui ont
coûté plusieurs séances sur V0.1 (multimètre perturbé à 200 kHz,
réinjection de charge de l'ADC, repliement d'une oscillation à 2,4 MHz,
méthode de la pente) sont détaillés dans **`docs/calibration-V0.1.md` §4**
et restent tous valables sur V0.2.

### Évolution sur IIN — **faite en V0.2**

Le **INA293A2** (gain 50, shunt 0,01 Ω, `I = Vadc × 2,000`) a remplacé le
ZXCT1109. Offset ramené à ~1,5 mA contre ~37 mA, et deux fois moins de
dissipation (20 mW à 1,42 A). Pleine échelle 6,6 A, dont ~6,4 A utiles ;
résolution 1,6 mA par LSB. `MEAS_IIN_A_PER_V` est passée de `1.214f` à
`2.000f`.

**Attention, contrainte permanente** : contrairement au ZXCT1109 qui se
nourrissait de la ligne mesurée, l'INA293 exige une **alimentation séparée
2,7-5,5 V**, à protéger (TVS 3,6 V). Le rail 3,3 V doit lui parvenir.

Reste à faire : un **second point vers 1 à 2 A**. Le point actuel est à
0,27 A seulement ; il confirme le gain mais ne verrouille pas l'absence
d'offset — que la fiche du INA293 rend cependant très probable.

### Alimentations à compléter

| Broche | Rail | Découplage |
|---|---|---|
| 11 | VDDA | 3,3 V + 2,2 µF au plus près |
| 35 | VDDIO | 3,3 V + découplage **actuellement absent** |
| 32, 43 | VDD | 1,2 µF minimum par broche (VREG interne actif) |

Séquencement : **VDD doit atteindre 0,7 V avant VDDIO** pour éviter un
glitch sur les sorties au démarrage. Broche 30 (TEST) impérativement non
connectée.

### Campagnes de rendement — ce qui est faisable aujourd'hui

- **Étage 1 seul** : réalisable dès que le matériel est remonté, en mode
  `VOSET = 0`, avec charge résistive connue R sur V1 :
  `rendement = (V1² / R) / (VIN × IIN)`. N'utilise ni `IOUT` ni `I1`/`I2`.
- **Système complet** : bloqué tant que `IOUT` n'est pas mesuré et que
  l'étage 2 n'est pas monté.

> **`I1` et `I2` ne sont pas des courants moyens.** Les shunts sont dans la
> **source des MOSFET** et l'ADC échantillonne à un instant fixe du cycle,
> près du pic du courant d'inductance. `V1 × I1` n'a donc **pas** la
> dimension d'une puissance de sortie d'étage. Ces voies existent pour la
> protection et le diagnostic, jamais pour un calcul de rendement.

---

## Résonance du filtre d'entrée — diagnostiquée le 25/08/2026

**C'est la cause réelle des défauts `overI1` qui bloquaient la montée en
puissance, et elle n'est pas sur la carte.**

### Le mécanisme

Un convertisseur régulé consomme une puissance constante : quand `Vin`
baisse, il tire *plus* de courant. Il présente donc à son entrée une
**résistance incrémentale négative** de `−Vin²/P`, soit **−14 Ω** à 40 W
sous 23,7 V. Cette résistance négative annule l'amortissement du LC formé
par l'**inductance des fils d'alimentation** et la capacité d'entrée de la
carte. Le filtre devient un oscillateur. C'est le critère de Middlebrook.

### Les mesures

| grandeur | valeur |
|---|---|
| fréquence d'oscillation initiale | **3 280 Hz** |
| capacité d'entrée `Cin` | 180 µF |
| impédance caractéristique `R₀ = 1/(2π·f·Cin)` | **0,27 Ω** |
| inductance des fils, déduite `L = R₀/(2πf)` | **13 µH** (≈ 1 m de câblage) |
| ondulation `Vin` avant traitement | 145 mV AC RMS |

### Comment elle se manifestait

Sur la sortie de l'ampli de shunt, en **enveloppe** modulant les pointes de
commutation : de 0,42 V au creux à 1,5 V au sommet, période 305 µs. Les
pointes elles-mêmes sont parasites — 0,42 V sur un shunt de 0,01 Ω
donneraient 42 A — mais `QUALSEL` les rejette, car elles durent moins que
les 533 ns exigés. **Le défaut survient quand l'enveloppe culmine et que
l'excursion devient assez LONGUE**, pas assez haute.

D'où deux caractéristiques trompeuses, qui ont coûté une journée :

- **`Iin` ne bouge pas** au moment du défaut — 1,69 A, vérifié deux fois.
  Le courant réel est hors de cause.
- **ça dépend de la puissance et de `Vin`** : la conductance négative vaut
  `−P/Vin²`. À 24 V ça tenait cinq minutes, à 20,9 V une seconde.

### Le remède

**Réseau d'amortissement R-C série aux bornes d'entrée**, fils courts :

| | valeur retenue | optimum calculé |
|---|---|---|
| `C_d` | **1000 µF** électrolytique, 35 V min | ≥ 4 × `Cin` |
| `R_d` | **0,5 Ω** (ce qui était en stock) | **0,138 Ω** |

Résultat : de « défaut en 1 à 2 s » à **cinq minutes stables à 40 W**, avec
quelques bosses résiduelles. `R_d = 0,5 Ω` est **3,6× au-dessus de
l'optimum** : passé l'optimum, un amortisseur R-C amortit MOINS, la
résistance déconnectant progressivement le condensateur. Descendre à
0,22 Ω est le levier restant le plus direct, avec le raccourcissement des
fils d'alimentation.

### L'erreur à ne pas refaire

**De la capacité seule DÉPLACE la résonance sans l'amortir.** Mesuré :
le 1000 µF posé sans résistance a fait passer l'oscillation de 3 280 à
~1 650 Hz — conforme au `1/√C` attendu — sans rien régler. C'est la
RÉSISTANCE qui amortit ; le condensateur ne sert qu'à laisser passer
l'alternatif en bloquant le continu, pour que `R_d` ne dissipe pas la
puissance d'entrée. Un céramique à faible ESR est le pire choix possible.

`R_d` ne voit **aucun courant continu**. À 200 kHz elle ne prend que 2,6 %
de l'ondulation de découpage — quelques µW. À la résonance, ~130 mW, et ce
chiffre s'effondre dès que l'amortissement fait effet. Un calibre 1 W
suffit très largement.

### À rejuger à froid — décisions prises sur ce diagnostic faux

Trois modifications du 25/08 visaient une pointe de commutation qui
n'existait pas. Elles n'ont plus de justification :

1. **Inversion AQ de l'étage 2** (`PWM_AQ_TAIL_STAGE2 = 1`) — le MOSFET
   600 V a changé de convention pour une raison qui ne tient plus.
2. **Blanking de l'étage 2 désactivé** — conséquence de la précédente. Or
   il FONCTIONNAIT, et c'est lui qui avait permis à l'étage 2 de monter le
   matin même.
3. **Phase `PWM_STAGE2_PHASE_COUNTS` à 150** — repli d'un essai à 0 qui
   visait le mauvais problème.

Le retour le plus probable est **l'étage 2 tel qu'il était le 24/08** :
convention d'origine, blanking actif. C'est la seule configuration qui ait
un résultat mesuré derrière elle.
