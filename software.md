# software.md — Firmware TMS320F28027 « dual boost »

Synthèse logicielle reconstituée à partir des sources (`src/*.c`, `src/*.h`)
et des documents de conception (`docs/`). Les sources font foi.

---

## 1. Environnement de compilation

| Élément | Valeur |
|---|---|
| Projet CCS | `TMS320F28027-dualboost` (Executable) |
| Cible | TMS320C28XX / TMS320F28027, cœur C28xx |
| Compilateur | TI v25.11.1.LTS (C2000), config active **Debug** |
| Sonde | Texas Instruments XDS100v3 USB |
| Target config | `targetConfigs/TMS320F28027.ccxml` |
| Bibliothèque | **C2000Ware headers bitfield** (`DSP28x_Project.h`, `F2802x_Device.h`) — **pas de driverlib**, pas de SysConfig |
| Édition de liens | `F2802x_generic_flash.cmd` + `F2802x_Headers_nonBIOS.cmd` |
| Dépôt | `git@git.syoul.fr:alim-High-Volt-boost/TMS320F28027-dualboost.git` |

Fichiers C2000Ware embarqués à la racine du projet :
`F2802x_GlobalVariableDefs.c`, `f2802x_sysctrl.c`, `f2802x_piectrl.c`,
`f2802x_pievect.c`, `f2802x_cputimers.c`, `f2802x_defaultisr.c`,
`f2802x_codestartbranch.asm`, `f2802x_usdelay.asm`.

Le mode `_FLASH` recopie les *ramfuncs* en RAM au démarrage
(`memcpy(&RamfuncsRunStart, …)` en tête de `main()`).

> **Décision en suspens** : la configuration « Debug » compile en `-O2`. À
> trancher avant de considérer le firmware figé.

---

## 2. Architecture — 3848 lignes réparties par responsabilité

```
src/
  main.c        505  ordonnancement, arbitrage des défauts, télémétrie
  control.c     483  machine d'état de démarrage + régulation
  uart_link.c   593  SCI-A, anneau RX, formatage $T, parsing $C
  pwm.c         253  ePWM1/ePWM2, duty, fréquence
  adc.c         245  9 voies, SOC déclenché ePWM, ISR courte
  safety.c      234  comparateurs + DAC + Digital Compare + Trip Zone
  bsp_gpio.c    199  mux GPIO, AIOMUX1, LED, HV_EN, Stage-EN, décharge
  status_led.c  165  motifs de clignotement par type de défaut
  measure.c      81  brut → grandeurs physiques
  bsp_clock.c    28  INTOSC1 + PLL 60 MHz
  calib.h       517  TOUTES les constantes matérielles — et rien ailleurs
  protocol.h     83  codes FAULT/STATE, structures télémétrie/commande
```

Règle structurante du projet : **aucune valeur magique hors de `calib.h`**,
chaque constante commentée avec son origine et son statut de mesure.
`calib.h` fait 517 lignes dont l'essentiel est de la traçabilité
métrologique — c'est délibéré.

---

## 3. Séquence d'initialisation

L'ordre est **imposé** et non négociable (`PROMPT §7`) :

```
GPIO en état sûr → AIOMUX1 → horloge → sécurité → ADC → UART → PWM en dernier
```

Ce que fait réellement `main()` :

```c
bsp_clock_init();          // INTOSC1 + PLL, PLLLOCKPRD = 10000 avant InitSysCtrl
bsp_gpio_leds_init();
status_led_init();         // bleu fixe : « je suis parti »
DINT; InitPieCtrl(); IER=0; IFR=0; InitPieVectTable();
bsp_gpio_analog_init();    // AIOMUX1 : AIO2/4/10/12/14 en mode analogique
bsp_gpio_control_init();   // HV_EN, Stage-EN à 0, pull-ups GPIO16/17 coupées
pwm_init();                // AVANT safety_init() — voir ci-dessous
safety_init();             // comparateurs + DAC + Digital Compare + TZ6
control_init();            // lit les périodes ePWM
adc_init();                // SOC déclenché par ePWM1
uart_link_init();
control_set_setpoints(CTRL_V1_SET_MIN_V, CTRL_VOSET_DISABLED);
// CPU Timer 0 (10 ms, PIE INT1.7) + CPU Timer 1 (195 µs, INT13 direct)
```

