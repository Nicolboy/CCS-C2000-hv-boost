# Montée en puissance — note de reprise

Campagne du 20 au 23/08/2026. Alimentation double boost TMS320F28027 :
10-24 V → V1 = 50 V → V_HT 200-500 V.

Ce document existe pour qu'une session neuve reprenne sans refaire le
chemin. Il dit ce qui est **acquis**, ce qui est **mesuré**, et ce qui
reste **ouvert** — et il distingue les trois.

---

## 1. Où en est la carte

| | |
|---|---|
| Branche | `bringup`, rien de poussé |
| Dernier commit | `e17c4de` |
| Firmware chargé | dualboost, HRPWM actif sur les deux étages |
| Meilleur point atteint | **400 V sur 50 kΩ**, tenu |
| Blocage actuel | `overI1` dès qu'on dépasse 400 V |

**Défaut matériel connu** : une piste coupée a été trouvée le 23/08 entre
la commande et l'étage 2. État de la réparation à vérifier avant toute
conclusion — plusieurs heures ont été perdues à chercher dans le logiciel
une panne qui était mécanique.

---

## 2. Ce qui a été résolu : la quantification du rapport cyclique

### Le problème

La sensibilité d'un boost est `dV/dD = Vin/(1-D)²`. Elle explose quand D
approche 1 :

| étage | Vin | Vout | D | période | **un count vaut** |
|---|---|---|---|---|---|
| 1 | 20 V | 50 V | 0,60 | 300 | 0,42 V (0,83 %) |
| 2 | 50 V | 500 V | 0,90 | 600 | **8,3 V (1,7 %)** |

Aucune valeur entière de CMPA ne donne 500 V. La régulation basculait donc
en permanence entre deux counts adjacents, et chaque bascule rompait
l'équilibre volt-seconde de près de 2 %. L'inductance intégrait ce résidu
pendant les 195 µs où la commande est figée : **escalier de courant
géométrique, +13 % par période, facteur 5 en 130 µs** (relevé au scope).

C'est ce qui produisait les déclenchements `overI2` « ponctuels » à 400 V
sur 50 kΩ, soit 3,2 W. Ni la puissance, ni la charge, ni l'artefact
inductif de la chaîne de mesure : la quantification.

### La correction

HRPWM. Le MEP subdivise un count en pas de ~142 ps, soit **116 pas** à
60 MHz (`MEP_ScaleFactor` mesuré). Le quantum de l'étage 2 tombe de 8,3 V
à **0,072 V**.

Le rapport cyclique circule maintenant en **Q8 counts** dans `control.c` et
`pwm.c`. Les gains du PID gardent leur définition d'origine, la conversion
se fait au dernier moment ; `s_accum_max` est inchangé, les deux décalages
se compensent exactement.

### Le résultat

400 V sur 50 kΩ tient, là où la carte coupait toutes les trois à sept
secondes. **À confirmer sur une durée supérieure à la minute** — la loi de
probabilité d'avant donnait plusieurs coupures dans cet intervalle.

---

## 3. Les trois pièges, tous silencieux

Aucun des trois ne produit d'erreur. Tout paraît correct sauf le résultat.
Ils ont coûté l'essentiel du temps de la campagne.

### 3.1 `AllowInterruptsWhenHalted` dans `.theia/launch.json`

L'IDE avait inscrit sur la configuration de lancement du dualboost, **et
sur elle seule** :

```xml
<property id="AllowInterruptsWhenHalted"><curValue>1</curValue>
```

