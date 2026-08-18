# Archive — étalonnage de la carte de puissance V0.1

> **Document d'archive. Ces valeurs ne sont plus dans le firmware.**
> La carte en service est la **V0.2**, dont l'étalonnage vit dans `src/calib.h`
> et dans `hardware.md`. Ce fichier existe pour deux raisons : ne pas reperdre
> le raisonnement qui a coûté plusieurs séances de banc, et garder les pièges
> identifiés, qui eux restent valables quelle que soit la révision.
>
> Figé le 17/08/2026, au passage V0.1 → V0.2.

---

## 1. Constantes V0.1 (état final, telles qu'elles étaient dans `calib.h`)

| Voie | Constante | Valeur V0.1 | Confiance à l'époque |
|---|---|---|---|
| VIN | `MEAS_VIN_GAIN_V_PER_V` | `10.909` | ✅ brut 2742 → 24,10 V au voltmètre |
| V1 | `MEAS_V1_GAIN_V_PER_V` | `30.82` | ✅ brut 944 → 23,44 V |
| VOUT | `MEAS_VOUT_GAIN_V_PER_V` | `181.82` | ✅ mesuré (théorique 178,6 ; écart 1,8 %) |
| IIN | `MEAS_IIN_A_PER_V` | `1.214` | ✅ deux points à 60 % d'écart, rapports à 0,1 % |
| IIN | `MEAS_IIN_OFFSET_V` | `0.000` | offset jugé négligeable, cf. §3 |
| I1 | `MEAS_I1_GAIN_V_PER_A` | `0.816` | ⚠️ **un seul point**, 29 % d'écart inexpliqué |
| I1 | `MEAS_I1_OFFSET_V` | `0.0` | 0 à 4 counts à courant nul |
| I2 | `MEAS_I2_GAIN_V_PER_A` | `0.637` | ❌ **jamais mesurée** |
| I2 | `MEAS_I2_OFFSET_V` | `0.0` | — |
| IOUT | — | `Vadc × 0,016667` | ❌ **jamais mesurée**, valeur de conception |
| NTC | β = 4000 K, pont 10 k / 10 k | — | ✅ sens validé uniquement |

Matériel de mesure V0.1 associé :

| Fonction | Composant V0.1 |
|---|---|
| Mesure IIN | **ZXCT1109**, Rsense 0,0208 Ω, Rgain 10 kΩ |
| Ampli de shunt I1/I2 | TSV791 (50 MHz), après TLV9151, après MCP6001 |
| Shunts MOSFET | 0,02 Ω nominal (valeur effective suspecte) |
| Suiveurs de tension | TLV9151 + RC 1 kΩ / 10 nF en sortie |

## 2. Points de mesure bruts V0.1

Étalonnage **en continu, PWM inhibé**, sur la valeur brute de l'ADC lue au
débogueur, contre le voltmètre, charge derrière la diode.

```
VIN : brut 2742 -> 2,2091 V   voltmètre 24,10 V  ->  10,909
V1  : brut  944 -> 0,7605 V   voltmètre 23,44 V  ->  30,82
```

Les 0,66 V d'écart entre les deux mesures sont la chute directe de la diode
FFSD2065 à 0,55 A — cohérent, et c'est ce qui avait permis de voir que V1
lisait trop haut : l'ADC affichait la même tension des deux côtés de la diode.

IIN, étalonnée **sans passer par le shunt** (en continu, `I = V1 / Rcharge`,
charge mesurée à 39,3 Ω, V1 calibrée à 0,1 %) :

```
24,0 V : brut 610 -> 0,4914 V   I = 23,44/39,3 = 0,5964 A -> 1,2137
15,2 V : brut 378 -> 0,3046 V   I = 14,54/39,3 = 0,3700 A -> 1,2149
```

Deux points séparés de 60 % en courant, même rapport à 0,1 % : la droite passe
par l'origine. À vide la voie lisait encore 16 mA ; les intégrer comme offset
DÉGRADAIT l'accord aux points de travail (1,8 % de dispersion au lieu de 0,1 %).

## 3. Historique du gain IIN — l'aller-retour à ne pas refaire

