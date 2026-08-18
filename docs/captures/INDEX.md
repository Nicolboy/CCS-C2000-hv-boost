# Index des captures d'oscilloscope — campagne V0.2 des 17 et 18/08/2026

> ⚠️ **Les fichiers image ne sont pas présents dans ce dossier.** Les captures
> ont été prises avec l'outil Capture d'écran de Windows et collées dans une
> conversation, sans être enregistrées. Ce document conserve **les relevés
> numériques lus sur l'instrument**, la configuration de la carte à chaque
> instant et ce que chaque capture démontre — c'est-à-dire l'essentiel de ce
> qui en a été tiré.
>
> Les noms de fichiers ci-dessous sont la **convention à appliquer** aux
> captures futures ou refaites.

## Convention de nommage

```
AAAA-MM-JJ_HHMM_description-courte.png     capture d'écran
AAAA-MM-JJ_HHMM_description-courte.csv     échantillons exportés
```

**Exporter en CSV, pas seulement en PNG.** Le bouton `Export` de la fenêtre
Scope de WaveForms enregistre les deux. 8192 points à 100 MHz font quelques
centaines de kilooctets, et le CSV permet de calculer moyennes, pentes et
instants de front exactement — au lieu de les estimer sur une image, ce qui a
produit plusieurs erreurs pendant cette campagne.

## Voies

Sauf mention contraire, sur toutes les captures :

| Voie | Signal | Couleur WaveForms |
|---|---|---|
| **C1** | sortie de l'ampli de shunt TSV791 (courant `I1`) | **jaune** |
| **C2** | grille du MOSFET, 8 V | **bleu** |

> **Piège rencontré** : WaveForms met C1 en jaune et C2 en bleu par défaut.
> L'hypothèse inverse a conduit à une analyse fausse, corrigée seulement
> quand l'amplitude de C2 (8,05 V sur son axe) a été recoupée avec la tension
> de grille réelle.

## Extraction de la valeur moyenne

L'instrument n'affiche pas la moyenne mais les deux RMS. Elle s'en déduit :

```
moyenne = racine(DC_RMS² − AC_RMS²)
```