**`pwm_init()` avant `safety_init()` — inversion apparente de la règle,
justifiée matériellement** : `pwm_init()` active `PCLKCR1.EPWMxENCLK`, sans
quoi les écritures de `safety_init()` dans `TZSEL`, `TZCTL` et `DCTRIPSEL`
seraient **perdues**. L'esprit de la règle est respecté : `pwm_init()`
laisse les deux sorties forcées à l'état bas (`AQCSFRC`) et le duty à 0,
donc rien ne sort tant que le Trip Zone n'est pas armé.

---

## 4. Contextes d'exécution — la hiérarchie est une décision de sécurité

| Contexte | Cadence | Vecteur | Rôle |
|---|---|---|---|
| **ISR ADC** | 66,7 kHz | INT1 (PIE) | stockage des bruts + `control_fast_check()` : **survoltage** |
| **ISR CPU Timer 1** | 5,13 kHz (195 µs) | **INT13 direct** (hors PIE) | `control_tick()` : un pas de régulation |
| **ISR CPU Timer 0** | 100 Hz (10 ms) | INT1.7 (PIE) | base de temps LED, âge de la liaison, cadence télémétrie |
| **Boucle principale** | libre | — | parsing `$C`, arbitrage des défauts, formatage `$T`, émission |

Le choix d'**INT13 pour la régulation** est raisonné : il passe **après**
l'ISR ADC (INT1), qui conserve donc sa latence de détection de survoltage
quoi que fasse la régulation, et **avant** l'UART (groupe 9) — « la
régulation est prioritaire, l'envoi UART se fait s'il reste du temps ».
Étant câblé directement sur le cœur, il n'y a ni `PIEIER` à armer ni
`PIEACK` à rendre, seulement le drapeau du timer à effacer.

**La régulation a quitté l'ISR ADC** : une séquence sur treize dépassait
9 µs et débordait sur la conversion suivante. La période de 195 µs
reproduit exactement la cadence qu'obtenait la décimation par 13 des
66,7 kHz, de sorte que `CTRL_SHIFT` et les vitesses de rampe gardent la
même signification qu'avant le changement.

**Discipline d'interruption** : pas de `sprintf`, pas de flottant lourd,
pas d'attente en ISR. La loi de commande n'emploie **ni multiplication ni
division** — uniquement comparaisons, additions et décalages.

---

## 5. Sécurité (`safety.c`)

Chaîne complète, **entièrement matérielle sur le chemin critique** :

```
shunt → TSV791 → COMPxA → comparateur (réf. DAC interne, 30 ns)
     → COMPxOUT → Digital Compare (DCAEVT1) → Trip Zone one-shot → TZ_FORCE_LO
     → et en parallèle : COMPxOUT → porte ET externe → driver
```

Configuration retenue :

- `COMPCTL.COMPDACEN = 1` (sans quoi le bloc reste éteint)
- `COMPCTL.COMPSOURCE = 0` — entrée inverseuse = DAC interne
- `COMPCTL.CMPINV = 1` — drivers actifs haut : sortie 1 = OK, 0 = défaut
- `SYNCSEL = 1` + `QUALSEL = 31` (~530 ns) — la qualification n'a d'effet
  qu'en mode synchrone. **Ce n'est pas un confort** : sans elle, la
  protection déclenchait sur des pointes de commutation invisibles au scope.
  Coût : 0,11 A de dépassement supplémentaire sur un défaut franc — négligeable.
- `PCLKCR3.COMPxENCLK = 1`, sinon `COMPSTS` ne se met jamais à jour
- `TZSEL.DCAEVT1 = 1` en **one-shot latché**, pas cycle-by-cycle
- **`TZSEL.OSHT6 = 1` = EMUSTOP** sur les deux ePWM (câblé en dur sur TZ6,
  TRM SPRUI09A §3.2.7 ; aucune macro nommée dans les headers)
- `TBCTL.FREE_SOFT = 0` **en plus** de TZ6, jamais à la place : `FREE_SOFT`
  ne gèle que le compteur de base de temps — l'Action Qualifier a déjà
  positionné la sortie, qui **reste figée haute** si le CPU s'arrête pendant
  la conduction. Seul TZ6 + `TZ_FORCE_LO` garantit la mise à l'état bas.

API : `safety_get_fault_flags()`, `safety_clear_faults()` (écriture
`TZCLR[OST]`, **jamais appelée automatiquement**), `safety_force_trip_test()`
(via `TZFRC[OSHT]`, pour tester sans provoquer de vrai court-circuit).

