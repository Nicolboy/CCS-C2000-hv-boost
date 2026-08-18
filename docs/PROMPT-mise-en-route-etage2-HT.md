# Mise en route de l'étage 2 haute tension — document de reprise

Destiné à une session Claude Code neuve. L'étage 1 est validé et fonctionnel ;
l'étage 2 (sortie 200 à 500 V) vient d'être peuplé et n'a **jamais été mis
sous tension**. La mise en route doit se faire **pas à pas**, avec vérification
à chaque palier.

## ⚡ Avertissement

Cette carte produit **400 à 500 V continus** sur un condensateur. Après coupure
de l'alimentation, ces tensions persistent **près d'une minute** sur le seul
bleeder de 1 MΩ. Le circuit de décharge active est **le seul organe qui rend
la carte manipulable**, et il n'est pas monté à la date de rédaction.

## Documents à lire avant d'intervenir

| Fichier | Contenu |
|---|---|
| `C:/ti/ccs2100/ccs/theia/resources/ai/CCS.md` | **obligatoire** avant tout outil MCP CCS |
| `hardware.md` | brochage V0.2, composants, calibration, points ouverts |
| `software.md` | architecture firmware |
| `docs/PROMPT-oscillation-grille.md` | **la panne qui a détruit un MOSFET sur l'étage 1** — à lire en entier |
| `docs/captures/INDEX.md` | relevés des captures d'oscilloscope de la campagne |
| `docs/ESP32-UART.md`, `docs/esp32_agent.md` | protocole `$C`/`$T` et état du firmware ESP32 |
| `docs/calibration-V0.1.md` | archive, seulement si une carte V0.1 est remise en service |

---

## 1. État validé de l'étage 1 — ne pas y toucher

Essai à pleine puissance du 18/08/2026, charge 42 Ω :

```
multimetre : Vin 23,7 V   I_in 2,17 A   V1 44,5 V
Pin = 51,4 W    Pout = 47,1 W    rendement = 91,7 %
```

### Constantes de mesure (V0.2, mesurées)

| Voie | Relation | Confiance |
|---|---|---|
| I1 | `(Vadc − 0,031) / 0,58` | ✅ deux routes indépendantes |
| V1 | `Vadc × 29,27` | ✅ trois points, 23 à 46 V |
| VIN | `Vadc × 10,909` | ✅ concorde au multimètre |
| IIN | `Vadc × 1,78` | ⚠️ un seul point |

### Protection étage 1

`SAFETY_ISHUNT_THRESHOLD_A = 4,0 A` crête → 2,351 V au DAC, code 729, pleine
échelle de chaîne 5,6 A. **Vérifiée sur événement réel** (déclenchement
correct, Trip Zone verrouillé).

### Correctifs matériels indispensables — la leçon la plus chère

Un MOSFET a été détruit le 18/08 par une oscillation de grille. Cause
établie : **l'entrée logique du driver n'avait aucune immunité au bruit**.

| Correctif | Étage 1 |
|---|---|
| **RC 100 Ω / 100 pF sur l'entrée du driver**, référencé à sa masse | ✅ **c'est LE correctif** |
| RC 1 kΩ / 100 pF à l'entrée de l'ampli de shunt | ✅ supprime la bosse d'établissement de ~1 µs |
| Masse du driver | ✅ **position d'ORIGINE** (masse de puissance) |
| Résistance de grille | 10 Ω, sans effet nuisible une fois l'entrée dé-glitchée |

**À NE PAS refaire sur l'étage 2** :

- **ne pas passer la masse du driver en Kelvin sur la source.** Essayé,
  inutile, et l'état intermédiaire sans RC a produit le redéclenchement qui a
  tué le MOSFET.
- **ne pas ajouter de résistance de grille avant d'avoir vérifié la forme
  d'onde.** Elle amortit une résonance LC ; contre un redéclenchement du
  driver elle ne fait qu'allonger le temps passé en régime linéaire.
- **ne pas compter sur un trigger de Schmitt à la place du RC.** L'hystérésis
  rejette en amplitude ; la perturbation faisait plusieurs volts. Seul le
  filtrage temporel fonctionne.

---

## 2. Étage 2 — paramètres et calculs

