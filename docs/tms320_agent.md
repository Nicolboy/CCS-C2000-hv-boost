# TMS320 — contrat de protocole (agent firmware TMS320F28027)

Ce document définit ce que **le firmware TMS320 doit émettre et
accepter** sur la liaison UART vers l'ESP32. Il est la contrepartie de
`esp32_agent.md` (qui définit ce que l'ESP32 doit faire de son côté) —
les deux documents doivent rester synchronisés, un changement de contrat
d'un côté implique une revue immédiate de l'autre.

Pour le câblage physique, la calibration analogique détaillée et la
traçabilité des mesures : voir `mesure-cartepuissance.md`. Pour
l'architecture de sécurité générale : voir `PROMPT-claude-code-TMS320.md`.

---

## Ce que le TMS320 émet : trame `$T` (télémétrie)

Toutes les **300 ms**.

```
$T,FREQ1=200000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,FAULT=0,STATE=4,V1SP=200.0,VOSP=400.0,REJ=0*XX
```

| Tag | Unité | Description |
|---|---|---|
| `FREQ1` | Hz | Fréquence de découpage étage 1 (200 kHz) |
| `FREQ2` | Hz | Fréquence de découpage étage 2 (100 kHz) |
| `DUTY1` | % | Rapport cyclique étage 1 |
| `DUTY2` | % | Rapport cyclique étage 2 |
| `VIN` | V | Tension d'entrée |
| `IIN` | A | Courant d'entrée |
| `V1` | V | Tension intermédiaire (sortie étage 1) |
| `I1` | A | Courant shunt MOSFET étage 1 |
| `T1` | °C | Température NTC étage 1 |
| `VOUT` | V | Tension de sortie HT |
| `I2` | A | Courant shunt MOSFET étage 2 |
| `T2` | °C | Température NTC étage 2 |
| `IOUT` | A | Courant de sortie |
| `FAULT` | code | Défaut courant — voir table ci-dessous |
| `STATE` | code | État de la machine de régulation — **valeurs à confirmer**, voir note |
| `V1SP` | V | Consigne V1 **effectivement acceptée** (pas forcément égale à la dernière demandée si rejet) |
| `VOSP` | V | Consigne VOUT effectivement acceptée, même remarque |
| `REJ` | compteur | Nombre cumulé de consignes rejetées depuis le boot |
| `LIM` | code | **Repliement de puissance actif** — voir section dédiée. **À implémenter**, absent du firmware actuel |

Tous les champs restent optionnels à l'émission individuelle (un champ
absent garde sa dernière valeur côté ESP32), mais en pratique le firmware
les envoie tous à chaque trame.

### `STATE` — table complète (confirmée sur le firmware)

| Code | État | Signification |
|---|---|---|
| `0` | IDLE | tout coupé, décharge active, en attente de `RUN=1` |
| `1` | START_S1 | étage 1 en montée, consigne rampée depuis la tension mesurée |
| `2` | RUN_S1 | V_inter établie, étage 2 encore à zéro |
| `3` | START_S2 | étage 2 en montée |
| `4` | RUN | régulation établie — **seul état où un point de mesure est exploitable** |
| `5` | FAULT | état sûr verrouillé |

Deux pièges à connaître avant de s'appuyer sur ce champ :

**`STATE=2` ne dure qu'un seul pas de régulation, soit ~200 µs.** Aucun
interrogateur cadencé à la seconde ne l'observera jamais. Ne pas écrire de
logique qui l'attend ou qui suppose l'avoir manqué à tort.

**`STATE=4` ne signifie pas toujours « les deux étages établis ».** En mode
étage 1 seul (`VOSET=0`, voir plus bas), la machine passe directement de
`1` à `4`, et `4` veut alors dire « étage 1 établi ». Le seul moyen de
distinguer les deux cas est `VOSP` : nul en mode étage 1 seul.

### Ordre des champs : `FAULT`, `STATE`, `REJ` en tête de trame

La trame complète fait environ **175 caractères sur les 200** autorisés, et
le formateur côté TMS320 **abandonne** un champ qui ne tiendrait pas plutôt
que de déborder. Comme un champ absent laisse le récepteur sur sa dernière
valeur connue, émettre `FAULT` en fin de trame signifierait qu'un
débordement le fasse disparaître — et que l'ESP32 continue d'afficher
`FAULT=0` pendant qu'un défaut réel est actif.

L'état de sécurité passe donc avant les mesures : perdre `T2` ou `IOUT` est
sans conséquence, perdre `FAULT` ne l'est pas.

### `FAULT` — table complète