Les surtensions ne passent **pas** par ce chemin : elles sont détectées par
`control_fast_check()` dans l'ISR ADC, par comparaison directe sur la
valeur brute, seuils pré-calculés en counts à la compilation.

---

## 6. Conduite : machine d'état et régulation (`control.c`)

### États

| Code | État | Signification |
|---|---|---|
| 0 | `IDLE` | tout coupé, décharge active, attente de `RUN=1` |
| 1 | `START_S1` | étage 1 en montée, consigne rampée depuis la tension mesurée |
| 2 | `RUN_S1` | V_inter établie, étage 2 encore à zéro |
| 3 | `START_S2` | étage 2 en montée |
| 4 | `RUN` | régulation établie — **seul état où un point de mesure est exploitable** |
| 5 | `FAULT` | état sûr verrouillé |

Deux pièges documentés :

- **`STATE=2` ne dure qu'un seul pas de régulation (~200 µs)** — aucun
  interrogateur cadencé à la seconde ne l'observera jamais.
- **`STATE=4` ne signifie pas toujours « les deux étages établis »** : en
  mode étage 1 seul (`VOSET=0`), la machine passe directement de 1 à 4. Le
  seul moyen de distinguer les deux cas est `VOSP`, nul dans ce mode.

### Loi de commande

Intégrateur pur en virgule fixe : `duty_counts = accumulateur >> CTRL_SHIFT`,
l'accumulateur recevant l'erreur brute à chaque pas.

| Constante | Valeur | Rôle |
|---|---|---|
| `CTRL_SHIFT` | 12 | gain intégral = 1/2¹² par pas |
| `CTRL_TICK_PERIOD_US` | 195 | 5128 Hz |
| `CTRL_RAMP_V1_V_PER_STEP` | 0,01 | ~51 V/s sur l'étage 1 |
| `CTRL_RAMP_VOUT_V_PER_STEP` | 0,10 | — |
| `CTRL_SETTLE_STEPS` | 500 | ~100 ms sous tolérance pour déclarer établi |
| `CTRL_DUTY_MAX` | 0,95 | duty max < 1 impératif |

**On rampe la consigne, pas le duty** : la boucle reste fermée pendant
toute la montée.

`CTRL_SHIFT = 12` est **délibérément très bas**, sans aucun modèle du
convertisseur : le boost à fort gain présente un zéro dans le demi-plan
droit qui limite la bande passante atteignable. La réponse est donc lente
— **c'est voulu, pas un défaut à corriger à l'aveugle**.

**Réglage restant à faire, sur matériel alimenté uniquement** : observer au
scope la réponse de V_inter à un échelon de consigne, diminuer `CTRL_SHIFT`
par paliers d'une unité (chacune double le gain), s'arrêter au premier
dépassement puis remonter d'un cran. Le terme proportionnel ne vient
qu'après : `duty = (accum >> CTRL_SHIFT) + (erreur >> CTRL_KP_SHIFT)`.

**Limite connue en sortie HT** : à 500 V depuis 35 V il faut D = 0,93, et
`dV/dD = Vin/(1−D)²` donne **1 LSB de duty ≈ 12 V** de quantification. La
régulation oscillera irréductiblement entre deux valeurs adjacentes tant
que le HRPWM/MEP n'est pas utilisé (~93 sous-pas par cycle SYSCLK, soit
14,8 bits au lieu de 8,2 — nécessite la bibliothèque SFO, non implémentée).
Ne concerne **pas** l'étage 1, où la quantification reste fine.

### Mode « étage 1 seul » — `VOSET = 0`

`VOSET` **exactement nul** n'est pas une consigne de 0 V, c'est la
convention qui désactive l'étage 2 : duty forcé à zéro, sortie ePWM
inhibée (`AQCSFRC`), porte ET non armée, `HT=1` sans effet, `VOSP` renvoie
0,0. C'est **l'état par défaut au démarrage**.

Ce mode existe parce que sans lui la machine resterait bloquée
indéfiniment en `START_S2`, l'intégrateur saturé à 95 % : une sortie ne peut
pas atteindre 200 V quand le MOSFET, la diode et l'inductance de l'étage 2
ne sont pas montés.

**Basculer entre les deux modes exige `RUN=0` d'abord** — une trame qui
activerait ou désactiverait l'étage 2 en marche est refusée (`REJ++`).

---

## 7. Arbitrage des défauts (boucle principale)