| | |
|---|---|
| Entrée | `V_inter`, 46 V |
| Sortie | 200 à 500 V (`CTRL_VOUT_SET_MIN_V` / `_MAX_V`) |
| Découpage | 100 kHz (`PWM_STAGE2_FREQ_HZ`) |
| Inductance | **440 µH** (220 + 220 en série) |
| Shunt I2 | **20 mΩ** — deux fois celui de l'étage 1 |
| Charge de conception | 10 à 15 mA sous 400-500 V, soit 4 à 7,5 W |

### Courants attendus

```
D    = 1 − 46/500 = 0,908        T = 10 µs, t_on = 9,08 µs
ΔI   = 46 × 9,08 µs / 440 µH = 0,95 A crête-à-crête

charge de conception (7,5 W) : I_in ≈ 0,18 A < ΔI/2 → DCM
                               I_crête = √(2·Pin/(L·f)) = 0,62 A
à 50 W                       : CCM, I_crête ≈ 1,7 A
```

### ⚠️ Marge de rapport cyclique très faible

`CTRL_DUTY_MAX = 0,95` et il faut déjà **0,908** à 500 V. Avec la chute de la
diode SiC et les pertes résistives, le duty réel monte encore : il reste
**environ 4 % de marge**. Si la boucle sature en butée, soit `V_inter` doit
être remontée, soit la sortie ne tiendra pas 500 V. À surveiller via `DUTY2`
en télémétrie dès les premiers paliers.

---

## 3. Bloquants avant toute mise sous tension

- [ ] 🔴 **MOSFET de décharge active monté, ≥ 600 V.** L'IRF840 500 V est
      sous-dimensionné pour une sortie qui atteint 500 V et coupe à 520 V.
      Sans lui la carte n'est pas manipulable en sécurité.
- [ ] 🔴 **RC 100 Ω / 100 pF sur l'entrée du driver de l'étage 2** (sortie de
      la porte ET IC9 → driver), référencé à la masse du driver.
- [ ] 🔴 **RC 1 kΩ / 100 pF à l'entrée de l'ampli de shunt I2.**
- [ ] Masse du driver étage 2 à sa position d'origine.
- [ ] Tenue en tension du condensateur de sortie vérifiée (≥ 600 V).
- [ ] Ohmmètre sur le shunt I2 et sur le réseau Rf/Rg de son ampli.

### 🔴 Bloquant logiciel côté ESP32

**L'ESP32 n'expose aujourd'hui aucun réglage de `VOSET`** — il est figé et
l'étage 2 reste donc désactivé (`VOSET = 0`, état par défaut au démarrage du
TMS320). Sans cette commande, l'étage 2 ne peut pas être activé du tout.

Contraintes protocole (voir `esp32_agent.md`) :

- plage acceptée : **200 à 500 V, ou 0** (0 = étage 2 désactivé) ;
- **activer ou désactiver l'étage 2 EN MARCHE est refusé** et incrémente
  `REJ`. Séquence obligatoire : `RUN=0` → nouveau `VOSET` → `RUN=1` ;
- `REJ` n'est toujours pas affiché par l'IHM : une consigne refusée est
  aujourd'hui **invisible**. À corriger avant la mise en route, sinon un
  refus passera pour une panne.

---

## 4. Voies de mesure non étalonnées — elles portent des protections

### `VOUT` — porte la coupure de survoltage à 520 V

`Vadc × 181,82`, valeur **V0.1, jamais revérifiée sur V0.2**. Une erreur de
gain déplace directement `CTRL_VOUT_OV_TRIP_V`.

**Complication** : la chute de masse décrite au §6 s'y ajoutera comme sur
`V1`. Étalonner `VOUT` avant d'avoir assaini les masses reviendrait à graver
cette erreur dans la constante.

### `I2` — porte la protection matérielle contre les surintensités

`Vadc / 0,637`, **jamais mesurée**, valeur V0.1. Le shunt fait 20 mΩ contre
10 mΩ sur l'étage 1. Si la chaîne se comporte comme celle de I1 (0,58 V/A
mesurés pour 10 mΩ), on attendrait **~1,16 V/A**.

⚠️ **`SAFETY_ISHUNT_THRESHOLD_A` est une constante UNIQUE** appliquée avec le
gain de chaque étage : les 4,0 A ne représentent pas le même courant des deux
côtés. `calib.h` annonce la tâche — *« à traiter en scindant la constante en
deux (une par étage) le jour où il le sera »*. **Ce jour est arrivé.**

**Tâche firmware due** : scinder en `SAFETY_ISHUNT_THRESHOLD_S1_A` et
`..._S2_A`, avec vérification d'écrêtage du code DAC sur chacune.