Numériquement fragile quand les deux RMS sont proches (cas des points à faible
courant, où l'écart tombe sous 1 %).

---

## 1 — `2026-08-17_1846_rampe-avec-bosse-etablissement.png`

**Configuration** : masse du driver en aval du shunt, aucune résistance de
grille, **aucune résistance série à l'entrée de l'ampli de shunt**.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,85 kHz |
| C1 DC RMS / AC RMS | 444,13 mV / 348,43 mV → moyenne 275,4 mV |
| C1 PosDuty | 38,131 % |
| C2 DC RMS / AC RMS | 5,5303 V / 4,0769 V |
| C2 PosDuty | **47,813 %** ← le vrai rapport cyclique |
| Curseurs | ΔX = −36,81 ns, ΔC2/ΔX = **−149,84 V/µs** |

**Démontre** : la pointe `L·di/dt` du shunt à l'amorçage, et la **bosse
d'établissement de ~1 µs** qui la suit. À 43 % de la conduction, la trace est
0,19 V au-dessus de la rampe vraie — d'où `I1` faux de 15 %.

Recoupement de la lecture de voie : `8 × √0,478 = 5,53 V` = le DC RMS de C2,
donc C2 est bien la grille à 8 V.

---

## 2 — `2026-08-17_2051_470ohms-avant-rc-entree.png`

**Configuration** : identique à la 1. Charge 470 Ω. C2 décochée.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,31 kHz |
| C1 DC RMS / AC RMS | 165,80 mV / 164,58 mV → moyenne 20,1 mV |
| C1 PosDuty | 16,211 % |

**Démontre** : fonctionnement en **DCM**. Point inexploitable pour
l'étalonnage — les deux RMS ne diffèrent que de 0,7 %, et la ligne de base
légèrement négative pèse alors plus lourd que le signal.

---

## 3 — `2026-08-17_2054_100ohms-avant-rc-entree.png`

**Configuration** : identique à la 1. Charge 100 Ω. C2 décochée.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,91 kHz |
| C1 DC RMS / AC RMS | 444,38 mV / 358,47 mV → moyenne 262,6 mV |
| C1 PosDuty | 36,572 % |

**A servi à** : première détermination du gain `I1` par la route de la
moyenne. Résultat contaminé par la bosse d'établissement.

---

## 4 — `2026-08-18_0917_470ohms-apres-rc-entree.png`

**Configuration** : **RC 1 kΩ/100 pF ajouté à l'entrée de l'ampli de shunt**
(τ = 100 ns). Charge 470 Ω.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 201,03 kHz |
| C1 DC RMS / AC RMS | 173,92 mV / 164,58 mV → moyenne 82,4 mV |
| C1 PosDuty | 15,133 % |

**Démontre** : **la bosse d'établissement a disparu**, rampe linéaire dès la
sortie du front. Le RC n'a pas changé le gain, il a rendu sa mesure possible.

---

## 5 — `2026-08-18_0921_100ohms-apres-rc-entree.png`

**Configuration** : identique à la 4. Charge 100 Ω.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,95 kHz |
| C1 DC RMS / AC RMS | 409,17 mV / 306,68 mV → moyenne 270,9 mV |
| C1 PosDuty | 37,294 % |

---

## 6 — `2026-08-18_0928_100ohms-avec-grille-etalonnage-final.png` ⭐

**Configuration** : firmware rechargé, C2 réactivée sur la grille.
Vin 24,1 V, V1 45,42 V, I_in 1,00 A (multimètre), charge 100 Ω.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,86 kHz |
| C1 DC RMS / AC RMS | 453,48 mV / 336,03 mV → moyenne 304,5 mV |
| C1 PosDuty | 45,538 % |
| C2 DC RMS / AC RMS | 5,4956 V / 4,0593 V |
| C2 PosDuty | **46,617 %** |

**Capture de référence de l'étalonnage `I1`.** Deux routes concordantes :

```
Route A (moyenne)  : (0,3045 − 0,031) × 45,42 / (1,00 × 21,32) = 0,583 V/A
Route B (curseur a mi-conduction) : (0,613 − 0,031) / 1,00      = 0,582 V/A
```

Validation croisée : rapport cyclique de la grille 46,62 % contre
`(V1 − Vin)/V1 = 46,94 %` attendu — 0,3 % d'écart.

---

## 7 — `2026-08-18_0940_oscillation-grille-40MHz-overI1.png` 🔴

**Configuration** : premier essai à 50 W. Masse du driver **en aval** du
shunt, pas de résistance de grille. Single shot, déclenchement C1 montant 2 V.
C1 500 mV/div, C2 2 V/div.

| Mesure | Valeur |
|---|---|
| C1 DC RMS / AC RMS | 1,1244 V / 0,97503 V |
| C2 DC RMS / AC RMS | 3,7624 V / 3,3640 V |

**Démontre** : la grille **oscille entre ~1 et ~2,8 V à ~40 MHz pendant
~750 ns** à l'amorçage — le MOSFET est en régime linéaire. C1 saturée à
~3,5 V. Après l'événement, les deux traces à zéro : **le Trip Zone a
verrouillé**, première validation de la protection sur événement réel.

---

## 8 — `2026-08-18_0955_apres-masse-driver-kelvin.png`

**Configuration** : masse du driver déplacée **en amont du shunt** (Kelvin sur
la source du MOSFET).

| Mesure | Valeur |
|---|---|
| C1 Frequency | 200,96 kHz |
| C1 DC RMS / AC RMS | 0,72447 V / 0,53451 V |
| C1 PosDuty | 43,989 % |
| C2 DC RMS / AC RMS | 5,1443 V / 3,9386 V |
| C2 PosDuty | 37,672 % |

**Démontre** : l'oscillation soutenue à 40 MHz a **disparu**, la grille atteint
8 V proprement en régime établi. Mais il subsiste **3 ou 4 tentatives avortées
à l'amorçage** sur ~350 ns. Pointe C1 à ~2,2 V, contre 2,351 V de seuil.
Rampe utile : sommet à ~1,30 V, soit 2,2 A — courant de travail sain.

---

## 9 — `2026-08-18_0958_effondrement-driver-detail.png` 🔴

**Configuration** : identique à la 8, seuil de déclenchement porté à 3 V.

| Mesure | Valeur |
|---|---|
| C1 Frequency | 201,09 kHz |
| C1 DC RMS / AC RMS | 0,82804 V / 0,61817 V |
| C2 DC RMS / AC RMS | 5,1473 V / 3,9361 V |

**Capture clé du diagnostic.** La grille monte à ~4 V, retombe vers 0,5 V,
remonte — **trois ou quatre fois** avant d'accrocher les 8 V. Ce sont des
**marches d'escalier franches, pas une sinusoïde amortie** : signature d'un
redéclenchement du driver, et non d'une résonance LC. C'est l'indice qui
distinguait les deux mécanismes.

Pointe C1 montée à 3,0 V, donc **aggravée** par rapport à la capture 8.

---

## 10 — `2026-08-18_1005_resistance-grille-10ohms-avant-destruction.png` 🔴

**Configuration** : **résistance de grille de 10 Ω** ajoutée.

**Démontre** : la grille **oscille autour de 2 V — sa tension de seuil —
pendant ~1 µs** à chaque amorçage. C1 saturée à 3,3 V.

**Le MOSFET a été détruit environ 2 secondes plus tard**, soit ~400 000
épisodes de régime linéaire sous 46 V.

**Leçon** : une résistance de grille amortit une résonance LC. Le mécanisme en
cause était un redéclenchement du driver ; ralentir la grille n'a fait
qu'allonger le régime linéaire, de 350 ns à 1 µs.

---

## 11 — `2026-08-18_1118_50W-atteint.png`

**Configuration** : **RC 100 Ω/100 pF ajouté à l'entrée du driver** — le
correctif. MOSFET remplacé. Charge 42 Ω. 500 ns/div.

| Mesure | Valeur |
|---|---|
| C1 DC RMS / AC RMS | 0,76597 V / 0,57024 V |
| C2 DC RMS / AC RMS | 98,964 mV ← **sonde de grille débranchée** |

Relevés simultanés :

| | Multimètre | Serveur |
|---|---|---|
| Vin | 23,7 V | 24,1 V |
| I_in | 2,17 A | 2,13 A |
| V1 | 44,5 V | 46,0 V |
| I1 | — | 2,3 A |

```
Pin = 51,4 W    Pout = 47,1 W    rendement = 91,7 %
```

**Démontre** : les 50 W passent. Mais la sonde C2 étant débranchée, cette
capture ne documente pas la grille — voir la 12.

---

## 12 — `2026-08-18_1127_grille-propre-reference.png` ⭐

**Capture de référence du bon fonctionnement.** À reproduire avant chaque
montée en puissance.

**Configuration** : identique à la 11, sonde de grille rebranchée. 500 ns/div.

| Mesure | Valeur |
|---|---|
| C1 DC RMS / AC RMS | 0,81107 V / 0,58522 V → moyenne 561,6 mV |
| C2 DC RMS / AC RMS | 5,5515 V / 4,0687 V → moyenne 3,777 V |

**Démontre** : grille à **8 V plat, fronts nets, aucun effondrement sur toute
la conduction**. Rapport cyclique 0,47-0,48, contre 0,467 attendu.
Pointe d'amorçage sur C1 à **1,63 V** contre 2,351 V de seuil, soit **30 % de
marge**.

**Réserve** : le gain `I1` recalculé sur cette capture donne 0,523 V/A contre
les 0,58 de `calib.h`, et l'offset qui ressort d'un ajustement à deux points
(83 mV) ne se réconcilie pas avec la mesure directe à courant nul (31 mV).
**Ne pas corriger avant d'avoir assaini les masses** : la chute de ~20 mΩ sur
le retour des ponts diviseurs pollue `V1` et `Vin`, qui entrent tous deux dans
le calcul.

---

## Captures à refaire en priorité, en export CSV

1. **`2026-08-18_1127_grille-propre-reference`** — référence de bon
   fonctionnement, à conserver comme point de comparaison permanent.
2. **Une rampe de courant à 50 W**, après correction des masses analogiques :
   elle trancherait l'écart de 11 % restant sur le gain `I1`.