Priorité **telle qu'implémentée** (vérifiée dans `compute_fault_code()` et
`control_fast_check()`) :

```
2 (surint. I1) > 3 (surint. I2) > 6 (surtension V1) > 7 (surtension VOUT)
  > 9 (sous-tension VIN) > 4 (T1) > 5 (T2) > 8 (liaison perdue) > 1 (EMUSTOP)
```

Les codes 6, 7 et 9 sont produits par la même variable `s_fault` de
`control.c` et s'excluent donc mutuellement ; leur ordre relatif est celui
de la cascade `if/else if` de `control_fast_check()`.

EMUSTOP est **le moins prioritaire malgré son numéro** : en développement
il se déclenche à chaque halte du débogueur et ne doit jamais masquer une
surintensité réelle.

| Code | Nature | Effet |
|---|---|---|
| 1 EMUSTOP | transitoire | reprise autorisée, mais **par la séquence de démarrage complète** |
| 8 liaison perdue (> 2 s) | transitoire | repli en état sûr, redémarre quand la liaison revient |
| 2-7, **9** | **verrouillés** | coupure définitive, **cycle d'alimentation requis** — aucun acquittement, ni automatique ni par UART |

### Code 9 — sous-tension d'entrée (implémenté, non documenté côté ESP32)

Surveillé dans l'ISR ADC sur la valeur brute, comme les survoltages.
Motif : **un élévateur compenserait une entrée qui s'effondre en augmentant
le rapport cyclique, donc le courant, jusqu'à la surintensité. On coupe
avant.**

Trois garde-fous, tous nécessaires :

- **Armement unique** — au démarrage, VIN traverse forcément la zone basse
  pendant la montée de l'alimentation. Surveiller dès le reset rendrait la
  carte impossible à démarrer. La surveillance ne s'arme qu'après un premier
  passage au-dessus de `CTRL_VIN_UV_ARM_V = 12 V`, et le reste jusqu'au
  prochain reset.
- **Seuil 0,5 V sous le minimum de conception** (`9,5 V` pour 10 V annoncés) :
  à 10 V pile, l'ondulation d'entrée et le creux d'un échelon de charge
  feraient tomber un défaut dont on ne sort qu'en coupant l'alimentation.
- **Anti-rebond 5 séquences ADC** (75 µs, soit quinze périodes de découpage) :
  une ondulation n'y survit pas, un vrai effondrement si. Ce n'est pas du
  filtrage qui masque le problème — c'est de l'anti-rebond sur une
  protection **verrouillante**, dont un déclenchement intempestif coûte un
  cycle d'alimentation complet.

**Reste à faire** : le code 9 n'apparaît dans aucune table de
`tms320_agent.md`, `esp32_agent.md` ni `orchestration.md`. Un orchestrateur
suivant la documentation le traiterait comme inconnu — or il est verrouillé
et doit interrompre une campagne. Corrigé dans ces trois documents.

**Arrêt global** : n'importe quel défaut sur n'importe quel étage arrête
l'ensemble. Sur un boost en cascade, laisser tourner l'étage 2 alors que
l'étage 1 est coupé n'a pas de sens. Les Trip Zones restent indépendantes
**en matériel** — cet arrêt global est la couche système par-dessus.

### `enter_safe_state()` — deux actions, aucune redondante

```c
control_set_run(false);
pwm_enable(STAGE_1/2, false);  stage_enable_set(STAGE_1/2, false);
hv_enable_set(false);          // isole réellement la charge
hv_discharge_set(true);        // APRÈS la coupure HV_EN
```

Couper `HV_EN` est indispensable : **un boost à 0 % de duty ne donne pas
0 V**, le chemin `Vin → L → diode → Cout` reste passant. La décharge vient
**après** l'isolation de la charge, et vide le condensateur en ~2 s au lieu
d'une minute sur le seul bleeder de 1 MΩ.

### Reprise après EMUSTOP — trois remises à zéro nécessaires

`uart_link_restart()` (l'anneau RX a débordé pendant la halte, la ligne en
cours est tronquée) + `safety_clear_faults()` + `control_restart()`.

Ce dernier est **critique** : pendant la halte, la sortie se vide dans la
charge alors que l'intégrateur conserve le duty d'avant. Le réappliquer
tel quel emballe le courant d'inductance — la désaimantation pendant le
temps bloqué, proportionnelle à `(V1 − Vin)`, devient quasi nulle alors que
la magnétisation reste entière. Constaté en débogage à 10 V d'entrée, où le
duty élevé rend le phénomène le plus violent.

