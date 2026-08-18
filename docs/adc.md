# ADC — TMS320F28027 en environnement bruité

Fiche de référence projet. Boost 15 V → 500 V, 50 W, PWM 100–500 kHz.
Architecture 2 cartes : commande (TMS320F28027 + ESP32) et puissance.

Objectif : maximiser l'ENOB réel de l'ADC 12 bits intégré en environnement dv/dt élevé.
**Priorité : robustesse EMI, pas précision absolue.**

---

## 0. Décisions d'architecture arrêtées

| Décision | Choix | Raison |
|---|---|---|
| Référence ADC | **Interne** (dérivée de VDDA) | Précision absolue non requise ; libère ADCINA0/A1 |
| Régulation | **Tout linéaire** (LM317 + AP2210) | Pas de second nœud de commutation → pas de battement avec le boost |
| Alimentations | **Indépendantes par carte** | Évite les défauts en cascade ; permet le bring-up séparé |
| Ratiométrique | **Abandonné** | Rails distincts entre cartes ; erreur NTC < 1 °C, acceptable |
| Masse | **Un seul point de liaison**, via connecteur | Inchangé malgré les régulateurs séparés |

### Règles d'or
1. Le bruit du boost est **déterministe et synchrone du PWM** → on ne le filtre pas, on l'**évite** en choisissant l'instant d'échantillonnage.
2. **Rien de haute impédance ne traverse le câble inter-cartes** (exception encadrée : NTC, cf. §7).
3. **Un seul plan de masse** par carte, jamais fendu. Partitionnement par le placement.
4. Traiter le bruit **à la source** (snubber, boucle chaude, R de grille) plutôt qu'en aval.
5. Régulateurs indépendants ⇒ **tout signal inter-cartes doit survivre à l'alimentation asymétrique**.

Ordre de priorité si arbitrage :
1. Synchronisation SOC ↔ PWM
2. Conditionnement basse impédance + charge bucket
3. Propreté de VDDA (= la référence, en mode interne)
4. Plan de masse continu + placement
5. Snubber / boucle chaude / boucle de grille
6. Moyennage logiciel synchrone

---

## 1. Arbres d'alimentation

### Carte commande

```
15 V ─┬─→ LM317 5 V ─┬─→ ESP32 (module)
      │              ├─→ AP2210 3.3 V → VDD  (TMS320 + logique)
      │              └─→ AP2210 3.3 V → VDDA (+ ampli-op locaux)
      │                  [ferrite + 10 µF en amont de chaque AP2210]
      └─→ (rien d'autre)
```

**Pourquoi les deux 3.3 V depuis le même 5 V, par deux AP2210 identiques :**
- Séquencement : les deux rails tracent ensemble (même source, même composant, même C_out).
  Un LM317 3.3 V direct depuis 15 V monterait bien avant VDDA → violation de la
  fenêtre VDDA/VDDIO (±0.3 V typ.) à chaque power-up.
- Isolation : le seul chemin commun est le 5 V, rejeté par chaque AP2210 séparément.
- Thermique : supprime ~0.9 W de dissipation inutile.