| Code | Signification | Nature |
|---|---|---|
| `0` | Aucun défaut | — |
| `1` | EMUSTOP — le débogueur a arrêté le CPU | **transitoire**, se relève seul |
| `2` | Surintensité I1 (étage 1) | verrouillé |
| `3` | Surintensité I2 (étage 2) | verrouillé |
| `4` | Température critique T1 (> 80 °C) | verrouillé |
| `5` | Température critique T2 (> 80 °C) | verrouillé |
| `6` | Survoltage V_inter (> 55 V) | verrouillé |
| `7` | Survoltage sortie HT (> 520 V) | verrouillé |
| `8` | Liaison UART perdue | **transitoire**, se relève seul |
| `9` | **Sous-tension d'entrée VIN (< 9,5 V)** | **verrouillé** |

Les codes 6, 7 et 9 sont détectés dans l'ISR ADC par comparaison directe
sur la valeur brute, sans conversion en volts — les seuils sont pré-calculés
en counts à la compilation.

### `9` — sous-tension d'entrée

Motif : un élévateur compenserait une entrée qui s'effondre en augmentant
le rapport cyclique, donc le courant, **jusqu'à la surintensité**. La
coupure intervient avant.

Trois comportements que l'IHM et l'orchestrateur doivent connaître :

- **La surveillance ne s'arme qu'après un premier passage de VIN au-dessus
  de 12 V.** Au démarrage, VIN traverse forcément la zone basse pendant la
  montée de l'alimentation ; surveiller dès le reset rendrait la carte
  impossible à démarrer. Une fois armée, elle le reste jusqu'au reset.
- **Seuil à 9,5 V, soit 0,5 V sous le minimum de conception (10 V)** : à
  10 V pile, l'ondulation d'entrée et le creux d'un échelon de charge
  feraient tomber un défaut dont on ne sort qu'en coupant l'alimentation.
- **Anti-rebond de 5 séquences ADC** (75 µs, quinze périodes de découpage).

C'est un défaut **verrouillé** : comme les codes 2 à 7, il exige un cycle
d'alimentation. Une IHM qui le traiterait comme transitoire attendrait
indéfiniment un retour spontané à `FAULT=0`.

**Priorité entre défauts simultanés, telle qu'implémentée :**

```
2 > 3 > 6 > 7 > 9 > 4 > 5 > 8 > 1
```

Les codes 6, 7 et 9 proviennent de la même variable interne et s'excluent
mutuellement ; leur ordre relatif est celui de la cascade de tests de
l'ISR ADC.

Les survoltages passent donc **avant** les surtempératures, et non après
comme une révision antérieure de ce document le supposait. Pour la question
restée ouverte sur les deux transitoires : **`8` prime sur `1`**.

**Il n'existe volontairement pas de code « court-circuit » séparé.** Un
court-circuit franchit le même comparateur et le même seuil qu'une
surintensité ; les codes 2 et 3 couvrent les deux cas.

**Verrouillage — pas d'effacement par le protocole.** Un défaut
verrouillé (2-7) ne s'efface jamais automatiquement, ni à la disparition
de sa cause, ni par commande UART. Aucun tag `$C` ne permet de
l'acquitter. Le seul retour à `FAULT=0` sur un code verrouillé est un
cycle d'alimentation complet — choix délibéré : un défaut de puissance
doit exiger une intervention humaine.

> Rappel structurel : la protection réelle (comparateurs + Trip Zone
> matériel) ne dépend jamais de cette liaison UART. `FAULT` est un simple
> report d'information, envoyé après coup — la coupure a déjà eu lieu en
> matériel avant même que la trame ne soit construite.

---

## Repliement de puissance — tag `LIM` (**à implémenter**)

Le TMS320 plafonne la puissance qu'il tire de l'entrée, selon la tension
d'entrée :

| Condition | Plafond | `LIM` |
|---|---|---|
| Pas de limitation active | — | `0` |
| `VIN > 20 V` | **50 W** | `1` |
| `VIN ≤ 20 V` | **25 W** | `2` |

### Ce n'est PAS un défaut, et c'est tout l'enjeu de ce tag

Le repliement **laisse la régulation tourner**. Rien n'est coupé, rien
n'est verrouillé, `FAULT` reste à `0` et `STATE` à `4`. Il n'occupe donc
volontairement **aucun code `FAULT`** : un consommateur qui interromprait
son traitement sur `FAULT != 0` s'arrêterait alors que l'alimentation
fonctionne — de façon bridée, mais nominale.