### Le garde-fou `s_power_path_armed`

`enable_power_path()` est appelée **à chaque tour de boucle** tant que
`RUN` est vrai, alors que la mise à zéro du duty qu'elle contient ne vaut
qu'à l'armement. Sans ce drapeau, la boucle principale écrase en
permanence le duty calculé par `control.c` (relevé seulement au tick de
régulation décimé) : le PWM sort alors **par salves séparées de longs
trous** — observé au scope.

---

## 7 bis. Repliement de puissance — **spécifié, non implémenté**

Limitation de la puissance d'entrée selon la tension d'entrée :

| Condition | Plafond |
|---|---|
| `VIN > 20 V` | **50 W** |
| `VIN ≤ 20 V` | **25 W** |

### Ce n'est pas un défaut

Le repliement **laisse la régulation tourner** en plafonnant la consigne de
duty : il ne coupe rien, ne verrouille rien, et ne doit donc **pas** occuper
un code `FAULT`. Un orchestrateur qui verrait un `FAULT != 0` interromprait
sa campagne alors que l'alimentation fonctionne — dégradée mais nominale.

Il lui faut donc une signalisation propre, en trois endroits :

- **`$T`** : nouveau tag `LIM` — `0` = pas de limitation, `1` = plafond 50 W
  actif, `2` = plafond 25 W actif. À placer **en tête de trame** avec
  `FAULT`, `STATE` et `REJ` : c'est une information d'exploitation, sa perte
  par troncature ferait croire à un fonctionnement libre.
- **LED** : état `LED_STATE_LIMITED`, distinct des huit existants (§10).
- **IHM / API** : champ `lim` dans `GET /api/telemetry`, affiché
  explicitement — sans quoi un point de mesure plafonné est indiscernable
  d'un point libre.

### Pourquoi 20 V, et pourquoi ces deux valeurs

Ce seuil n'est pas arbitraire : **c'est le pendant logiciel de la limite
matérielle de la chaîne de mesure de courant** déjà documentée
(`hardware.md` §6, `calib.h`). La pleine échelle réelle de la voie I1 est
de 4,04 A et le seuil de protection à 3,5 A ; le comparateur surveille le
courant **crête**, pas le moyen :

| Vin | Plafond | I_moyen | ΔI c-à-c | I_crête | Marge sous 3,5 A |
|---|---|---|---|---|---|
| 22,4 V | 50 W | 2,23 A | 1,22 A | 2,84 A | 19 % |
| 20,0 V | 50 W | 2,50 A | ~1,2 A | ~3,10 A | 11 % |
| 19,9 V | 25 W | 1,26 A | ~1,2 A | ~1,86 A | 47 % |
| 10,0 V | 25 W | 2,50 A | 0,83 A | 2,92 A | 17 % |

Sans repliement, 50 W à 15 V donnent déjà 4,04 A de crête — **au-dessus du
seuil**, donc un déclenchement à chaque tentative ; et à 10 V, 5,68 A, soit
au-delà de ce que la chaîne sait restituer. Le repliement rend donc la
plage d'entrée complète exploitable au lieu de la limiter à ~19 V.

La discontinuité à 20 V (50 W → 25 W, soit un facteur 2) est assumée : elle
place le crête très bas juste sous le seuil, ce qui est le comportement sûr.

### Points à trancher à l'implémentation

- **Hystérésis sur les 20 V obligatoire.** Sans elle, une entrée qui
  oscille autour du seuil fait basculer le plafond d'un facteur 2 à chaque
  ondulation. Reprendre la forme déjà employée pour la surtempérature
  (`SAFETY_OVERTEMP_HYST_C`) plutôt que d'en inventer une autre.
- **Mesurer la puissance par `VIN × IIN`, jamais par `V1 × I1`.** `IIN` est
  une vraie moyenne, calibrée à ±6 % sur deux points. `I1` est un
  échantillon instantané pris près du pic du courant d'inductance dans la
  source du MOSFET : `V1 × I1` n'a pas la dimension d'une puissance (§ répété
  dans `hardware.md` et `orchestration.md`, c'est l'erreur la plus tentante
  du projet).
- **Où appliquer le plafond.** Dans `control_tick()`, en bornant la sortie
  de l'intégrateur — pas en modifiant la consigne, sinon `V1SP` remonterait
  une valeur que l'opérateur n'a pas demandée. La consigne reste celle
  acceptée, c'est le duty atteignable qui est bridé.
