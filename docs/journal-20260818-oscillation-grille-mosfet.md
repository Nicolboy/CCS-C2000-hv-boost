# Oscillation de grille étage 1 — destruction du MOSFET (18/08/2026)

Document de reprise. Exposé factuel du problème, des essais menés et de leur
résultat. Les mesures sont séparées des hypothèses. À lire avant toute
intervention sur l'étage 1 de la carte de puissance V0.2.

Relevés d'instrument détaillés des douze captures d'oscilloscope de la
campagne : **`docs/captures/INDEX.md`**.

---

## 1. Contexte matériel

Alimentation haute tension **dual boost en cascade** pilotée par un
TMS320F28027. **Seul l'étage 1 est peuplé** ; le MOSFET, la diode et
l'inductance de l'étage 2 ne sont pas montés (`VOSET = 0` côté firmware).

### Étage 1

| Élément | Valeur |
|---|---|
| Entrée | 24 V (alimentation de laboratoire) |
| Sortie `V_inter` | consigne 15 à 50 V, essais à 45-46 V |
| Découpage | 200 kHz, rapport cyclique ≈ 0,47 |
| Inductance | 47 µH |
| MOSFET | **NTD100N70GN1**, 70 V, boîtier 3 broches (pas de source Kelvin) |
| Driver de grille | **UCC27517**, **inverseur**, alimenté pour 8 V de grille |
| Diode | **FFSD2065**, Schottky SiC (pas de recouvrement inverse) |
| Charges d'essai | 470 Ω, 100 Ω, 42 Ω |

### Chaîne de mesure du courant `I1`

| Élément | Valeur |
|---|---|
| Shunt | **10 mΩ, 1 %, 500 mW, boîtier 1206** |
| Position du shunt | **dans la SOURCE du MOSFET** |
| Amplificateur | **TSV791**, non-inverseur, `1 + 3300/100 = 34` |
| Filtre d'entrée | **1 kΩ + 100 pF** (τ = 100 ns), ajouté le 18/08 |
| Sortie vers le DSP | 100 Ω série, **aucun condensateur** |
| Broche DSP | **9** — `ADCINA2 / COMP1A`, partagée ADC **et** comparateur analogique |

### Protection contre les surintensités

Comparateur analogique interne + DAC 10 bits → Digital Compare → Trip Zone
one-shot. Entièrement matérielle et **continue** : elle n'échantillonne
jamais, elle est indépendante de l'ADC et du logiciel.

- `SAFETY_COMP_QUALSEL = 31`, soit **~530 ns** de qualification (valeur
  maximale du champ, rien à gagner de plus)
- Seuil : `SAFETY_ISHUNT_THRESHOLD_A = 4,0 A` crête
- Code DAC : `(0,031 + 4,0 × 0,58) / 3,3 × 1023 = 729`, soit **2,351 V** à la
  broche
- Pleine échelle de la chaîne : `(3,3 − 0,031) / 0,58 = 5,6 A`

### Étalonnage `I1` retenu (mesuré, deux routes indépendantes)

```
MEAS_I1_GAIN_V_PER_A = 0,58        MEAS_I1_OFFSET_V = 0,031
```

Point de mesure : Vin 24,1 V, V1 45,42 V, I_in 1,00 A (multimètre), 100 Ω.

- **Route A**, par la moyenne : `(0,3045 − 0,031) × 45,42 / (1,00 × 21,32) = 0,583`
- **Route B**, curseur à mi-conduction, où le courant d'inductance vaut
  exactement le courant d'entrée : `(0,613 − 0,031) / 1,00 = 0,582`

Validation croisée : rapport cyclique lu sur la grille = 46,62 %, contre
`(V1 − Vin)/V1 = 46,94 %` attendu.

---

## 2. Le problème

Lors des essais de montée en puissance vers 50 W (charge 42 Ω), la grille du
MOSFET **n'atteint pas son état haut de façon franche à l'amorçage**. Elle
oscille ou s'effondre pendant plusieurs centaines de nanosecondes, laissant le
MOSFET en **régime linéaire** sous 46 V de drain avec plusieurs ampères.

