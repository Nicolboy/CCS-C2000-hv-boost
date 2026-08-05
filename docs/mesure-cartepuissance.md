# Mesures carte puissance — TMS320F28027 dual boost

Suivi des mesures de calibration effectuées sur la carte élévateur (à moitié
complète : composants de puissance et drivers MOSFET pas encore montés).
Ce document est la source pour remplir `calib.h` dans le firmware. Les
valeurs numériques finales à utiliser en code sont dans
`PROMPT-claude-code-TMS320.md` (section 5) ; ce fichier-ci garde la trace
des mesures brutes et de leur contexte.

---

## 1. Chaînes de mesure de tension

Méthode : alimentation de labo réglée à une tension connue, lecture de la
tension de sortie du pont diviseur au multimètre (avant l'ADC).

| Voie | Point de mesure | Coefficient mesuré (Vadc/Vréel) | Gain (V/V) | Pleine échelle (3,3 V) | Statut |
|---|---|---|---|---|---|
| VIN (V_batt) | pin 10 | 0,09 | 11,11 | 36,7 V | ✅ mesuré |
| V1 (V_inter) | pin 16 | 0,032 | 31,25 | 103,1 V | ✅ mesuré |
| VOUT (V_HT) | pin 14 | 0,0055 | 181,82 | 600 V | ✅ mesuré |

**Remarques :**
- VIN et V1 tombent exactement sur les valeurs théoriques calculées à la
  conception (11,11 et 31,25) — le pont a été monté avec les bonnes valeurs
  de résistances.
- VOUT diffère de 1,8 % de la valeur théorique (178,6 → 181,82), imputable
  à la tolérance des résistances réelles. Sans conséquence : la pleine
  échelle réelle (600 V) reste au-dessus de l'objectif (400 V max), avec
  même un peu plus de marge que prévu.
- V1 max visé = 50 V → seulement ~1,6 V sur l'ADC (48 % de la plage), pas
  de saturation, résolution confirmée suffisante.

---

## 2. Chaîne de mesure de courant d'entrée (IIN)

Méthode : courant imposé au primaire, lecture de la tension de sortie de
l'ampli de mesure au multimètre.

| Point | Valeur |
|---|---|
| Mesure | 250 mA → 0,208 V en sortie ampli |
| Gain calculé | 0,832 V/A |
| Gain inverse | 1,2019 A/V |
| Pleine échelle (3,3 V) | 3,97 A |

Un seul point mesuré (pas de vérification de linéarité multi-points comme
pour les shunts MOSFET). Cohérent avec le courant d'entrée nominal attendu
(~590 mA pour ~5 W de charge côté HT), bonne marge avant saturation.

**Reste à faire** : idéalement un second point de mesure pour confirmer la
linéarité et détecter un éventuel offset (mesure faite à un seul courant,
donc le calcul suppose une caractéristique passant par zéro — à vérifier).

---

## 3. Chaînes de mesure de courant shunt MOSFET (I1, I2) — protection rapide

⚠️ **Mesures faites avec le MCP6001 monté provisoirement sur le banc de
test.** Le composant final prévu est le TLV9151 (meilleur offset, meilleure
bande passante). Le gain (fixé par le réseau de résistances RF/RG) doit
rester valable après le remplacement ; l'offset devra être entièrement
remesuré.

Méthode : courant imposé dans chaque shunt (0,02 Ω), lecture au
multimètre de la tension aux bornes du shunt (`V_Rshunt`) et de la sortie
ampli-op avant ADC (`V_shunt_adc`), à quatre points (0, 0,5, 1, 2 A).

### Données brutes

**Shunt 1 (étage 1) :**

| I (A) | V_Rshunt (mV) | V_shunt1_adc (V) |
|---|---|---|
| 0 | — | 0,0473 |
| 0,5 | 10,3 | 0,367 |
| 1 | 20,38 | 0,682 |
| 2 | 40,9 | 1,31 |

**Shunt 2 (étage 2) :**

| I (A) | V_Rshunt (mV) | V_shunt2_adc (V) |
|---|---|---|
| 0 | — | 0,0340 |
| 0,5 | 10,48 | 0,357 |
| 1 | 20,58 | 0,674 |
| 2 | 40,76 | 1,31 |

### Résultats (régression linéaire sur les 4 points)

| Voie | Offset (I=0) | Gain (V/A) | Gain équivalent shunt×ampli (V/V) | Gain inverse (A/V) | Pleine échelle (3,3 V) |
|---|---|---|---|---|---|
| I1 (étage 1) | 47,3 mV | 0,631 | ≈ 31,5 | 1,585 | 5,15 A |
| I2 (étage 2) | 34,0 mV | 0,637 | ≈ 31,8 | 1,570 | 5,12 A |

**Formules de conversion (provisoires, MCP6001) :**
```
I1 = (Vadc − 0,0473) / 0,631
I2 = (Vadc − 0,0340) / 0,637
```

**Remarques :**
- Très bonne linéarité sur les 4 points des deux voies.
- Écart de gain d'environ 1 % entre les deux voies — attribuable à la
  tolérance des résistances de gain, propre à chaque canal. Ne pas
  utiliser un gain unique moyenné : garder deux jeux de constantes séparés
  dans `calib.h`.
- Le gain mesuré (~31,5-31,8) est proche de la valeur visée à la
  conception (31), mais **s'écarte de l'hypothèse initiale utilisée pour
  calculer le seuil DAC** (qui supposait un gain de 30 pile). Voir §5 pour
  la correction.
- Les deux offsets (34-47 mV) sont nettement plus élevés que le Vos
  typique du MCP6001 seul à ce gain — probablement une combinaison de
  l'offset ampli réel et d'un petit décalage du pont/de la référence.
  Sans conséquence pratique : l'auto-zéro logiciel prévu au boot du
  firmware absorbe cet écart automatiquement, quelle que soit son origine.

---

## 4. Chaîne de mesure de courant de sortie (IOUT)

**Non mesurée.** Reste à faire — même méthode que pour IIN (courant imposé
côté charge après le switch de sortie, lecture de la tension ampli-op).
Valeur théorique provisoire dans `calib.h` : 3,0 V @ 50 mA → gain
0,016667 A/V (à confirmer/remplacer par une mesure réelle).

---

## 5. Seuil de protection — recalcul avec les gains mesurés

Le calcul initial du code DAC (`DACVAL`) pour le seuil de 3 A avait été
fait en supposant un gain exactement égal à 30 (shunt 0,02 Ω × ampli-op
×30), donnant `Vadc = 1,8 V` à 3 A. **Cette hypothèse est fausse** : le gain
réel mesuré est ~31,5-31,8, donc en réalité `1,8 V` correspondait à un
seuil réel plus proche de 3,3 A que de 3 A. Non dangereux (la protection
aurait juste déclenché un peu tard), mais corrigé avant tout flashage.

**Valeurs recalculées avec les gains et offsets mesurés (§3) :**

```
Seuil 3 A, comparateur 1 (I-shunt1) :
  Vadc = 0,0473 + 3 × 0,631 = 1,940 V
  DACVAL = round(1,940 × 1023 / 3,3) = 601

Seuil 3 A, comparateur 2 (I-shunt2) :
  Vadc = 0,0340 + 3 × 0,637 = 1,945 V
  DACVAL = round(1,945 × 1023 / 3,3) = 603
```

**⚠️ Ces deux valeurs sont provisoires (MCP6001).** Elles devront être
recalculées avec les mêmes formules dès que le gain et l'offset seront
remesurés sur le TLV9151. Le firmware ne doit jamais figer `DACVAL` en
dur : le calculer par macro à partir de `SAFETY_ISHUNT_THRESHOLD_A` et des
constantes de gain/offset mesurées, pour que ce remplacement futur ne
touche qu'aux valeurs de `calib.h`, pas au code.

---

## 6. Ce qu'il reste à faire

### Mesures à compléter
- [ ] IOUT : gain réel (un point de mesure minimum, idéalement deux)
- [ ] IIN : second point de mesure pour vérifier la linéarité/l'offset
- [ ] Chaîne NTC (Temp1, Temp2) : vérifier la valeur réelle de la
      résistance fixe des ponts diviseurs (10 kΩ supposé) et, si possible,
      un point de mesure à température connue pour valider la formule
      Steinhart-Hart simplifiée
- [ ] I1, I2 **à refaire intégralement** une fois le TLV9151 monté (gain à
      confirmer proche de ~31,5-31,8, offset à mesurer — attendu très
      inférieur aux 34-47 mV actuels)

### Matériel restant à monter sur la carte élévateur
- [ ] Composants de puissance (MOSFET IPD60R360, diode SiC STPSC406,
      inductances) — carte actuellement à moitié complète
- [ ] Driver de grille : **le UCC27518/19 initialement prévu ne convient
      pas** (seuils d'entrée CMOS proportionnels à VDD = 12 V, un signal
      3,3 V du F28027 ne les franchira jamais). Remplacer par UCC27517
      (seuils CMOS/TTL indépendants de VDD, même boîtier SOT-23-5, même
      brochage) — vérifier la polarité (inverseur/non-inverseur) requise
      selon le sens voulu sur EN
- [ ] TLV9151 : montage définitif à la place du MCP6001 de banc

### Firmware (TMS320F28027-dualboost, côté Claude Code / CCS)
- [ ] Reporter les gains/offsets mesurés (§1-3) dans `calib.h`, avec la
      structure par voie recommandée (gain, offset, seuil) plutôt que des
      constantes isolées
- [ ] Vérifier que le calcul de `DACVAL` est bien fait par macro à partir
      des constantes, pas en dur
- [ ] Une fois le driver de grille corrigé et les composants de puissance
      montés : test matériel de l'étape 2 du firmware (sécurité) avant
      toute activation du PWM — critère : `TZFRC` force les sorties à 0,
      un breakpoint coupe le PWM (TZ6/OSHT6), le flag ne se réarme pas
      seul

### Points de configuration matérielle en attente de confirmation
- [ ] Fréquence de découpage par étage (100 ou 200 kHz)
- [ ] Polarité réelle des LED (bleue/rouge)
- [ ] Valeur d'inductance retenue par étage (47 ou 100 µH)
- [ ] État de la liaison JTAG / UART après le dernier diagnostic (mesure
      du 1,8 V sur VDD broches 32/43, recâblage croisé RX/TX) — à
      confirmer que tout fonctionne avant de reprendre les tests