| Valeur | Origine | Verdict |
|---|---|---|
| `1.2019` | premier étalonnage | sous-estimait de 30 %, rendements > 100 % |
| `1.568` | ajustement empirique à deux points | faisait lire 30 % trop haut (1,80 A affichés pour 1,42 A réels) |
| `1.202` | calculée : `Gt` 4 mA/V × `Rsense` 0,0208 Ω × `Rgain` 10 kΩ = 0,832 V/A | — |
| `1.214` | étalonnée sans passer par le shunt, deux points | valeur finale V0.1 |

**La leçon :** deux valeurs successives ont été posées empiriquement sans
comprendre l'écart, et la deuxième a annulé la première. Le symptôme qui a
tranché à chaque fois n'était pas une mesure isolée mais le **bilan de
puissance** : un rendement supérieur à 100 % est une impossibilité physique,
donc un révélateur qui ne ment pas.

## 4. Pièges identifiés sur V0.1 — **toujours valables sur V0.2**

C'est la partie de ce document qui ne périme pas.

**Le multimètre est perturbé par le découpage à 200 kHz.** Il lisait jusqu'à
56 % de moins que l'ADC sur la sortie du ZXCT1109. Ce n'est PAS un effet de
charge : vérifié à 24,07 V sous 10 kΩ contre 24,10 V sans, donc 10 MΩ
d'impédance d'entrée. → **Étalonner en continu, PWM inhibé.**

**La réinjection de charge de l'ADC.** Avant l'ajout des RC 1 kΩ + 10 nF en
sortie des suiveurs, V1 lisait jusqu'à **16 % trop haut, de façon bimodale**.
Tout étalonnage antérieur à ce filtre est à jeter.

**Le repliement d'une oscillation à 2,4 MHz** faisait lire V1 faux de 2,7 %.
L'échantillonneur-bloqueur de l'ADC redresse une sonnerie que le RC atténue
mais n'élimine pas.

**Ne jamais étalonner un shunt par la méthode de la pente**
(`dV/dt = gain × Vin/L`) : elle confond le gain avec l'inductance réelle,
connue à ±20 % au mieux. Deux tentatives ont donné 0,62 puis 0,73. La seule
méthode valide : en conduction continue, le courant d'inductance **à
mi-conduction** vaut exactement le courant d'entrée — aucune hypothèse sur L,
le shunt ou le gain.

**`DACVAL` est un champ 10 bits, tronqué et non saturé.** Un seuil de 3,67 V
donnait 1138 → tronqué à 114 → déclenchement dès 0,45 A, l'inverse de l'effet
voulu. Arrivé réellement, en relevant `MEAS_I1_GAIN_V_PER_A` de 0,631 à 0,816.
D'où l'écrêtage obligatoire dans `SAFETY_DAC_CODE_FROM_V`.

**Étalonner sur la valeur BRUTE de l'ADC, pas sur l'affichage du serveur.**
La télémétrie est arrondie à une décimale : à 24,0 V affichés, l'incertitude
de ±0,05 V interdit de séparer un gain d'un offset.

**Le bilan de puissance est le juge de paix.** Il ne demande de faire
confiance à aucun instrument en particulier et détecte immédiatement une
chaîne fausse.

## 5. Ce qui restait ouvert sur V0.1

- **Écart de gain de 29 % sur I1.** Gain mesuré 0,816 V/A → 25,9 mΩ de
  résistance effective pour 20 mΩ nominaux. La ligne de base négative
  (−34 mV) signale du cuivre partagé entre l'extrémité froide du shunt et le
  chemin de retour, mais n'en représente que 0,4 mΩ. Jamais élucidé.
- **I2, IOUT : jamais mesurées.**
- **NTC : résistance fixe réelle jamais confirmée** (10 kΩ supposé).
- **Pleine puissance (50 W) seulement au-dessus de ~19 V d'entrée** : en
  dessous, le courant crête dépasse la pleine échelle de la chaîne (4,04 A) et
  aucun seuil n'y est plaçable. Contournement spécifié mais non implémenté :
  le repliement de puissance (tag `LIM`).