- **Discipline d'ISR** : `VIN × IIN` est une multiplication flottante, à
  proscrire dans `control_tick()`. Comparer plutôt le courant brut à un
  seuil **pré-calculé en counts** pour chacun des deux plafonds, comme le
  fait déjà `control_fast_check()` pour les survoltages. Deux constantes de
  `calib.h`, aucune opération coûteuse en interruption.
- **Constantes à ajouter dans `calib.h`** : `CTRL_FOLDBACK_VIN_THRESH_V`
  (20,0), son hystérésis, `CTRL_PLIM_HIGH_W` (50,0), `CTRL_PLIM_LOW_W`
  (25,0). Rien en dur ailleurs.

---

## 8. Acquisition (`adc.c`)

9 voies, référence **interne** obligatoire, `Device_cal()` appelé,
**SOC déclenché par ePWM1** (jamais en free-run). ISR courte : stockage des
bruts + `control_fast_check()`, rien de plus.

Ordre de séquence — les 4 premières forment le **groupe critique** :

`I1 → V1 → VIN → IIN` puis `IOUT, I2, VOUT, T1, T2`

Deux fenêtres d'acquisition, parce que les sources n'ont rien de comparable :

| Fenêtre | `ACQPS` | Durée/voie | Voies |
|---|---|---|---|
| rapide | 6 (7 cycles, 117 ns) | 550 ns | toutes sauf thermistances (suiveur + RC 1 k/10 nF, τ de quelques ns) |
| lente | 25 (26 cycles, 433 ns) | 866 ns | T1, T2 — haute impédance, pas de suiveur |

C'est ce **raccourcissement** qui rend possible le placement des voies
utiles dans la fenêtre propre du cycle de découpage.

**Le déclenchement est centré sur `I1` seule**, pas sur le groupe. Deux
raisons :

1. `I1` est la seule voie dont l'instant compte. VIN/V1/VOUT sont des
   tensions aux bornes de gros condensateurs derrière un RC 1 k/10 nF dont
   les 10 µs moyennent déjà sur deux périodes. IIN et IOUT passent par des
   ZXCT1109 dont le shunt est **en amont** du condensateur d'entrée —
   vérifié au scope, courant continu sans ondulation.
2. Les 4 voies rapides occupent 132 counts alors que la conduction n'en
   dure que 128 à ce rapport cyclique : le groupe **ne rentre pas** dans la
   fenêtre propre, vouloir l'y centrer n'avait pas de solution.

La version précédente centrait le groupe, ce qui rejetait `I1` 250 ns après
l'amorçage à D = 0,43, **en pleine transition**.

> `ADC_TIMING_PROBE` (`calib.h`) : instrumentation temporaire qui marque la
> durée de l'ISR ADC sur `HV_EN` et **neutralise `hv_enable_set()`**.
> **Doit valoir 0 avant tout essai en tension.** Actuellement à 0.

---

## 9. Liaison UART (`uart_link.c`)

SCI-A, GPIO28 (RX) / GPIO29 (TX), **57600 8N1**, `BRR` calculé par macro
depuis `LSPCLK = SYSCLKOUT/4`.

- Réception par ISR + **anneau circulaire 64 octets**, parsing dans la
  boucle principale
- Émission `$T` toutes les **300 ms** (l'ESP32 déclare la liaison perdue
  au-delà de 2 s)
- Émission **en tâche de fond** : `uart_link_service_tx()` pousse au plus
  4 octets (profondeur FIFO) puis rend la main — ne doit jamais retarder la
  régulation
- Checksum **XOR** entre `$` et `*`, format `%02X` **majuscules**
- Trame au checksum invalide : ignorée silencieusement
- Ligne ≤ 200 caractères ; la trame `$T` complète en fait ~175

### Trame `$T` (télémétrie)

```
$T,FREQ1=200000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,
V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,
FAULT=0,STATE=4,V1SP=200.0,VOSP=400.0,REJ=0*XX
```

**`FAULT`, `STATE`, `REJ` sont placés en tête de trame.** Le formateur
**abandonne** un champ qui ne tiendrait pas plutôt que de déborder, et un
champ absent laisse le récepteur sur sa dernière valeur : émettre `FAULT`
en fin de trame signifierait qu'un débordement le fasse disparaître, et que
l'ESP32 affiche `FAULT=0` pendant qu'un défaut réel est actif. Perdre `T2`
ou `IOUT` est sans conséquence ; perdre `FAULT` ne l'est pas.