**À faire :**
- [ ] C_out **identiques** sur les deux AP2210
- [ ] Clamp **BAT54S** entre VDD et VDDA (borne l'écart à ±0.3 V au power-down)
- [ ] Ferrite + 10 µF entre le 5 V et chaque entrée AP2210 (isole les rafales WiFi de l'ESP32)
- [ ] LM317 5 V : **TO-220 + dissipateur ou DPAK avec cuivre** — 10 V × ~120 mA = 1.2 W
      moyens, **3.5 W en crête WiFi**
- [ ] LM317 : bypass ADJ 10 µF (+15–20 dB de réjection) + 2 diodes 1N4148 de protection
- [ ] Placement : LDO numérique en zone numérique, LDO analogique en zone analogique.
      Ne pas les aligner au centre de la carte.

> **⚠ À VÉRIFIER — forme du module ESP32.** Un WROOM nu s'alimente en 3.3 V, pas en 5 V
> (seuls les DevKit acceptent 5 V sur VIN). Si module nu : 15 V → 3.3 V @ 350 mA en
> linéaire = 4 W, intenable. Dans ce cas, **buck dédié pour l'ESP32**, confiné dans un
> coin de la carte avec son propre plan. L'ESP32 est déjà un émetteur radio à SMPS
> interne : autant regrouper les sources de bruit numérique.

### Carte puissance

```
15 V ─┬─→ LM317 10 V ──→ drivers de grille  [+ bulk local 10 µF // 100 nF au driver]
      │
      └─→ LM317 3.3 V ─→ 3.3 VA-PWR : ampli shunt, buffers, excitation NTC
          [ferrite + 10 µF en amont]
```

**En parallèle, pas en cascade.** Le rail driver délivre des impulsions de plusieurs
ampères en quelques nanosecondes : c'est le rail le plus bruyant du montage. La PSRR
d'un LM317 est nulle dans cette bande — dériver l'analogique de là annulerait tout le
reste du travail.

Dissipation : 5 V × ~30 mA = 0.15 W (driver) ; 11.7 V × ~20 mA = 0.23 W (analogique).
Sans souci.

> **⚠ À VÉRIFIER — affaissement du 15 V.** Ce rail est l'entrée du boost et tire ~3.5 A.
> S'il descend à 12 V en charge, le LM317 10 V n'a plus que 2 V de marge, **sous le
> dropout pire cas (~2.5 V)** : le rail de grille décroche exactement au moment de forte
> charge. Mesurer le 15 V réel en pleine charge, et retomber sur **9 V** si nécessaire.

> **⚠ À VÉRIFIER — V_gs requis par le MOSFET.** 10 V convient à un superjonction Si
> (R_ds(on) spécifié à 10 V). **Ne convient pas** au SiC (18–20 V) ni au GaN (plafond
> ~6.5 V — destruction). Confirmer la techno et la courbe R_ds(on) = f(V_gs) de la
> référence exacte.

**Nommage :** `3.3 VA-PWR` (carte puissance) ≠ `VDDA` (carte commande). Deux rails
analogiques distincts, sans relation. C'est la conséquence directe des régulateurs
indépendants.

### Isolation de défaut

Les régulateurs sont indépendants, mais le 15 V reste commun. Pour que l'indépendance
soit réelle :
- [ ] **Diode série ou load switch** sur l'entrée 15 V de la carte commande → un
      effondrement côté puissance est tamponné par le bulk local quelques ms, assez pour
      logger le défaut et couper le PWM
- [ ] **Fusible / PTC** dédié par branche
- [ ] **TVS** sur l'entrée 15 V de la carte commande (ce fil traverse la zone de puissance)

---

## 2. Contraintes du composant

| Élément | Valeur / règle |
|---|---|
| Résolution | 12 bits, ~4.6 MSPS |
| Plage d'entrée | 0 – 3.3 V (max VDDA + 0.3 V) |
| Modèle d'entrée | Capacité commutée : quelques kΩ de switch, ~1–2 pF de C_hold |
| Impédance de source cible | **≤ 50 Ω** en pied de broche, sinon allonger `ACQPS` |
| Référence | **Interne** → la pleine échelle suit VDDA |

**Conséquence de la référence interne : la propreté de VDDA *est* la qualité de la
référence.** Le bruit sur ce rail se traduit directement en bruit de code, sans
atténuation. L'AP2210 VDDA est le composant le plus critique de la chaîne.

### Obligations firmware
- **Calibration TI au boot** (`Device_cal()` / trim ADC depuis l'OTP). Sans elle,
  offset et gain hors spec. Non négociable.
- Vérifier l'**errata F2802x** (première conversion / EOC) avant de figer le séquenceur.
- Broches analogiques inutilisées → AGND, jamais flottantes.
- Découplage VREFHI/VREFLO : vérifier la note TI F2802x — certains modes en attendent
  malgré la référence interne. À trancher **avant** routage.

---

## 3. Synchronisation ADC ↔ PWM

Le levier le plus efficace. La majorité des bits se gagne ici.

- SOC déclenché **depuis l'ePWM** (SOCA/SOCB sur CMPB ou CTR=PRD).
  Jamais par timer asynchrone, jamais en polling.
- Fenêtre d'échantillonnage **loin des fronts** : milieu de la phase ON pour le courant
  inductance, ou juste avant le front de commande.
- **Blanking 100–200 ns** après chaque front.
- À 500 kHz (période 2 µs) la fenêtre utile devient très étroite. Si la qualité de mesure
  prime, rester vers **100–200 kHz**.
- `ACQPS` : le plus long que le budget temporel permet. **À rallonger** compte tenu des
  1 kΩ de protection d'interface (cf. §4).
- **Moyennage synchrone** : 2–4 SOC sur le même canal dans la fenêtre calme, moyenne
  software. Préférable à un RC agressif, qui ajoute du retard dans la boucle.

### Séquenceur
- Ne pas placer un canal haute impédance juste après un canal à forte dynamique
  (diaphonie via C_hold).
- Parade : conversion dummy intercalée, ou répétition du canal critique.
- Symptôme d'un problème de ref/séquencement : **le code d'un canal dépend du canal
  converti juste avant**.

---

## 4. Interfaces inter-cartes — protection contre l'alimentation asymétrique

**Le scénario le plus dangereux** : carte puissance alimentée, carte commande non. Les
sorties d'ampli injectent dans les broches ADC du MCU éteint via ses diodes ESD →
latch-up, ou back-powering laissant le MCU dans un état zombie avec des PWM
indéterminées alors que la puissance est vivante.

Chaîne obligatoire sur **chaque** signal analogique inter-cartes :

```
[ampli côté puissance] → R 1 kΩ → [connecteur] → R 100 Ω → [pin ADC]
                                                     |
                                             C 1–10 nF → AGND (retour court)
```

- **R 1 kΩ côté puissance** : limite l'injection à quelques centaines de µA, sous le
  seuil de latch-up. Protège aussi l'ampli.
- **Clamp BAT54S** (vers 3.3 VA et AGND) sur les entrées les plus exposées.
- Compensation du 1 kΩ : `ACQPS` allongé **et** charge bucket porté à **10 nF**.
  Alternative propre si la place le permet : buffer côté commande, après la résistance.

**Côté drivers :**
- [ ] **Pull-down sur les entrées de commande** des drivers, côté carte puissance
- [ ] **Pull-down 10 kΩ grille-source** au plus près du MOSFET — garantit le blocage tant
      que le rail 10 V n'est pas établi. Critique avec des régulateurs indépendants.

**Scénario inverse** (commande seule alimentée) : les entrées ADC lisent zéro, ce qui
ressemble à un fonctionnement normal. Détection explicite obligatoire, cf. §8.

---

## 5. Conditionnement des entrées

Schéma type, côté carte commande :

```
Source (déjà bufferisée) → R 100 Ω → [pin ADC]
                                |
                        C 10 nF → AGND (retour court)
```

- **Charge bucket** : ≥ 20–50 × C_hold. C0G/NP0. 10 nF ici vu les 1 kΩ amont.
- Filtre RC = anti-repliement **et** réjection du résidu de découpage.
  - f_c ≈ Fs/5 à Fs/10
  - Tension de sortie : quelques dizaines de kHz
  - Courant : plus large (attention au retard dans la boucle)

### Mesure du 500 V
- Pont diviseur **en plusieurs résistances série** (ex. 4 × 1 MΩ, 1206), distances de
  fuite respectées, fente fraisée sous le pont.
- Z_Thévenin énorme → **buffer obligatoire** (OPA320, MCP6V, TLV9061 : rail-to-rail E/S).
- **Jamais d'attaque directe de l'ADC par un pont haute impédance.**

### Mesure de courant
- Shunt en **Kelvin (4 fils)**.
- Ampli différentiel (INA240, INA181) **sur la carte puissance**, au plus près du shunt,
  alimenté en **3.3 VA-PWR**.
- Bénéfice du 3.3 V : saturation naturelle sous VDDA → aucun risque de surtension sur
  l'entrée ADC.
- Contrainte : plage de sortie utile ~0.1–3.2 V. Dimensionner le shunt pour que le
  courant crête donne ~3 V.

> **⚠ À VÉRIFIER — référence de l'ampli de shunt.** V_supply max de l'INA240 / INA181 =
> 5.5 V. Confirmer avant de figer le rail.

---

## 6. Layout

### Carte commande — 4 couches minimum

| Couche | Contenu |
|---|---|
| L1 | Composants + signaux courts |
| L2 | **Plan de masse continu, jamais découpé** |
| L3 | Plans d'alimentation (5 V, VDD, VDDA) |
| L4 | Signaux + cuivre de garde |

- Un seul plan de masse. Partitionnement **par placement** : analogique d'un côté ;
  MCU, ESP32, JTAG, comm de l'autre. Un plan fendu crée des boucles pires que le problème
  qu'il prétend résoudre.
- Aucune piste analogique ne traverse une discontinuité de L2, ni ne passe sous le
  quartz, le JTAG, les sorties PWM ou **l'antenne de l'ESP32** (keep-out à respecter).
- Pistes ADC : courtes, sur L1, cuivre de garde de part et d'autre, stitching tous les
  5–10 mm.

### Découplage MCU

| Rail | Traitement |
|---|---|
| VDDIO 3.3 V | 100 nF par broche, via de masse sous le pad |
| VDD 1.8 V (VREG interne) | C_out selon TI (typ. 1.2–2.2 µF céramique) — cause classique d'instabilité |
| **VDDA** | **1 nF + 100 nF + 10 µF** (dans cet ordre de proximité), retour VSSA court et direct |
| VSSA | Rejoint le plan en **une zone unique**, sous la partie analogique |
| XRS | RC de reset + 100 nF ; surveiller les glitches dus aux dv/dt |

Le **1 nF est le plus important** : il absorbe le contenu HF injecté par capacité
parasite depuis le nœud de commutation.

### Carte puissance
- **Minimiser la boucle chaude** (MOSFET + diode/synchro + C_out). Boucle plane, retour
  immédiatement sous l'aller. C'est elle qui rayonne.
- Nœud de commutation (500 V, dv/dt élevé) = antenne capacitive : cuivre **minimal**,
  jamais en regard de la carte de commande.
- **Snubber RC ou RCD**. 10 dB gagnés ici valent tous les filtres analogiques en aval.
- Zone HT : distances de fuite ≥ 3–4 mm, fentes fraisées, vernis.

### Boucle de grille — plus important que le régulateur lui-même
- **Boucle driver → grille → source → driver minimale.** Plusieurs ampères en quelques ns.
- **Retour de masse du driver ramené directement à la source du MOSFET**, piste dédiée,
  jamais via le plan de puissance (l'inductance commune injecte dans V_gs → ré-amorçages).
- **Bulk local** : 10 µF céramique + 100 nF **collés au driver**. Le LM317 ne voit que la
  moyenne (I = Q_g × V_gs × f_sw, typiquement 25–50 mA) ; les crêtes doivent être
  absorbées localement.
- **R de grille séparées** turn-on / turn-off (via diode). Levier direct sur le bruit vu
  par l'ADC : ralentir le turn-on de 20 ns coûte peu en rendement et gagne beaucoup en EMI.

---

## 7. Liaison carte à carte

- **Rien de haute impédance ne traverse le câble** — sauf NTC (ci-dessous).
- **Un fil de retour par signal** : paire torsadée, ou nappe avec alternance signal/GND.
  Une nappe « 8 signaux + 1 GND » est une erreur classique.
- Connecteur : **≥ 30 % de broches GND**, réparties, GND aux deux extrémités.
- Filtre RC (ou RC + ferrite) **à l'arrivée sur la carte de commande**.
- Signal critique ou long → **différentiel** (rejet du mode commun dû aux dv/dt).
- Selfs de mode commun si dv/dt sévères.
- **Un seul point de connexion des masses**, via le connecteur. Pas de second chemin par
  boîtier ou entretoise métallique → sinon le courant de mode commun boucle et pollue.
  **Vrai malgré les régulateurs indépendants.**
- Câbles courts (< 15 cm), plaqués contre un plan de masse, **jamais parallèles** au
  câble de puissance ni au fil du nœud de commutation.
- Mécanique : cartes perpendiculaires, ou plaque de blindage reliée à la masse commande.
- **Ne pas renvoyer le 3.3 V d'une carte à l'autre** : ce serait une antenne branchée
  directement sur le rail analytique critique.

### Thermistances — exception encadrée
Pull-up + NTC tous deux côté puissance, excités en **3.3 VA-PWR**. Le nœud du diviseur
(5–10 kΩ) traverse le câble : haute impédance, mais acceptable car la bande utile est de
l'ordre du hertz.

- **RC massif à l'arrivée : 1 µF, f_c ≈ 1–10 Hz.** Écrase le mode commun capté.
- **Fil de retour dédié par NTC**, torsadé avec le signal. Un retour passant par le plan
  de masse de puissance donne une lecture qui bouge avec le rapport cyclique.
- Mieux si la place le permet : buffer côté puissance → Z_source de quelques ohms.
- **Non ratiométrique** : excitation par 3.3 VA-PWR, lecture référencée VDDA. Deux
  AP2210/LM317 à ~1 % → jusqu'à 2 % d'écart, soit **< 1 °C sur une NTC 10 k en zone
  20–80 °C**. Acceptable pour de la surveillance thermique.
- Correction optionnelle gratuite : mesurer 3.3 VA-PWR sur un canal ADC (diviseur 2:1) et
  diviser numériquement → rétablit le ratiométrique **et** sert de détection de présence.

---

## 8. Firmware — sécurité liée aux alimentations indépendantes

- **Canal de monitoring du rail carte puissance** : sanity check au boot et en continu.
  Le PWM ne démarre que si ce canal est dans une fenêtre valide.
- **Trip-zone matérielle (TZ)** : sorties PWM forcées inactives tant que la carte
  puissance n'est pas confirmée présente et saine. Utiliser le TZ **hardware** du F28027,
  pas une variable logicielle — il agit même si le code plante.
- **Plausibilité croisée** : tension de sortie et courant incohérents → défaut de liaison.
  Détecte le connecteur mal enfiché, mode de panne le plus fréquent d'une architecture
  2 cartes.
- Piège : carte commande seule alimentée → les ADC lisent zéro, ce qui **ressemble à un
  fonctionnement normal** (courant nul, tension nulle). D'où la détection explicite.

---

## 9. Checklist de revue

### Points ouverts à trancher
- [ ] **Forme du module ESP32** (WROOM nu 3.3 V vs DevKit 5 V) → conditionne l'arbre 5 V
- [ ] **Techno et référence MOSFET** → valide ou invalide les 10 V de grille
- [ ] **Référence ampli de shunt** → V_supply max ≥ 3.3 V, plage de sortie
- [ ] **15 V mesuré en pleine charge** → marge de dropout du LM317 10 V
- [ ] **Découplage VREFHI/VREFLO** en mode interne (note TI F2802x)

### Firmware
- [ ] Calibration ADC TI appelée au boot
- [ ] SOC déclenché par ePWM, ni timer ni polling
- [ ] Fenêtre hors des fronts + blanking 100–200 ns
- [ ] `ACQPS` dimensionné pour Z_source réelle (1 kΩ + 100 Ω)
- [ ] Moyennage synchrone 2–4 échantillons
- [ ] Séquenceur : pas de canal HZ après canal à forte dynamique
- [ ] Monitoring rail puissance + TZ hardware + plausibilité croisée
- [ ] Errata F2802x vérifié

### Schéma
- [ ] Deux AP2210 identiques depuis le 5 V, C_out identiques
- [ ] Clamp BAT54S VDD ↔ VDDA
- [ ] LM317 : bypass ADJ 10 µF + 2 diodes de protection (les 3)
- [ ] R 1 kΩ côté puissance sur chaque signal inter-cartes
- [ ] Clamp BAT54S sur entrées ADC exposées
- [ ] Charge bucket 10 nF + R 100 Ω sur chaque entrée ADC
- [ ] Buffer sur toute source > 50 Ω (impératif sur le pont 500 V)
- [ ] Shunt Kelvin, ampli diff en 3.3 VA-PWR côté puissance
- [ ] VDDA : 1 nF + 100 nF + 10 µF
- [ ] C_out VREG 1.8 V conforme TI
- [ ] Pull-down grille-source 10 kΩ + pull-down entrées driver
- [ ] Bulk 10 µF // 100 nF au driver
- [ ] R de grille séparées on/off
- [ ] Diode série + fusible + TVS sur entrée 15 V carte commande
- [ ] Entrées ADC inutilisées à AGND

### Layout
- [ ] 4 couches, plan de masse L2 continu et non fendu (chaque carte)
- [ ] Partitionnement analogique / numérique par placement
- [ ] LDO placés dans leur zone de charge respective
- [ ] Keep-out antenne ESP32 respecté
- [ ] Pistes ADC courtes, gardées, stitching 5–10 mm
- [ ] Boucle chaude minimisée, snubber présent
- [ ] Boucle de grille minimale, retour driver à la source
- [ ] Distances de fuite HT ≥ 3–4 mm + fentes
- [ ] Connecteur inter-cartes ≥ 30 % GND
- [ ] Aucun second chemin de masse entre cartes
- [ ] Dissipateur / cuivre suffisant sur LM317 5 V (jusqu'à 3.5 W en crête)