> **Pourquoi cette vérification est critique.** `DACVAL` est un champ 10 bits :
> un code > 1023 est **tronqué, pas saturé**. Sur l'étage 1, un seuil de 7,0 A
> combiné à un gain erroné demandait 3,99 V au DAC, au-delà de la référence de
> 3,3 V : le code était écrêté et le seuil placé à une tension que l'ampli
> n'atteint jamais. **La protection était muette, sans aucun signe extérieur.**
> Ne pas reproduire ça sur un étage à 500 V.

### Méthode d'étalonnage qui a fonctionné pour `I1`

Au **point de fonctionnement**, jamais par injection hors gamme — l'injection
à 10 A saturait l'ampli et a produit un gain faux de 40 %.

```
Route A (moyenne) : gain = (moyenne_C1 − offset) × Vout / (I_in × (Vout − V1))
                    moyenne = racine(DC_RMS² − AC_RMS²)   [lu sur l'instrument]

Route B (curseur) : à mi-conduction, le courant d'inductance vaut EXACTEMENT
                    le courant d'entrée de l'étage. Aucune hypothèse sur L
                    ni sur le mode de conduction.

Offset : carte alimentée, RUN=0 → aucun courant dans le shunt de source.
```

Exporter les captures en **CSV** (bouton `Export` de WaveForms), pas seulement
en PNG : lire des tensions sur des pixels a produit plusieurs erreurs pendant
la campagne de l'étage 1.

---

## 5. Comportement du firmware pour l'étage 2

Démarrage **en cascade** (`control.c`) : `V_inter` est établie et stabilisée
avant que l'étage 2 ne démarre, pour qu'il parte d'une tension d'entrée connue.

```
IDLE → START_S1 → RUN_S1 → START_S2 → RUN
```

- `VOSET = 0` → la machine s'arrête à `RUN` avec l'étage 2 inerte
  (`STATE = 4`, `VOSP = 0.0`) — **état par défaut au démarrage**
- `control_hv_allowed()` exige `CTRL_STATE_RUN` **et** étage 2 actif : `HV_EN`
  (opto VOM1271) ne peut pas s'armer avant
- un défaut de puissance est **verrouillé** : aucun acquittement, ni
  automatique ni par UART. Cycle d'alimentation obligatoire.
- l'icône danger HT de l'IHM doit dépendre **uniquement de `VOUT`**, jamais de
  `FAULT` : un défaut ne décharge pas le condensateur de sortie.

### ⚠️ `I2` sera illisible en télémétrie — structurel, pas une panne

La séquence ADC est déclenchée par **ePWM1 uniquement**
(`EPwm1Regs.ETSEL.bit.SOCASEL`), et ePWM2 tourne à 100 kHz **sans
synchronisation** (`PHSEN = TB_DISABLE`). L'échantillon `I2` tombe donc à une
phase arbitraire du cycle de l'étage 2 : n'importe quoi entre zéro et le pic,
d'une trame à l'autre.

La protection n'en dépend pas — elle est analogique et continue, elle
n'échantillonne jamais. **Ne pas chercher à corriger ça en déplaçant `CMPB`** :
un seul point de déclenchement sert les neuf voies, et le déplacer avait mis
`V1` sur le front d'extinction et fait osciller la régulation.

---

## 6. Problèmes ouverts hérités de l'étage 1

### 🔴 Chute de masse sur les ponts diviseurs analogiques

Les ponts partagent **~20 mΩ de retour de masse avec le chemin de puissance**.
L'erreur est proportionnelle au courant d'entrée :

| Courant d'entrée | Erreur `V1` ramenée à la broche |
|---|---|
| 0,97 A | +20 mV |
| 2,17 A | +51 mV (soit **+3,4 %** sur la tension affichée) |

Conséquence : la boucle affiche fidèlement sa consigne de 46,0 V pendant que
le vrai `V_inter` est à **44,5 V**, et l'écart bouge avec la charge.

**Ne pas corriger les gains pour compenser** — ils sont justes, validés à
plusieurs points. Le correctif est un **retour de masse dédié en étoile** vers
l'AGND du DSP. `VOUT` subira exactement le même défaut, et elle porte le seuil
de 520 V.

C'est le **troisième problème de masse de la campagne**. Un examen d'ensemble
du plan de masse est plus probablement la bonne réponse que trois correctifs
ponctuels.