C'est le **mode temps réel**. Conséquence : `Error -1133, Device blocked
debug access because it is currently executing non-debuggable code` à
chaque connexion, et impossibilité totale de charger le programme. Une
soirée perdue.

Le témoin qui a tranché : **`TMS-test-io`**, projet frère sans
`launch.json`, donc aux valeurs par défaut, se chargeait sans difficulté
sur la même carte avec la même sonde à la même minute.

> **Méthode à retenir** : deux projets, même cible, comportements
> différents ⇒ la cause est dans le projet, pas dans le matériel. Ce
> témoin existait depuis le début et n'a été utilisé qu'après des heures.

Le `Test Connection` passait intégralement pendant tout ce temps — IR
38 bits, six motifs d'intégrité sans erreur. **Quand le JTAG est bon et que
la connexion échoue, regarder la configuration avant de soupçonner le
silicium.**

**Contournement manuel** si le cas revient : tenir RST à la masse pendant
la connexion et le **relâcher avant le chargement**. `Error -1137, Device
is held in reset` signale qu'on est connecté et qu'il faut lâcher.

**Correctif permanent en place** : `BOOT_DEBUG_WINDOW_MS = 500` dans
`calib.h` — 500 ms de boucle vide avant `EINT`, tout étant déjà à l'état
sûr. La sonde dispose d'une marge franche à chaque démarrage.

### 3.2 `SFO()` referme EALLOW derrière elle

`HRMSTEP` est un registre protégé. Un `EALLOW` posé **avant** l'appel à
`SFO()` ne couvre plus l'écriture qui le suit : elle est rejetée sans le
moindre retour. Il faut le réarmer juste avant.

Ce qui a mis sur la voie : la **même écriture depuis le débogueur**, qui
ignore la protection, passait du premier coup.

### 3.3 La bibliothèque SFO ne recopie pas `MEP_ScaleFactor`

Son en-tête affirme pourtant que `SFO()` *« updates HRMSTEP register with
MEP_ScaleFactor value »*. Avec `SFO_TI_Build_V6.lib` sur f2802x, c'est
faux : mesuré au banc, `MEP_ScaleFactor = 116` en RAM et `HRMSTEP = 0`
dans le périphérique, EALLOW ouvert.

Or avec AUTOCONV, `HRMSTEP` est le **multiplicateur** de la fraction. À
zéro, la fraction est multipliée par rien. Tout paraît juste —
calibration réussie, `HRCNFG` correct, `CMPAHR` renseigné — et le rapport
cyclique ne bouge pas d'un millième.

`sfo_step()` dans `pwm.c` fait désormais la recopie explicitement.

### 3.4 (bonus) `s_hrpwm_ok` jamais relevé

`pwm_hrpwm_service()` ne levait le drapeau que dans `pwm_hrpwm_init()`.
Quand la calibration ne tenait pas dans le budget borné de l'init, le
drapeau restait faux **définitivement** et la fraction était écartée pour
toujours, calibration valide ou non. Corrigé.

---

## 4. `EDGMODE = HR_REP` — tranché par la mesure

Le point qui exigeait une vérification au banc, parce que `DBCTL.POLSEL =
DB_ACTV_LO` (driver UCC27517 inverseur) échange les fronts.

Protocole : duty figé à `CMPA = 150` sur `TBPRD = 299`, donc 50 % exact,
sortie EPWM1A observée sur **GPIO0 / broche 29**, `Stage1-EN` maintenu bas
donc aucune conversion. Mesure par le **rapport cyclique** et non à l'œil :
le déplacement attendu est de 16,6 ns, soit moins de deux points
d'échantillonnage du Discovery2, alors que la lecture de PosDuty est
reproductible à 0,003 point.

| fraction | PosDuty | déplacement | attendu |
|---|---|---|---|
| 0 | 49,985 % | référence | — |
| 255, `HR_FEP` | 50,3 % | **+0,32** | mauvais sens |
| 255, `HR_REP` | 49,68 % | **−0,305** | −0,332 ✓ |
| 128, `HR_REP` | 49,82 % | **−0,165** | −0,166 ✓ |

Le MEP ne sait que **retarder**. Avec `HR_FEP` le niveau haut s'allongeait :
le front retardé était celui de `CTR = 0`, que CMPA ne commande pas.
`HR_FEP` agissait donc sur le signal **de la broche**, après l'inversion du
Dead-Band — **le MEP est inséré en aval de ce sous-module**. L'inversion
échangeant les fronts, celui de CMPA est le montant : `HR_REP`.

La linéarité en trois points est la propriété qui compte : une commande non
monotone ne converge pas.

> **À revérifier si `DBCTL.POLSEL` change un jour** : les deux réglages
> sont liés.

---

## 5. Le blocage actuel : `overI1` au-dessus de 400 V

### Ce n'est pas du courant réel

À 500 V sur 50 kΩ : 10 mA, soit 5 W. Sous 12,2 V d'entrée et 70 % de
rendement, 0,58 A moyen. L'ondulation dans L1 (47 µH, 200 kHz, D = 0,756) :

```
ΔI = Vin·D·T/L = 12,2 × 0,756 × 5 µs / 47 µH ≈ 0,98 A
```

soit un **crête à 1,1 A pour un seuil à 4 A**. Il manque un facteur 3,6.

C'est l'artefact déjà caractérisé le 22/08 : la pointe `L·di/dt` injectée
**en aval** de l'amplificateur de shunt. Elle croît avec le courant
commuté, donc elle réapparaît dès qu'on monte. Le 10 nF posé à la broche
l'a atténuée sans la supprimer.

> **Ne pas poursuivre dans la voie du filtrage** — c'est écrit dans
> `calib.h` et ça reste vrai. La protection agit déjà sur la moyenne et non
> sur le crête.

### La réponse à explorer EN PRIORITÉ : inverser la convention de l'AQ

Idée de Nicolas, 23/08/2026. Elle **invalide** une affirmation écrite dans
`calib.h` — que la position des fronts de coupure serait « structurellement »
impossible à maîtriser. C'est faux : elle ne l'est qu'avec la convention
actuelle.

**Aujourd'hui** : `AQCTLA.ZRO = AQ_SET`, `AQCTLA.CAU = AQ_CLEAR`. La
conduction occupe `0 → CMPA`. Le MOSFET s'amorce à `CTR = 0` (fixe) et se
**bloque sur CMPA** (mobile). C'est donc le front destructeur qui se
déplace, puisque c'est lui qui interrompt le courant d'inductance.

**Proposition** : `AQCTLA.ZRO = AQ_CLEAR`, `AQCTLA.CAU = AQ_SET`. La
conduction occupe `CMPA → PRD`. Le MOSFET s'amorce sur CMPA (mobile) et se
**bloque au passage à zéro** (fixe). L'amorçage rayonne beaucoup moins : il
établit le courant dans une inductance, il ne l'interrompt pas.

Trois gains d'un coup :

1. **Le blanking devient trivial.** La fenêtre s'ancre sur
   `PULSESEL = DC_PULSESEL_ZERO` avec un décalage constant. Tout le
   contournement par `DCFOFFSET` piloté depuis l'ISR (décrit plus bas)
   disparaît.
2. **Le déphasage entre les deux étages redevient utile.** Les deux fronts
   de coupure étant fixes, ils sont séparables à **tous** les points de
   fonctionnement — ce que `PWM_STAGE2_PHASE_COUNTS` donne aujourd'hui pour
   impossible.
3. **L'instant d'échantillonnage de l'ADC** cesse de dériver par rapport à
   la commutation.

#### Ce qu'il faut traiter

**Le sens du rapport cyclique s'inverse** : `CMPA = période − duty`. Une
soustraction dans `pwm_set_duty_q8()`.

**`PWM_HRPWM_EDGMODE` doit repasser à `HR_FEP`.** À la broche, CMPA
produirait désormais le front descendant. À revérifier avec le protocole
exact du §4 — duty figé, mesure par PosDuty, dix minutes.

**`pwm_apply_adc_trigger()` doit être refait.** La mi-conduction n'est plus
`CMPA/2` mais `(CMPA + PRD)/2`.

**LE PIÈGE SÉRIEUX.** Aujourd'hui `duty = 0` s'écrit `CMPA = 0`. Après
inversion, `CMPA = 0` signifierait **conduction pendant toute la période**.
L'état de repos et l'état de pleine conduction échangent leur codage.

L'inhibition réelle passe par `AQCSFRC`, qui force la sortie indépendamment
de CMPA, donc le chemin de sécurité tient. Mais tout endroit du code qui
écrirait un zéro « par prudence » produirait exactement l'inverse de ce
qu'il croit faire. C'est le genre de renversement qui coûte un MOSFET, et
il y en a déjà eu un sur ce banc.

Traitement : un `#define` pour la valeur de repos, un garde-fou de
compilation, et le commentaire qui l'explique. À faire **délibérément**.