La conséquence est symétrique et il faut la voir : **sans lire `LIM`, un
point de mesure plafonné est rigoureusement indiscernable d'un point
libre.** `VOUT` sera stable, `STATE` vaudra `4`, `FAULT` vaudra `0`, et la
tension pourra pourtant rester sous sa consigne parce que le duty est bridé
— pas parce que la boucle est mal réglée. C'est exactement le genre de
point qui pollue une courbe de rendement sans laisser de trace.

`LIM` est donc placé **en tête de trame** avec `FAULT`, `STATE` et `REJ` :
sa disparition par troncature ferait croire à un fonctionnement libre.

### Pourquoi 20 V, et pourquoi un facteur 2

Le seuil est le **pendant logiciel d'une limite matérielle** déjà connue :
la chaîne de mesure de courant sature à 4,04 A et le seuil de protection
matériel est à 3,5 A **sur le courant crête**, pas moyen.

| Vin | Plafond | I_moyen | I_crête | Marge sous 3,5 A |
|---|---|---|---|---|
| 22,4 V | 50 W | 2,23 A | 2,84 A | 19 % |
| 20,0 V | 50 W | 2,50 A | ~3,10 A | 11 % |
| 19,9 V | 25 W | 1,26 A | ~1,86 A | 47 % |
| 10,0 V | 25 W | 2,50 A | 2,92 A | 17 % |

Sans repliement, 50 W à 15 V donnent 4,04 A de crête — **au-dessus du
seuil**, donc un déclenchement à chaque tentative ; et à 10 V, 5,68 A, soit
au-delà de ce que la chaîne sait mesurer. Le repliement rend la plage
d'entrée complète exploitable au lieu de la limiter à ~19 V.

La discontinuité à 20 V est assumée : elle place le courant crête très bas
juste sous le seuil, ce qui est le comportement sûr.

### Comportement attendu du consommateur

- **Ne jamais traiter `LIM != 0` comme une erreur.** Ce n'est pas un défaut,
  et aucune campagne ne doit s'interrompre dessus.
- **Toujours annoter un point de mesure avec la valeur de `LIM`.** Un point
  relevé sous plafond n'est pas comparable à un point libre.
- **Ne pas déduire `LIM` de `VIN`** côté ESP32 ou orchestrateur, même si la
  règle paraît simple : l'hystérésis sur le seuil de 20 V rend la valeur
  dépendante de l'historique, pas seulement de `VIN` instantané. Le TMS320
  reste la seule source de vérité, comme pour les bornes de consigne.

---

## Ce que le TMS320 accepte : trame `$C` (commande)

```
$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX
```

| Tag | Valeurs | Statut | Description |
|---|---|---|---|
| `HT` | 0 ou 1 | optionnel | Sortie HT activée/désactivée |
| `PWM1` | 0 ou 1 | optionnel, **accepté mais sans effet** | La régulation pilote les étages elle-même |
| `PWM2` | 0 ou 1 | optionnel, **accepté mais sans effet** | idem |
| `V1SET` | float | optionnel | Consigne tension intermédiaire, **15 à 50 V** |
| `VOSET` | float | optionnel | Consigne sortie HT, **200 à 500 V**, ou **0 = étage 2 désactivé** |
| `RUN` | 0 ou 1 | optionnel | Autorise la régulation à suivre `V1SET`/`VOSET` |

Tous les tags sont désormais optionnels, y compris `HT` — un tag inconnu
est ignoré silencieusement. Ce comportement permet aux deux firmwares
d'évoluer indépendamment sans se bloquer mutuellement sur une trame
incomplète.

### `VOSET=0` : mode étage 1 seul

`VOSET` **exactement nul** n'est pas une consigne de 0 V, c'est la
convention qui **désactive l'étage 2** :

- la machine s'arrête à `STATE=4` dès que l'étage 1 est établi, sans jamais
  passer par `2` ni `3` ;
- le rapport cyclique de l'étage 2 est forcé à zéro à chaque pas, sa sortie
  ePWM reste inhibée (`AQCSFRC`) et sa porte ET n'est pas armée ;
- **`HT=1` reste sans effet** : il n'y a pas de sortie HT à mettre sous
  tension. `VOSP` renvoie `0.0`.

Ce mode existe parce que sans lui la machine resterait bloquée
indéfiniment en `STATE=3`, l'intégrateur saturé à 95 % : une sortie ne peut
pas atteindre 200 V quand le MOSFET, la diode et l'inductance de l'étage 2
ne sont pas montés. C'est **l'état par défaut au démarrage** du TMS320 —
le défaut le plus sûr est de ne rien commander sur un étage absent.

Zéro est sans ambiguïté : ce n'est pas une consigne plausible, et toute
valeur strictement comprise entre 0 et 200 reste refusée comme avant. Une
valeur négative est refusée elle aussi.