La protection matérielle a correctement déclenché (`FAULT = 2`, surintensité
`I1`) à chaque épisode, mais **le MOSFET a fini par être détruit** après
environ 2 secondes de fonctionnement, soit ~400 000 épisodes.

---

## 3. Chronologie des essais

### Essai 1 — configuration d'origine

**Configuration** : masse du driver en **aval** du shunt (masse de puissance),
**aucune résistance de grille**, RC 1 kΩ/100 pF à l'entrée de l'ampli de shunt.

**Observation au scope** (C1 = sortie ampli de shunt, C2 = grille) :

| | Valeur |
|---|---|
| Grille à l'amorçage | oscillation **1 à 2,8 V** à **~40 MHz**, pendant ~750 ns |
| Sortie ampli | **saturée à ~3,5 V** (rail), hachée |
| Suite | PWM coupé, les deux traces à zéro — Trip Zone verrouillé |

La grille n'atteint jamais 8 V ; elle reste autour de la tension de seuil.

**Hypothèse formulée** : inductance de source commune. Le shunt étant dans la
source, sa résistance **et son inductance parasite** appartiennent à la fois au
circuit de puissance et au circuit de grille. Le `L·di/dt` du courant de drain
se retranche de la Vgs.

### Essai 2 — masse du driver déplacée en amont du shunt (Kelvin)

**Modification** : masse du driver connectée directement à la broche source du
MOSFET, en amont du shunt.

**Résultat — amélioration partielle** :

| | Avant | Après |
|---|---|---|
| Oscillation soutenue 40 MHz | présente | **éliminée** |
| Grille en régime établi | 1-2,8 V | **8 V propres, fronts nets** |
| Grille à l'amorçage | — | **3 ou 4 tentatives avortées** : montée à ~4 V, effondrement vers 0,5 V, reprise — sur ~350 ns |
| Pointe sortie ampli | 3,5 V | **2,2 puis 3,0 V** selon les cycles |
| Déclenchement `overI1` | oui | **oui, toujours** |

Rampe de courant utile en régime établi : sommet à ~1,30 V, soit
`(1,30 − 0,031)/0,58 = 2,2 A` — **le courant de travail est sain**. C'est la
pointe d'amorçage qui franchit le seuil de 2,351 V.

**Hypothèse formulée** : le driver a désormais sa masse sur un nœud qui bouge,
alors que **son entrée logique vient de la porte ET (IC8), référencée à la
masse logique**. Le driver ne voit que `V_entrée − V_sa_masse`.

Estimation du rebond à l'amorçage :

```
décharge de la capacité de jonction de la FFSD2065 (~1 nF sous 46 V)
di/dt de l'ordre de 1 A/ns = 10⁹ A/s
inductance du chemin de source (shunt 1206 + piste) ≈ 2 à 3 nH

V = L·di/dt ≈ 3 nH × 10⁹ = 3 V
```

Trois volts de rebond positif sur la masse du driver : vu depuis lui, son
entrée chute de 3 V, il la lit comme un ordre de blocage et coupe. Le courant
s'arrête, le rebond disparaît, il rouvre. D'où les tentatives successives, qui
cessent quand la capacité de la diode est déchargée.

**Note** : la même inductance pollue la mesure. L'ampli de shunt voit
`R·I + L·di/dt` ; à ces vitesses le second terme domine, et la pointe observée
n'est pas représentative d'un courant réel.

### Essai 3 — résistance de grille de 10 Ω

**Modification** : 10 Ω en série sur la grille.

**Résultat — dégradation, puis destruction** :

| | Valeur |
|---|---|
| Grille à l'amorçage | oscille **autour de 2 V** (tension de seuil) pendant **~1 µs** |
| Sortie ampli | **saturée à 3,3 V** (rail), oscillation violente |
| Durée de fonctionnement | ~2 secondes |
| Issue | **MOSFET détruit** |

**Analyse** : une résistance de grille amortit une **résonance LC** de la
boucle de grille. Le mécanisme en cause est un **redéclenchement du driver**,
pas une résonance. Ralentir la grille ne fait qu'**allonger le temps passé
autour du seuil**, donc allonger le régime linéaire — de 350 ns à 1 µs.