#### Pourquoi en priorité

Ça supprime la cause au lieu de masquer l'effet, et ça rend le blanking
beaucoup plus simple si on le fait quand même. Mais c'est un changement de
convention qui touche `pwm.c`, `control.c`, le déclenchement ADC et l'état
de repos : **à faire en début de session, à froid**, pas en fin de
campagne.

### La solution de repli : le blanking

Vérifié dans les en-têtes du F2802x le 23/08. **Oui, la fenêtre
d'aveuglement peut alimenter l'événement que `TZSEL` utilise déjà** — pas
besoin de déménager vers `DCAEVT2`. Le maillon est `DCACTL.EVT1SRCSEL`.

```
COMP1OUT → DCAH → TZDCSEL.DCAEVT1 → [bloc filtre] → DCAEVT1 → TZSEL → OST
                                          ↑                ↑
                                    DCFCTL.SRCSEL    DCACTL.EVT1SRCSEL
```

À ajouter dans `safety.c`, en plus de ce qui existe :

```c
EPwm1Regs.DCFCTL.bit.SRCSEL     = DC_SRC_DCAEVT1;   // le filtre prend DCAEVT1
EPwm1Regs.DCFCTL.bit.BLANKE     = 1;                // fenetre d'aveuglement
EPwm1Regs.DCFCTL.bit.PULSESEL   = DC_PULSESEL_ZERO;
EPwm1Regs.DCACTL.bit.EVT1SRCSEL = DC_EVT_FLT;       // DCAEVT1 prend le FILTRE
```