**Basculer entre les deux modes exige `RUN=0` d'abord.** Une trame qui
activerait ou désactiverait l'étage 2 alors que la machine tourne est
**refusée** (`REJ` s'incrémente) : l'activer en marche ferait démarrer
l'étage 2 avec un intégrateur et une rampe hors contexte, donc par un
à-coup de rapport cyclique ; le désactiver couperait la sortie sans passer
par l'état sûr. La séquence correcte est `RUN=0`, puis le nouveau `VOSET`,
puis `RUN=1`.

### Comportement hors bornes : **rejet, jamais clamp**

Une valeur `V1SET`/`VOSET` hors de sa plage est **refusée en bloc** :

- la consigne précédemment acceptée est conservée telle quelle
- `REJ` est incrémenté
- `V1SP`/`VOSP` (dans la télémétrie) ne changent pas

Ce choix est délibéré : un clamp silencieux masquerait le refus et
laisserait croire qu'une valeur a été appliquée alors que ce n'est pas le
cas. C'est à `V1SP`/`VOSP`/`REJ` de porter cette information, jamais à un
comportement de clamp implicite.

`RUN` démarre à `0` par défaut au boot : aucune régulation ne s'engage
tant qu'il n'a pas été explicitement mis à `1`.

Un `FAULT` verrouillé (2-7) interrompt immédiatement la régulation
(retour à l'état sûr, PWM inhibés) **indépendamment de l'état de `RUN`**.
Les `FAULT` transitoires (1, 8) ne doivent pas interrompre une régulation
en cours au-delà de leur propre durée.

---

## Circuit de décharge active — rôle du TMS320

Piloté sur **GPIO5 / broche 40**, logique inversée, via un pont résistif
référencé à la HT elle-même (pas une pull-down sur le rail logique
3,3 V) : le MOSFET de décharge est rendu passant dès que GPIO5 ne pilote
plus activement un état bas — CPU planté, halte debug, ou alimentation
logique du TMS320 coupée alors que la HT est encore présente. Ce
failsafe ne dépend à aucun moment du rail 3,3 V.

Conséquence pour le firmware : **ne jamais initialiser GPIO5 dans un état
qui bloquerait la décharge** avant que le système ait explicitement
validé un état sûr. L'état par défaut au reset (avant toute écriture
logicielle) doit laisser le pont reprendre la main.

---

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), hexadécimal majuscule
sur 2 chiffres (`%02X`).