### Trame `$C` (commande)

```
$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX
```

| Tag | Plage | Statut |
|---|---|---|
| `HT` | 0/1 | optionnel |
| `RUN` | 0/1 | optionnel, **0 par défaut au boot** |
| `V1SET` | **15 à 50 V** | optionnel |
| `VOSET` | **200 à 500 V**, ou **0 = étage 2 désactivé** | optionnel |
| `PWM1`, `PWM2` | 0/1 | acceptés, **sans effet** (historiques) |

Tous les tags sont optionnels, un tag inconnu est ignoré silencieusement —
les deux firmwares peuvent ainsi évoluer indépendamment.

**Hors bornes : rejet, jamais clamp.** La consigne précédente est
conservée, `REJ` est incrémenté, `V1SP`/`VOSP` ne changent pas. Un clamp
silencieux masquerait le refus. HT et RUN sont retenus même en cas de
rejet numérique : refuser une consigne ne doit pas empêcher un ordre
d'arrêt de passer.

`main()` part de la commande courante avant parsing (`cmd = g_last_cmd`) :
un champ absent garde sa dernière valeur, et `cmd` n'est jamais une
variable de pile non initialisée qui pourrait activer `RUN`.

> **Piège du checksum, documenté pour ne pas le reproduire.** Sur C28x,
> `uint8_t` fait **16 bits** (pas d'adressage par octet) : un cast
> `(uint8_t)` ne tronque rien. Décoder avec `strtol` sur un buffer non
> terminé lisait au-delà de la trame — `"7D"` suivi d'un résidu `"7D"`
> donnait `0x7D7D`, et **toute trame était rejetée**. Décoder exactement
> deux chiffres, de manière bornée.

---

## 10. Signalisation (`status_led.c`)

Motifs de clignotement par état, cadencés au tick 10 ms. Bleu fixe dès
`status_led_init()` : « je suis parti ».

| État | Motif | Période | Logique de conception |
|---|---|---|---|
| `STARTUP` | bleu **fixe** | — | maintenu 1 s minimum pour être visible |
| `NOMINAL` | bleu bref | 0,2 s / 2,0 s | battement lent = « tout va bien » |
| `LINK_LOST` | bleu rapide | 0,2 s / 0,4 s | reste **bleu** : transitoire, pas un défaut de puissance |
| `EMUSTOP` | alternance bleu / rouge | 0,5 s + 0,5 s | bicolore = ni tout à fait sain, ni verrouillé |
| `OVERCURRENT` | rouge **fixe** | — | le plus grave, le plus immobile |
| `OVERVOLTAGE` | rouge **rapide** | 0,15 s / 0,3 s | — |
| `OVERTEMP` | rouge **lent** | 0,5 s / 1,0 s | — |
| `UNDERVOLTAGE` | rouge **double éclat** | 2 × 0,1 s puis pause, 1,2 s | — |

Le raisonnement derrière les motifs rouges mérite d'être conservé : les
**trois cadences simples** (fixe, lent, rapide) étant déjà prises par les
trois premiers défauts verrouillés, un quatrième rythme ne serait plus
discernable à l'œil. Pour la sous-tension on a donc **changé de forme, pas
de vitesse** — deux éclats brefs suivis d'une longue pause. Les défauts
verrouillés doivent rester distinguables **sans l'IHM**.

Contrainte d'écriture : `status_led_tick()` tourne en ISR, donc **aucune
multiplication ni division**. Le compteur de phase reboucle par comparaison,
jamais par modulo — sur C28x, un `%` par une valeur qui n'est pas une
puissance de deux est une division logicielle, soit plusieurs dizaines de
cycles.

### `LED_STATE_LIMITED` — à ajouter avec le repliement (§7 bis)

Le repliement n'est pas un défaut : son motif ne doit **pas** être rouge,
sous peine de le rendre indiscernable des quatre défauts verrouillés.
L'alimentation fonctionne, donc la LED reste **bleue**.

Proposition cohérente avec la grammaire ci-dessus : **bleu double éclat**,
même forme que la sous-tension mais dans l'autre couleur — « je tourne,
mais pas librement ». La forme porte l'information (limitation), la couleur
porte la gravité (bleu = pas de défaut).

Sa place dans `compute_led_state()` : **après tous les défauts**, juste
avant `LED_STATE_NOMINAL`. Un défaut réel doit toujours l'emporter sur
l'affichage d'une limitation.

`compute_led_state()` suit **exactement le même ordre de priorité** que
`compute_fault_code()`, pour que la LED et le code remonté à l'IHM ne
puissent jamais désigner deux causes différentes. Les surtensions
détectées par `control.c` avaient été oubliées ici dans une version
antérieure : la LED restait sur le motif nominal alors que la puissance
était coupée et verrouillée — un affichage rassurant et faux, découvert au
banc **après la destruction du MOSFET par surtension à vide**.

---

## 11. Interface d'orchestration (hors firmware)

Trois couches, chacune avec un rôle strict :

- **Orchestrateur** (agent/script/humain) : propose des consignes, lit des
  résultats. Ne connaît aucune limite physique — peut demander n'importe quoi.
- **ESP32** : pont sans intelligence de sécurité. Traduit HTTP ↔ `$C`/`$T`.
  Ne clippe rien, ne valide rien au-delà du format.
- **TMS320** : **seule couche** qui applique les bornes dures.

Routes HTTP spécifiées (`docs/orchestration.md`, **pas encore confirmées
implémentées côté ESP32**) : `GET /api/telemetry`, `POST /api/setpoint`,
`POST /api/stop`.

Règles à respecter par tout orchestrateur :

- Ne considérer un point de mesure valide **qu'à `state == 4`**.
- **Relire `REJ` avant et après chaque consigne** : c'est le seul signal de
  refus. Ne jamais supposer qu'une tension stable correspond à la consigne
  demandée — un intégrateur saturé produit une valeur parfaitement stable
  mais fausse.
- **N'interrompre une campagne que sur `FAULT` 2-7.** S'arrêter sur
  n'importe quel `FAULT != 0` reviendrait à abandonner sur un simple hoquet
  UART (codes 1 et 8, auto-résolutifs).

---

## 12. État d'avancement

### Fait

- [x] Étapes 1 à 7 du plan d'implémentation (squelette, sécurité, GPIO,
      ADC, conversion physique, PWM, UART)