plus `DCFOFFSET` (retard avant la fenêtre) et `DCFWINDOW` (largeur), tous
deux en counts de TBCLK. Idem sur `EPwm2Regs`.

### La limitation, et son contournement

`PULSESEL` ne propose que **`CTR = 0`** ou **`CTR = PRD`**. La fenêtre est
donc ancrée sur un instant fixe avec un décalage constant.

Or le front qui rayonne le plus est celui de **CMPA**, la coupure, et il se
déplace avec le rapport cyclique. Même problème structurel que le déphasage
des deux PWM : aucun décalage constant ne suit un front mobile.

**Mais `DCFOFFSET` est un simple registre.** L'ISR écrit déjà `CMPB` en le
calculant depuis `CMPA` à chaque pas — `pwm_apply_adc_trigger()` fait
exactement ça. Écrire `DCFOFFSET = CMPA − quelques counts` de la même
manière fait **suivre la fenêtre au front de coupure**, pour une
soustraction de plus.

### Le prix, à dire clairement

Pendant la fenêtre, **la protection est aveugle** — c'est le moment précis
où un court-circuit franc se manifesterait. À dimensionner au plus juste :
la pointe parasite dure ~150 ns et `SAFETY_COMP_QUALSEL = 31` impose déjà
0,5 µs de qualification continue. Une fenêtre de 500 ns à 1 µs devrait
suffire, **à confirmer au scope**.

C'est un vrai compromis de sécurité, pas un réglage anodin.

---

## 6. Le plafond de puissance, à décider avant la suite

Le seuil comparateur est à **4 A crête sur les deux voies**
(`SAFETY_ISHUNT_THRESHOLD_A`, `calib.h:787`). L'étage 2 est confortable :
à 75 W sous 50 V, 1,5 A moyen, 2 A crête.

C'est **l'étage 1** qui plafonne, tout le courant d'entrée passant dans son
inductance. À 75 % de rendement global :