L'indice qui distinguait les deux mécanismes était visible : le driver
produisait des **tentatives franches, en marches d'escalier**, et non une
sinusoïde amortie.

---

## 4. Causes écartées, avec le motif

| Piste | Motif de l'élimination |
|---|---|
| Firmware / instant d'échantillonnage ADC | la protection est analogique et continue, elle n'échantillonne jamais |
| Recouvrement inverse de la diode | FFSD2065 = Schottky SiC, aucun recouvrement |
| Filtre RC en sortie d'ampli | il n'y en a pas, seulement 100 Ω série |
| Pertes de commutation du MOSFET | bilan d'énergie : 1 µs de transition dissiperait 4,6 W à 200 kHz, contre **1,9 W de pertes totales mesurées** sur le convertisseur |
| Saturation de l'inductance | rampe linéaire, pas de plateau, pas d'échauffement, `I_min = 1,2 A > 0` donc CCM franche |
| Seuil de protection trop bas | courant crête réel ~2,9 A à 50 W contre un seuil de 4,0 A, soit 38 % de marge |

---

## 5. Problèmes distincts résolus pendant la même campagne

À connaître, car ils ont invalidé des mesures antérieures.

1. **Deux alimentations sans masse commune** (carte de contrôle et carte de
   puissance). Corrigé le 17/08 au soir. **Tous les relevés antérieurs sont
   invalidés**, y compris ceux qui semblaient confirmer une constante.

2. **Aucune résistance série à l'entrée de l'ampli de shunt.** La pointe
   `L·di/dt` arrivait directement sur les broches ; l'établissement prenait
   ~1 µs et faussait `I1` de 15 %. Corrigé par le RC 1 kΩ/100 pF.

3. **Étalonnage `I1` faux et protection inopérante.** Le gain avait été établi
   par injection à 10 A lue 3,40 V, d'où `0,34 V/A` — or la pleine échelle est
   de 5,8 A : l'amplificateur était **en butée** et 3,40 V était le rail. Avec
   ce gain, le seuil de 7,0 A demandait 3,99 V au DAC, au-delà de sa référence
   de 3,3 V : le code était **écrêté à 1023**, plaçant le seuil à une tension
   que l'ampli n'atteint jamais. **La protection de l'étage 1 était muette**,
   sans aucun signe extérieur. Corrigé (gain 0,58, seuil 4,0 A).

---

## 5 bis. RÉSOLU — RC sur l'entrée du driver (18/08/2026)

**Correctif nécessaire ET suffisant : RC de 100 Ω / 100 pF sur l'entrée
logique du driver, référencé à la masse du driver.**

### La position de la masse du driver n'est PAS le facteur déterminant

Quatre configurations ont été testées. Le recoupement est sans ambiguïté :

| Masse driver | RC entrée driver | Résultat |
|---|---|---|
| origine (masse de puissance) | non | 🔴 oscillation 40 MHz (essai 1) |
| Kelvin sur la source | non | 🔴 redéclenchement, 350 ns (essai 2) |
| Kelvin + 10 Ω de grille | non | 🔴 1 µs de régime linéaire, **MOSFET détruit** (essai 3) |
| Kelvin | **oui** | ✅ 50 W, grille propre |
| **origine** | **oui** | ✅ **fonctionne parfaitement** |

Le RC est le seul élément présent dans les deux configurations qui
fonctionnent et absent des trois qui échouent.

### Cause réelle

**L'entrée logique du driver n'avait aucune immunité au bruit** : entrée CMOS
rapide, haute impédance, dans un environnement commuté à 200 kHz. Elle se
faisait redéclencher ; seul le chemin de couplage différait selon la position
de la masse.

### Hypothèse abandonnée — inductance de source commune

Le passage de la masse du driver en Kelvin (essai 2) reposait sur l'idée que
le `L·di/dt` du shunt, commun aux circuits de puissance et de grille, se
retranchait de la Vgs. **Cette hypothèse n'a pas résisté** : le déplacement
n'a pas corrigé le problème, et il a posé la masse du driver sur le nœud le
plus bruyant du circuit — ce qui a produit le redéclenchement de l'essai 2,
puis la destruction du MOSFET à l'essai 3.