- [x] Boucle de régulation **fermée** (`control.c`), machine à états 0-5
- [x] Protocole `$C`/`$T` étendu (`V1SET`, `VOSET`, `RUN`, `STATE`, `V1SP`,
      `VOSP`, `REJ`)
- [x] Fréquences tranchées : étage 1 = 200 kHz, étage 2 = 100 kHz
- [x] Polarité LED confirmée (actif haut)
- [x] Sens de la formule NTC validé sur thermistances réelles

### Reste à faire

- [ ] **Repliement de puissance** (§7 bis) — spécifié, non implémenté :
      tag `LIM` dans `$T`, `LED_STATE_LIMITED`, constantes `calib.h`,
      plafonnement dans `control_tick()`
- [ ] **Documenter le code 9 côté ESP32** — fait dans `tms320_agent.md`,
      `esp32_agent.md` et `orchestration.md` ; **reste à traiter dans le
      firmware ESP32 lui-même** (affichage persistant, pas d'acquittement)
- [ ] **Réglage du gain de la boucle** — prochain travail, seulement sur
      matériel alimenté (méthode au §6)
- [ ] Reporter les gains/offsets `I1`/`I2` une fois le shunt et le réseau
      Rf/Rg mesurés à l'ohmmètre
- [ ] Test matériel de l'étape 2 (sécurité) une fois la carte puissance
      montée — critère : `TZFRC` force les sorties à 0, un breakpoint coupe
      le PWM, le flag ne se réarme pas seul
- [ ] Trancher `-O2` en configuration Debug
- [ ] HRPWM / SFO — `pwm.c` est structuré pour l'accueillir, non implémenté
- [ ] Compensation de dérive de l'oscillateur interne via le capteur de
      température interne — point d'accroche prévu, non implémenté
- [ ] Watchdog — désactivé au démarrage du projet, à réactiver

### Interdits permanents du projet

- Faire dépendre une protection du CPU, de l'ADC ou de l'UART
- Effacer automatiquement un flag de défaut latché
- Toucher aux broches JTAG (20-23)
- Mélanger driverlib et headers bitfield sur un même périphérique
- Écrire une valeur matérielle ailleurs que dans `calib.h`
- Inventer le protocole UART — `docs/ESP32-UART.md` fait foi