| sortie | P entrée | Iin à 20 V | marge / 4 A |
|---|---|---|---|
| 300 V / 30 mA — 9 W | 12 W | 0,6 A | large |
| 500 V / 50 mA — 25 W | 33 W | 1,7 A | large |
| 500 V / 100 mA — 50 W | 67 W | 3,3 A | **serré** |
| 500 V / 150 mA — 75 W | 100 W | 5,0 A | **dépassé** |

Le mur tombe entre 100 et 150 mA. Pour aller au bout il faut **monter Vin
vers 28-30 V**, ce qui ramène 100 W à 3,5 A.

> **À vérifier avant de monter Vin** : tenue en tension des condensateurs
> d'entrée et du MOSFET étage 1. C'est le seul point du plan qui peut
> coûter un composant.

### La charge

Le rhéostat 10 kΩ / 10 W ne passe pas la première marche : à 500 V il
dissiperait 25 W. Proposition retenue : **deux résistances à corps
aluminium 1,5 kΩ / 100 W en série** sur une plaque — 3 kΩ, 250 V par
élément, une seule pièce mécanique.

Escalier proposé, avec **délestage volontaire à chaque marche** avant de
passer à la suivante (il ne reste que 20 V de marge sous le seuil à 520 V,
et l'étage 2 n'a jamais été délesté) :

- **A** — 300 V / 10 kΩ, 9 W. Passe avec le rhéostat actuel, aucun achat.
  Surtout : **mesurer le rendement réel**, dont dépend tout le tableau.
- **B** — 500 V / 10 kΩ, 25 W.
- **C** — 500 V / 5 kΩ, 50 W. Blanking en place, dissipateur nécessaire.
- **D** — 500 V / 3,3 kΩ, 75 W. Après décision sur Vin.

---

## 7. Autres points ouverts

**Verrouillage total des défauts.** Un défaut latche jusqu'au cycle
d'alimentation (PROMPT §8). `update_overtemp()` implémente pourtant une
hystérésis pour que `s_overtemp_t2` retombe au refroidissement — mais
`control_trip()` latche par-dessus, donc **l'hystérésis ne peut jamais
agir**. Cette incohérence a coûté trois manipulations sur la campagne. Une
modification a été proposée trois fois et **n'a pas été autorisée** : ne
pas la faire sans accord explicite.