**La masse du driver a été remise à sa position d'origine, et tout fonctionne
parfaitement avec le seul RC d'entrée.**

### Recommandation pour l'étage 2 et pour toute reprise

Monter les RC. **Ne pas faire le Kelvin** : inutile, et l'état intermédiaire
sans RC est dangereux.

### Forme d'onde de grille après correctif

| | Valeur |
|---|---|
| Grille | **8 V plat, fronts nets, aucun effondrement sur toute la conduction** |
| Rapport cyclique lu sur la grille | 0,47-0,48, contre `(44,5-23,7)/44,5 = 0,467` attendu |
| Pointe d'amorçage sur l'ampli de shunt | **1,63 V** contre 2,351 V de seuil, soit **30 % de marge** |

### Essai à 50 W réussi

```
multimetre : Vin 23,7 V   I_in 2,17 A   V1 44,5 V   charge 42 ohms
Pin  = 23,7 x 2,17 = 51,4 W
Pout = 44,5^2 / 42 = 47,1 W
rendement = 91,7 %
```

Le même rendement qu'à 20 W : le convertisseur monte en puissance sans se
dégrader.

**Si la pointe d'amorçage revient** en montant plus haut en puissance, porter
le condensateur d'entrée de l'ampli de shunt de 100 pF à 470 pF (cf. §7 C).

---

## 6. État actuel et questions ouvertes

### État

- MOSFET de l'étage 1 **détruit lors de l'essai 3, remplacé**
- Masse du driver **remise à sa position d'origine** (masse de puissance) --
  le Kelvin a été abandonné, il n'apportait rien
- Résistance de grille de 10 Ω en place. Sans effet nuisible une fois l'entrée
  du driver dé-glitchée ; elle ne nuisait que pendant le redéclenchement, en
  allongeant le temps passé autour du seuil. À retirer ou ramener à 2,2 Ω si
  la pointe d'amorçage doit être réduite.
- RC 1 kΩ/100 pF à l'entrée de l'ampli de shunt
- **RC 100 Ω/100 pF à l'entrée du driver** -- le correctif
- Étage 1 fonctionnel à 50 W, rendement 91,7 %
- Firmware et étalonnage à jour, **non mis en cause**

### 🔴 NOUVEAU — chute de masse sur les ponts diviseurs analogiques

Relevé pendant l'essai à 50 W réussi :

| | Multimètre | Serveur | Écart |
|---|---|---|---|
| Vin | 23,7 V | 24,1 V | +1,7 % |
| V1 | 44,5 V | 46,0 V | **+3,4 %** |
| I_in | 2,17 A | 2,13 A | -1,8 % (dans la dispersion) |

L'erreur sur V1 est **proportionnelle au courant d'entrée**, ce qui exclut une
erreur de gain :

| Courant d'entrée | Erreur V1 ramenée a la broche | Resistance impliquee |
|---|---|---|
| 0,97 A | +20 mV | 20,6 mOhm |
| 2,17 A | +51 mV | 23,5 mOhm |

Rapport des erreurs 2,55 pour un rapport de courants 2,24. VIN donne le meme
ordre de grandeur (~17 mOhm). Les ponts diviseurs partagent donc une
**vingtaine de milliohms de retour de masse avec le chemin de puissance**, et
cette chute s'ajoute a la tension mesuree.

**Consequence reelle, pas cosmetique** : la boucle regule sur la valeur
mesuree. Elle affiche fidelement sa consigne de 46,0 V pendant que le vrai
`V_inter` est a **44,5 V**, et l'ecart bouge avec la charge.

**NE PAS corriger le gain pour compenser.** `MEAS_V1_GAIN_V_PER_V = 29,27` est
valide a trois points, il est juste. Retoucher le gain deplacerait l'erreur
vers une autre charge. Le correctif est un **retour de masse dedie** pour les
ponts analogiques vers l'AGND du DSP, en etoile, sans passer par la masse de
puissance.

C'est le **troisieme probleme de masse de la campagne**, apres les deux
alimentations non reliees et la reference du driver. Un plan de masse a revoir
globalement est plus probable que trois correctifs ponctuels.