### Écart résiduel sur le gain `I1`

Le gain recalculé sur la dernière capture donne 0,523 V/A contre les 0,58
inscrits, et l'offset d'un ajustement à deux points (83 mV) ne se réconcilie
pas avec la mesure directe à courant nul (31 mV). **À reprendre après
correction des masses**, pas avant.

### Instant d'échantillonnage de `I1`

`I1` remonte ~20 % au-dessus du courant d'entrée : l'ADC échantillonne aux
~2/3 de la conduction au lieu de la moitié, ~400 ns plus tard que programmé.
Cause non élucidée. **Ne pas comparer `I1` à `IIN`.** Sans conséquence pour la
sécurité.

---

## 7. Procédure de mise en route, pas à pas

> **Règle absolue, tirée de la destruction du MOSFET de l'étage 1 :**
> **la forme d'onde de grille se valide AVANT la montée en puissance, pas
> pendant.** Un régime linéaire de 1 µs répété à 100 kHz tue un MOSFET en
> quelques secondes — et à 500 V, pas forcément proprement.

### Palier 0 — hors tension

Contrôles du §3, plus vérification à l'ohmmètre de l'ensemble de la chaîne
étage 2 (shunt, Rf/Rg, grille non percée).

### Palier 1 — `VOSET = 0`, étage 1 seul

Confirmer que rien n'a été cassé sur l'étage 1 par le câblage de l'étage 2 :
`V1` régulée, rendement ~92 %, grille de l'étage 1 propre.

### Palier 2 — première commutation de l'étage 2, tension minimale

**Obstacle à traiter** : `CTRL_VOUT_SET_MIN_V = 200 V` interdit un démarrage
doux. Deux options, à arbitrer avec l'utilisateur :

- abaisser temporairement cette borne (par exemple à 60 V) pour la mise en
  route, avec rappel explicite de la restaurer ;
- alimenter `V_inter` depuis une alimentation de laboratoire à tension réduite.

**Limitation de courant de l'alimentation de laboratoire réglée serré.**

À vérifier avant d'aller plus loin :

- [ ] **grille de l'étage 2 : carré propre, sans effondrement, sur tous les
      cycles** — c'est le seul critère de passage au palier suivant
- [ ] pointe d'amorçage sur l'ampli de shunt, comparée au seuil du DAC
- [ ] `DUTY2` cohérent avec `1 − V1/VOUT`

### Palier 3 — étalonnage `I2`

Par la méthode du §4, au point de fonctionnement. Puis fixer
`SAFETY_ISHUNT_THRESHOLD_S2_A` sur le courant crête réel, et **vérifier le
code DAC produit** (< 1023, et atteignable dans la pleine échelle de la
chaîne).

### Palier 4 — montée par paliers de tension

100 V, puis 200, 300, 400, 500 V. À chaque palier :

- [ ] grille toujours propre
- [ ] `DUTY2` et sa marge sous `CTRL_DUTY_MAX = 0,95`
- [ ] températures (les NTC ne sont validées **qu'en sens**, pas en valeur)
- [ ] bilan de puissance cohérent — c'est le juge de paix qui a détecté
      toutes les erreurs de mesure de la campagne étage 1
- [ ] **attendre la décharge complète avant toute intervention**

### Palier 5 — `HV_EN` et charge réelle

`HV_EN` ne s'arme qu'une fois les deux étages établis et si l'opérateur l'a
demandé. Vérifier la décharge active en conditions réelles.

---

## 8. Ce qui a coûté cher sur l'étage 1, à ne pas repayer

| Piège | Coût |
|---|---|
| Deux alimentations sans masse commune | une journée de mesures invalidées |
| Gain établi par injection hors gamme (ampli saturé) | gain faux de 40 %, protection muette |
| Seuil produisant un code DAC écrêté | **protection inopérante sans aucun signe** |
| Déplacement de `CMPB` pour améliorer une voie de diagnostic | régulation déstabilisée |
| Résistance de grille contre un redéclenchement de driver | **un MOSFET détruit** |
| Lecture de tensions sur des captures d'écran | plusieurs conclusions fausses |

**Méthode qui a fonctionné, à chaque fois** : le **bilan de puissance** comme
arbitre. Un rendement supérieur à 100 % ou inférieur à 50 % désigne
immédiatement la voie fautive, sans avoir à faire confiance à un instrument
plutôt qu'à un autre.