**Voies NTC non tamponnées.** `adc.h` documente déjà `ADC_CH_T1` / `T2`
comme « non tamponnée, lente ». Un seul échantillon aberrant verrouille la
carte (défaut code 5 constaté le 22/08, alors que la température réelle
s'est stabilisée à 44 °C pour un seuil à 80 °C). Correctifs : **100 nF au
point milieu de chaque pont** (matériel) et une temporisation à N
échantillons consécutifs sur le modèle de `CTRL_VIN_UV_COUNTS` (logiciel).
Ni l'un ni l'autre n'est fait.

**Angle mort de la protection de sous-tension.** `s_vin_armed` ne passe à
vrai qu'après un premier franchissement de 12 V. Une alimentation qui
démarre faible et le reste n'est jamais surveillée — constaté le 23/08 avec
VIN à 6,7 V et l'étage 1 saturé à 95 % sans aucune coupure. Le compromis
est assumé et documenté, mais l'angle mort est réel. Piste : temporisation
d'armement plutôt que seuil.

**Sessions de débogage instables quand la puissance commute.** Elles
tenaient le matin, puissance coupée, et tombaient toutes les deux ou trois
lectures dès que la carte convertissait. Couplage du bruit de commutation
dans le JTAG très probable — le même mécanisme a déjà été identifié sur les
voies analogiques (nappe, `adc.h`). Pistes : raccourcir le câble JTAG,
descendre TCLK sous 1 MHz.

**Plus anciens, non traités** : sortir LED1 du pont de mesure ;
caractériser la compression de IOUT (−12 % de 27 à 104 mA) ;
`data/carte-puissance/zxct1109.yaml` annonce R3 = 1,5 kΩ là où la netlist
dit 470 Ω ; transmettre `docs/claudeesp32-corrections-ihm.md` à la session
ESP32.

---

## 8. Outils

**`TMS-test-io`** — projet frère, même cible. Toutes les broches en entrée,
LED et UART. Un mode ajouté le 23/08 (`TEST_PWM_ON_GPIO2`) sort un créneau
**100 kHz / 50 % permanent sur GPIO2, broche 37**, sans régulation ni
machine d'état. C'est lui qui a mené à la piste coupée.

- `Stage2-EN` (broche 26) est forcée **basse** au démarrage ; écrire
  `g_stage2_en = 1` au débogueur pour ouvrir la porte.
- **Aucune inversion de polarité** dans ce mode, contrairement au firmware
  réel : la grille sera en opposition de phase avec la broche 37.
- **Étage de puissance hors tension obligatoire** — aucune protection.

**`PWM_HRPWM_EDGE_TEST`** dans `calib.h` — expose `g_hr_test_coarse` et
`g_hr_test_frac`, fige le duty de l'étage 1 et maintient `Stage1-EN` bas.
Exige `PWM_HRPWM_STAGE1 = 1`, sans quoi `pwm_set_duty_q8()` écarte la
fraction et l'essai ne montre rien.

---

## 9. Pièges de lecture, pour ne pas les refaire

**Le rapport cyclique à la broche est inversé.** À 95 % de conduction, la
broche est **basse 95 % du temps** : on n'y voit que des impulsions de
500 ns. Sur une base de temps lente, ça ressemble à un niveau 0 continu. Un
signal à « 96 % haut » sur l'étage 2 correspond à **4 % de conduction**,
donc à un étage quasiment au repos — et c'est l'état correct.

**L'étage 2 ne démarre qu'après l'étage 1.** `control_tick()` appelle
`stage_reset(S2)` à chaque pas tant que V1 n'est pas établie pendant
`CTRL_SETTLE_STEPS`. Avec la puissance débranchée, V1 ne monte pas, donc
**PWM2 ne sortira jamais** — ce n'est pas une panne.

**Les lectures de registres asynchrones se contredisent.** Prendre `s_state`
puis `AQCSFRC` à quelques secondes d'écart sur une machine qui bascule mène
à des conclusions fausses ; ça s'est produit plusieurs fois. Le bon
instrument est la **télémétrie** (trame de 300 ms, grandeurs cohérentes
entre elles) ou un **halt** du cœur pour figer un instantané.

**`VOUT` et `VOSP` se ressemblent dans la trame.** Un serveur affichant
200 V alors que le multimètre en lit 50 sur V1 n'est pas forcément en
défaut : vérifier lequel des deux champs est affiché.

---

## 10. Ordre proposé pour la reprise

1. **Confirmer l'état de la piste réparée**, et que 400 V / 50 kΩ tient
   plus d'une minute. C'est la validation qui manque à HRPWM.
2. **Inverser la convention de l'Action Qualifier** (§5, en priorité) pour
   rendre le front de coupure fixe. À faire à froid, en début de session,
   en traitant explicitement le renversement du codage de `duty = 0`.
   Revérifier `EDGMODE` derrière, avec le protocole du §4.
3. **Mesurer ce que ça donne au-dessus de 400 V.** Si le front de coupure
   fixe suffit à faire disparaître le `overI1`, le blanking devient
   inutile — et on aura supprimé la cause au lieu de masquer l'effet.
4. **Le blanking seulement si nécessaire** (§5, repli). Il sera de toute
   façon plus simple : fenêtre ancrée au passage à zéro, sans piloter
   `DCFOFFSET` depuis l'ISR.
5. **Marche A** de l'escalier (§6) : 300 V / 10 kΩ, et surtout mesurer le
   rendement réel.
6. Décider de Vin avant les marches C et D.

Les correctifs de confort — temporisation NTC, armement de la sous-tension
— peuvent s'intercaler à tout moment ; ils ne bloquent rien.