**Tout etalonnage mesure avant cette correction en herite** : re-verifier les
gains une fois les masses assainies, et pas avant.

### À vérifier avant toute remise sous tension

Un MOSFET meurt généralement **court-circuité drain-source**. Le courant
d'entrée est alors passé sans limite par le reste de la chaîne. À contrôler à
l'ohmmètre, carte hors tension :

- [ ] shunt 10 mΩ (peut s'ouvrir ou dériver)
- [ ] TSV791 (entrée poussée hors plage sans résistance série pendant toute la campagne)
- [ ] UCC27517 (a débité dans une grille éventuellement percée)
- [ ] diode FFSD2065 et inductance
- [ ] alimentation de laboratoire et sa limitation de courant

### Questions restées sans réponse

1. **Y a-t-il un condensateur de découplage sur le driver, et son retour
   va-t-il au même point que la broche GND du driver** (donc sur la source
   depuis la modification) ? Si le retour est resté sur la masse de puissance,
   la boucle d'alimentation du driver traverse le shunt — second chemin de
   perturbation, non exploré.
2. Quelle est exactement la référence de masse de la porte ET IC8 qui attaque
   l'entrée du driver ?
3. Cause exacte de la mort du MOSFET (thermique par régime linéaire, ou
   avalanche) — le NTD100N70GN1 est en 70 V pour une sortie à 46 V, soit une
   marge de 50 % ringing compris.

---

## 7. Directions proposées — HISTORIQUE, en grande partie périmé

> Section conservée pour la trace du raisonnement. **La résolution effective
> est au §5 bis** : le seul RC sur l'entrée du driver, masse d'origine
> conservée. La direction A (MOSFET à source Kelvin) n'a pas été nécessaire.

### A. MOSFET à broche source Kelvin (recommandé)

Boîtier 4 broches. La boucle de grille est séparée du chemin de puissance
**par construction** : le driver garde sa masse sur la source Kelvin, qui ne
porte pas le courant de puissance. Le mécanisme de redéclenchement disparaît,
il n'est pas seulement atténué.

Passer au passage à **100 V minimum** au lieu des 70 V actuels.

### B. Si l'on reste en boîtier 3 broches

- masse du driver, **son découplage** et son **entrée logique** tous
  référencés au même point ;
- **RC de 100 Ω / 100 pF sur l'entrée du driver**, référencé à *sa* masse
  (τ = 10 ns, suffisant pour rejeter un rebond de quelques nanosecondes sans
  retarder la commande) ;
- **pas de résistance de grille au-delà de 2,2 Ω** tant que le
  redéclenchement n'est pas éliminé — au-delà elle allonge le régime linéaire.

### C. Filtrage de la pointe de mesure, si elle subsiste

Porter le condensateur d'entrée de l'ampli de shunt de 100 pF à **470 pF**
(τ = 470 ns, toujours sous les 530 ns de `QUALSEL`) : une pointe de ~200 ns
serait atténuée à ~35 %, tandis que la rampe utile de 2,3 µs ne subirait
qu'un retard de 470 ns, soit 0,24 A de sous-lecture.

**Ne pas relever le seuil de protection** : il a démontré sa valeur sur
événement réel, et la pleine échelle de la chaîne (5,6 A) ne laisse pas de
place au-dessus de 4,0 A.

---

## 8. Procédure de remise en route

Ne pas relancer directement à 50 W.

1. Chaîne vérifiée à l'ohmmètre, composants douteux remplacés.
2. **Vin 10-12 V**, charge légère (470 Ω), limitation de courant de
   l'alimentation à ~0,5 A.
3. **Capture de la grille en premier** : elle doit être un carré propre à 8 V,
   **sans effondrement, sur tous les cycles**. Ne pas augmenter la puissance
   tant que ce n'est pas obtenu.
4. Seulement ensuite : 100 Ω, puis 24 V, puis 42 Ω.

La leçon de l'essai 3 est qu'un régime linéaire de 1 µs répété à 200 kHz tue
un MOSFET en quelques secondes. **La forme d'onde de grille doit être validée
avant, et non pendant, la montée en puissance.**