> **Piège déjà rencontré, documenté pour ne pas le reproduire.** Décoder
> le checksum avec `strtol` sur un buffer non terminé par `\0` fait lire
> au-delà de la trame. Et sur C28x, `uint8_t` fait 16 bits (pas
> d'adressage par octet) : un cast `(uint8_t)` ne tronque rien, et un
> `"7D"` suivi d'un résidu `"7D"` donne `0x7D7D`, conservé tel quel —
> toute trame était alors rejetée. Décoder **exactement deux chiffres**,
> de manière bornée.

---

## Calibration analogique (résumé — détail complet dans `mesure-cartepuissance.md`)

| Grandeur | Relation | Statut |
|---|---|---|
| `VIN` | `VIN = Vadc x 11,11` | mesuré |
| `IIN` | `IIN = Vadc x 1,2019` | mesuré (1 point seulement) |
| `V1` | `V1 = Vadc x 31,25` | mesuré |
| `VOUT` | `VOUT = Vadc x 181,82` | mesuré |
| `IOUT` | `IOUT = Vadc x 0,016667` | **jamais mesuré** |
| `I1` | `I1 = (Vadc - 0,0473) / 0,631` | provisoire (ampli MCP6001 de banc, à refaire avec TLV9151) |
| `I2` | `I2 = (Vadc - 0,0340) / 0,637` | provisoire, idem |

### `I1` et `I2` ne sont **pas** des courants moyens — ne jamais les utiliser pour un calcul de puissance

Les shunts sont dans la **source des MOSFET** : ils mesurent le courant de
commutation, pas le courant de sortie de l'étage. Et l'ADC échantillonne à
un instant fixe du cycle (déclenchement à `CTR=ZERO`, `I1` converti environ
3,5 µs plus tard, soit près du **pic** du courant d'inductance à D = 0,8) —
c'est un échantillon instantané, pas une moyenne.

Conséquence : `V1 × I1` n'a **pas** la dimension d'une puissance de sortie
d'étage. Ces deux voies existent pour la protection et le diagnostic, pas
pour la mesure de rendement.

Avec une charge résistive connue `R` sur V_inter, le rendement de l'étage 1
se calcule sans `I1` du tout, à partir de grandeurs bien calibrées :

```
rendement_etage1 = (V1² / R) / (VIN × IIN)
```

Ce calcul suppose l'étage 2 désactivé (`VOSET=0`), faute de quoi l'étage 2
consomme lui aussi sur V_inter et le bilan est faussé.

NTC : `R_ntc = R_fixe x (VREF - Vadc) / Vadc`, seuil de coupure 80 °C
provisoire (thermistances à 5 mm des MOSFET, mesurent le cuivre, pas la
jonction).

---

## Points ouverts — état

Les quatre points ouverts de la révision précédente sont **traités** :

1. ~~Signification des codes `FAULT` 6 et 7~~ → survoltages, table ci-dessus.
2. ~~Table complète de `STATE`~~ → six états documentés, avec les deux
   pièges (`2` transitoire, `4` ambigu en mode étage 1 seul).
3. ~~Priorité entre `FAULT=1` et `FAULT=8`~~ → `8 > 1`, ordre complet
   `2 > 3 > 6 > 7 > 4 > 5 > 8 > 1`.
4. `IOUT` **reste à mesurer** — confirmé. La valeur de `calib.h` est
   théorique, jamais vérifiée sur la carte.

### Ce qui reste réellement ouvert côté matériel

- `IOUT` jamais mesuré, `IIN` calibré sur un seul point avec offset non
  vérifié : **aucune courbe de rendement système n'est fiable** tant que
  ces deux voies ne sont pas reprises. Le rendement de l'étage 1 seul,
  lui, est calculable dès maintenant (formule plus haut).
- `I1`/`I2` à recalibrer **entièrement, offset compris**, après le
  remplacement des MCP6001 par des TLV9151.
- MOSFET de décharge IRF840 en 500 V alors que la sortie peut atteindre
  500 V et couper à 520 V : **à remplacer par un 600 V** avant toute mise
  sous tension de l'étage 2.
- Sens de la formule NTC à valider avec une résistance asymétrique
  (4,7 kΩ ≈ 47 °C) : 10 k/10 k ne permet pas de discriminer.

### Ce qui reste ouvert côté firmware — réglage de la boucle

**Le gain de la régulation n'est pas réglé.** C'est le prochain travail,
et il ne peut se faire que sur matériel alimenté.

État actuel de la loi de commande (`calib.h`) :

| Constante | Valeur | Rôle |
|---|---|---|
| `CTRL_SHIFT` | `12` | gain intégral = `1 / 2^12` par pas |
| `CTRL_DECIM` | `13` | 1 pas de régulation toutes les 13 séquences ADC, soit ~5,1 kHz |
| `CTRL_RAMP_V1_V_PER_STEP` | `0,01` | ~51 V/s sur l'étage 1 |

`CTRL_SHIFT = 12` a été choisi **délibérément très bas**, sans aucun
modèle du convertisseur : le boost à fort gain présente un zéro dans le
demi-plan droit qui limite la bande passante atteignable, et démarrer trop
raide risquait l'oscillation dès le premier essai. La réponse sera donc
lente — c'est voulu, pas un défaut à corriger à l'aveugle.

**Méthode de réglage, une fois l'étage 1 alimenté :**

1. Observer au scope la réponse de V_inter à un échelon de consigne
   (`V1SET` de 20 à 25 V par exemple), avec la charge résistive en place.
2. Diminuer `CTRL_SHIFT` **par paliers d'une unité** — chaque unité double
   le gain. S'arrêter dès qu'un dépassement ou une oscillation apparaît,
   puis remonter d'un cran.
3. Le terme **proportionnel** ne vient qu'après, une fois l'intégrateur
   réglé : `duty = (accum >> CTRL_SHIFT) + (erreur >> CTRL_KP_SHIFT)`.
   Un décalage supplémentaire, toujours sans multiplication ni division —
   la discipline d'interruption du projet reste valable.

**Point connu qui limitera le résultat en sortie HT** : à 500 V depuis
35 V il faut D = 0,93, et `dV/dD = Vin/(1-D)²` donne alors **1 LSB de
rapport cyclique ≈ 12 V** de quantification en sortie. La régulation
oscillera irréductiblement entre deux valeurs adjacentes tant que le
HRPWM/MEP n'est pas utilisé (~93 sous-pas par cycle SYSCLK, soit 14,8 bits
au lieu de 8,2 — nécessite la bibliothèque SFO, non implémentée). Ce point
ne concerne **pas** l'étage 1, où la quantification reste fine.

Autre décision en suspens : la configuration « Debug » compile en `-O2`.
À trancher avant de considérer le firmware figé.
