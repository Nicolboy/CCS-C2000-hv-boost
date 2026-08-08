# Mise au point de l'étage 1 — journal de débogage

Compte rendu de la session qui a amené l'étage 1 d'un convertisseur qui ne
sortait aucun PWM à une régulation propre sur toute sa plage, 15 à 50 V, avec
un rendement mesuré de 90 %.

Sept défauts distincts ont été trouvés. Aucun n'était visible depuis le code
seul ; tous ont demandé une mesure sur cible. Plusieurs se masquaient
mutuellement, ce qui explique le nombre de fausses pistes — elles sont
consignées ici volontairement, parce qu'elles disent ce que la mesure a
réellement départagé.

---

## 1. Aucune sortie PWM

**Symptôme.** Rien en sortie du TMS320, même en actionnant HV_EN dans
l'interface web.

**Cause.** L'ESP32 n'envoyait jamais `RUN=1`. `HT` ne démarre rien : le
chemin de puissance n'est armé que sur `RUN`, et `HT` n'est consulté
qu'ensuite, pour autoriser `HV_EN` une fois les étages établis. Le bouton de
l'IHM était câblé sur le mauvais tag.

Second défaut simultané : `V1SET=0.0`, hors de la plage 15-50 V, donc rejeté
à chaque trame. `REJ` s'incrémentait sans que personne ne le regarde.

**Diagnostic.** Lecture de la trame `$C` directement dans le tampon de
réception du TMS320, au débogueur, décodée depuis les codes ASCII :

```
$C,HT=1,V1SET=0.0,VOSET=0.0,RUN=0*69
```

**Leçon.** `REJ` était visible en télémétrie depuis le début et signalait le
problème. Un compteur de refus qui s'incrémente veut dire que ce qui est
affiché n'est pas ce qui est appliqué — c'est exactement le cas de figure qui
rend un dysfonctionnement indéchiffrable. L'IHM doit l'afficher.

---

## 2. PWM en salves séparées de longs trous

**Symptôme.** Au scope, quelques cycles de PWM puis un trou d'environ 70 µs,
en répétition.

**Cause.** `enable_power_path()` remettait le duty à zéro, et elle était
appelée à **chaque tour de boucle principale** tant que `RUN` était vrai —
alors que cette mise à zéro ne vaut qu'à l'armement. La boucle principale
écrasait donc en permanence le duty calculé par `control.c`, qui ne le
relevait qu'au tick de régulation décimé.

Reste de l'époque du harnais de bring-up, où le duty était imposé depuis
`main.c` et où la fonction n'était appelée qu'une fois.

**Correction.** Drapeau `s_power_path_armed`, remis à `false` par
`enter_safe_state()` — seul chemin de retour au repos — et à la reprise après
EMUSTOP.

---

## 3. Driver de grille inverseur

**Contexte.** Le passage à l'UCC27517 inverse la logique entre `IN` et `OUT` :
un niveau **bas** sur la broche ePWM correspond au MOSFET **passant**.

**Ce qui aurait été dangereux.** Inverser seulement la sortie ePWM aurait
retourné toutes les protections. `TZCTL` était réglé sur `TZ_FORCE_LO` :
sur défaut, la broche serait passée à l'état bas, donc le MOSFET **pleinement
passant**. La protection serait devenue le court-circuit.

**Correction, en trois parties indissociables.**

L'inversion est faite par `DBCTL.POLSEL = DB_ACTV_LO`, seul vrai bit de
polarité de l'ePWM. `DB_ACTV_LO` inverse les deux sorties ; `EPWMxB` n'étant
pas routé, c'est sans effet de bord et ça lève l'ambiguïté sur celui des codes
qui vise la voie A — se tromper aurait rendu le MOSFET passant en permanence.

Le placement dans la chaîne commande le reste. L'ordre des sous-modules est
`AQ → DB → chopper → TZ → broche` :

- `AQCSFRC` est **en amont** : son forçage à l'état bas ressort haut sur la
  broche, donc MOSFET bloqué. Inchangé.
- `TZCTL` est **en aval** : bascule en `TZ_FORCE_HI`.

Enfin, le multiplexage des broches vers l'ePWM a été déplacé en fin de
`pwm_init()`, une fois les sorties déjà forcées. Il ouvrait sinon une fenêtre
où un ePWM encore vierge — Action Qualifier à zéro, donc sortie basse —
rendait le MOSFET passant pendant toute la configuration.

**Vérification sur cible.** Broche à 1 pendant une halte du cœur, donc MOSFET
bloqué. `TZFLG = 5` : `INT + OST`, sans `DCAEVT1`, c'est bien EMUSTOP.

**Note matérielle.** Les pull-downs prévus sur les lignes PWM vers les drivers
doivent devenir des **pull-ups**. Ceux des entrées des portes ET restent
corrects : elles pilotent `EN`, actif à l'état haut, non concerné par
l'inversion.

---

## 4. Surintensité à chaque reprise après EMUSTOP

**Symptôme.** Toute mise en pause du débogueur provoquait un déclenchement de
surintensité verrouillé à la reprise, à 10 V d'entrée.

**Cause.** `enable_power_path()` remettait bien `CMPA` à zéro, mais
**l'accumulateur de l'intégrateur n'était pas réinitialisé**. Dès le premier
tick de régulation il réimposait le duty d'avant la halte — environ 70 % —
sur une sortie qui s'était vidée dans la charge pendant l'arrêt.

Redémarrer un boost à fort duty sur une sortie effondrée est un emballement
garanti : la désaimantation pendant le temps bloqué, proportionnelle à
`(V1 − Vin)`, est quasi nulle alors que la magnétisation reste entière. Le
courant monte de 0,75 A par période sans jamais redescendre — quatre cycles
suffisent à franchir 3 A.

**Correction.** `control_restart()`, qui ramène la machine d'état à `IDLE` et
vide l'intégrateur. La conversion repart alors par la séquence de démarrage
complète, avec une rampe partant de la tension **mesurée** — ce que la
consigne d'origine demandait sans que ce soit réellement appliqué.

`control_set_run(false)` n'aurait pas convenu : ce drapeau n'est relu qu'au
prochain pas de régulation, et la boucle principale l'aurait repassé à `true`
avant que l'ISR ne l'ait vu.

**Portée.** Ce n'était pas un artefact de débogage. N'importe quelle halte du
cœur produisait la même surintensité au redémarrage.

---

## 5. Mesure de `V1` corrompue — le défaut le plus coûteux

**Symptôme.** Télémétrie sautant entre deux valeurs, 29,6 et 39,6 V, alors que
l'oscilloscope mesurait **30,185 V continus avec 77 mV d'ondulation**. La
boucle régulait sur la moyenne d'une mesure bimodale et stabilisait la sortie
à 30 V en croyant être à 35.

**Ce qui a été éliminé par la mesure**, et non par le raisonnement :

- l'instant d'échantillonnage — retour au déclenchement sur zéro, l'erreur
  persiste ;
- la fenêtre d'acquisition — portée de 433 ns à 1,07 µs, l'erreur persiste ;
- la recopie de l'ISR — un simple `s_raw[i] = ADCRESULTi`, sans décalage ;
- un artefact du débogueur — le registre est stable pendant la halte.

**Cause.** Sonde sur la broche 16, l'entrée `ADCINB4` : le pont diviseur donne
la bonne valeur continue, mais la broche porte des pointes de commutation.
L'AC RMS passe de **3,35 mV à l'entrée du suiveur à 8,06 mV à sa sortie** : le
suiveur ne transmet pas une perturbation venue de l'amont, il en **fabrique**.

Deux mécanismes possibles, même remède : instabilité du suiveur sur charge
capacitive, ou réinjection de charge du condensateur d'échantillonnage de
l'ADC. Le filtre de 100 pF présent était **en amont** de l'amplificateur, du
mauvais côté du problème.

**Correction matérielle.** `1 kΩ` en série depuis la sortie du suiveur,
`10 nF` de la broche à la masse analogique. La résistance isole le suiveur de
la charge capacitive, le condensateur fournit localement la charge
d'échantillonnage.

**Indice décisif.** La tension de sortie est passée de 30,2 à 32,3 V quand la
fenêtre d'acquisition a été modifiée depuis le débogueur. Changer la durée
d'échantillonnage change la probabilité d'attraper une pointe, donc l'erreur
moyenne, donc le point de régulation. Aucune modification de la boucle ne peut
produire cet effet — seule une mesure corrompue le peut.

---

## 6. Étalonnage

Refait **après** l'ajout des RC, sur deux points en charge référencés au
voltmètre et à l'ampèremètre. Tout étalonnage antérieur à ce filtre est à
jeter, y compris une courbe de rendement à 87 % qui reposait sur une mesure de
`V1` corrompue.

| Constante | Avant | Après | Correction |
|---|---|---|---|
| `MEAS_VIN_GAIN_V_PER_V` | 11,11 | 11,00 | −1,0 % |
| `MEAS_V1_GAIN_V_PER_V` | 31,25 | 31,32 | +0,2 % |
| `MEAS_IIN_A_PER_V` | 1,2019 | 1,568 | **+30 %** |

L'erreur de 30 % sur `IIN` donnait des rendements supérieurs à 100 %, ce qui
est précisément ce qui a mis sur la piste. Une valeur physiquement impossible
est le meilleur des signaux.

**Gain seul, offset supposé nul, et c'est un choix délibéré.** Les deux points
impliqueraient un offset de 25 counts sur `IIN` et 37 sur `VIN`, soit 30 mV
ramenés à la broche — sept fois l'offset maximal d'un MCP6001. Un tel écart ne
peut pas être un offset d'amplificateur : le modèle à deux points capte donc
autre chose, et le figer reviendrait à graver une erreur de mesure. Écart
résiduel avec le gain seul : moins de 2 %. **Un troisième point trancherait.**

---

## 7. Instant d'échantillonnage de l'ADC

Le déclenchement était sur `CTR=ZERO`, avec deux défauts : `VIN`, première
voie de la séquence, était échantillonnée exactement sur le front de mise en
conduction ; et l'instant de blocage, qui se déplace avec le duty, tombait
dans la fenêtre d'une voie ou d'une autre selon la charge.

**Correction.** Déclenchement sur `CTRU=CMPB`, avec
`CMPB = (CMPA >> 1) − ADC_TRIG_LEAD_COUNTS` recalculé à chaque écriture du
duty. Décalage, soustraction et comparaison de garde uniquement : utilisable
depuis l'ISR. `CMPB` en mode ombre chargé au passage à zéro comme `CMPA`,
sinon l'instant de mesure correspondrait à un duty différent de celui appliqué
dans la période.

Le milieu de la conduction est le point le plus éloigné des **deux** fronts,
donc le plus calme du cycle. C'est aussi l'instant où le shunt, placé dans la
source du MOSFET, donne la moyenne du courant sur la phase de conduction — la
rampe étant linéaire.

**Attention :** cette valeur n'est le courant moyen d'**entrée** qu'en
conduction continue. En DCM il existe une phase à courant nul, et l'échantillon
surestime alors le courant d'entrée du rapport `1/(D+D2)`. Le rendement se
mesure de toute façon sur les voies post-condensateur, pas sur les shunts.

**Réorganisation.** Les quatre voies qui comptent — `I1`, `V1`, `VIN`, `IIN` —
passent en tête, avec une fenêtre d'acquisition ramenée de 433 à 117 ns. C'est
le RC qui l'autorise : le condensateur d'échantillonnage se charge désormais
depuis les 10 nF locaux. Une voie tombe à 550 ns, la séquence complète à
5,58 µs contre 7,8 — elle tient maintenant dans une période de découpage.

Le groupe est **centré** sur le milieu de la conduction, pas aligné dessus :
`CMPB` recule aussi du demi-étalement du groupe, sans quoi les trois dernières
voies dériveraient vers le blocage.

**Limite connue.** Les 2,2 µs du groupe critique ne tiennent dans la conduction
que si celle-ci dépasse ~2,5 µs, soit un rapport cyclique supérieur à 0,5 à
200 kHz. C'est une limite de l'ADC, pas du réglage.

---

## 8. Déclenchement du comparateur sur des pointes invisibles

**Symptôme.** Impossible de dépasser 40 V. À 45 V, le courant enflait
progressivement pendant 170 ms puis la protection coupait — alors que le crête
visible atteignait 0,85 V pour un seuil à 1,893 V.

**Fausses pistes écartées.** Saturation d'inductance (47 µH/8 A, hors de
cause), transitoire sur `Vin` (le défaut survient aussi hors changement),
erreur de gain de la chaîne `I1`, erreur d'échelle du DAC du comparateur.

**Méthode qui a tranché.** Le comparateur a été utilisé comme **détecteur de
crête à 30 ns**, en abaissant `DACVAL` par paliers, convertisseur stable à
35 V :

| `DACVAL` | Seuil | Résultat |
|---|---|---|
| 300 | 0,968 V | ne déclenche pas |
| 250 | 0,806 V | **déclenche** |

Le sommet visible de la rampe ne dépasse pas 0,50 V. Le comparateur voit donc
une excursion entre 0,81 et 0,97 V, soit **0,35 V de dépassement** invisible
sur les captures.

**Pourquoi elle était invisible.** Non pas par manque de bande passante —
l'Analog Discovery monte à 30 MHz — mais par **longueur d'enregistrement**. Le
tampon fait 8192 points : sur une fenêtre de 1 ms, la cadence tombe à
8,2 MHz, soit un point toutes les 122 ns. Une impulsion de quelques dizaines
de nanosecondes passe entre deux échantillons. S'y ajoutent le mode de
décimation, qui doit être en crête ou min/max pour conserver les événements
brefs, et la masse de la sonde, dont le fil crocodile limite la bande utile à
quelques mégahertz quel que soit le calibre annoncé.

**Pointe finalement capturée** à 100 MS/s, 200 ns/div, ressort de masse court,
au point de fonctionnement 10 V / 50 V / 1,296 A :

| | Valeur |
|---|---|
| Sommet utile de la rampe | ~1,22 V |
| **Pointe** | **1,619 V** |
| Largeur | **~25 ns** |
| Seuil de déclenchement | 1,893 V |
| Marge sans qualification | 0,27 V, soit 14 % |

La fenêtre de qualification exige 533 ns contre 25 ns de pointe : **un facteur
20**, la marge est donc très large. Et à 14 % du seuil à 50 V, la pointe aurait
franchi celui-ci en chargeant davantage — la qualification n'est pas un
confort, c'est ce qui rend la plage de fonctionnement accessible.

Un détail confirme le mécanisme : après la pointe le signal ne retombe pas à
zéro comme le ferait un shunt de source au blocage, il décroît lentement, avec
la constante de temps de l'amplificateur (~127 kHz au gain 31,5, soit 1,25 µs).
Un ampli aussi lent ne peut pas *amplifier* une impulsion de 25 ns : elle
arrive sur sa sortie par un autre chemin — couplage capacitif, ou réjection
d'alimentation médiocre en haute fréquence. D'où son invisibilité dans le
signal utile et sa parfaite visibilité pour un comparateur à 30 ns.

Le recoupement à 45 V donnait 1,04 V de dépassement pour un sommet visible de
0,85 V : le parasite grandit environ trois fois quand le courant double.

**Correction.** `SYNCSEL = 1` et `QUALSEL = 31` sur les deux comparateurs. La
bascule exige 32 échantillons SYSCLK consécutifs au-dessus du seuil.

`SYNCSEL` **doit** passer à 1 : la qualification ne porte que sur la sortie
synchronisée. En asynchrone, `QUALSEL` est sans effet — c'est ce qui rendait
le champ inopérant alors qu'il était déjà présent dans le code.

**Coût, chiffré.** La latence passe de ~30 ns à ~550 ns. Mais le shunt étant
dans la source du MOSFET, il ne voit rien pendant le blocage : à 0,81 de duty,
un défaut apparaissant juste après le blocage reste invisible pendant 0,95 µs
quelle que soit la vitesse du comparateur. La latence réelle de bout en bout
passe donc de ~950 ns à ~1500 ns — un facteur 1,6, pas 18.

Sortie en court-circuit, l'inductance ne se désaimante plus et le courant monte
à `di/dt = Vin/L = 0,20 A/µs`. Sur 1,5 µs, cela fait **0,30 A de dépassement** :
coupure vers 3,3 A au lieu de 3,0, pour une inductance donnée à 8 A et un
MOSFET à 16 A.

---

## Résultat

| `Vin` | `V1` | `Iin` | `P_in` | `P_out` | Rendement |
|---|---|---|---|---|---|
| 9,94 V | 35,0 V | 0,620 A | 6,16 W | 5,57 W | 90,4 % |
| 15,07 V | 35,1 V | 0,416 A | 6,27 W | 5,60 W | 89,3 % |
| 9,43 V | 49,6 V | 1,314 A | 12,39 W | 11,18 W | 90,2 % |

Charge de 220 Ω. Rendement remarquablement plat de 35 à 50 V et sur toute la
plage d'entrée. L'étage 1 couvre l'intégralité de sa plage de consigne.

---

## Points ouverts

**Marge du MOSFET.** Le FQD20N06 est calibré 60 V et fonctionne à 49,6 V, soit
21 % de marge. On sait maintenant qu'il existe des transitoires vigoureux dans
la maille de commutation. **La surtension au blocage n'a jamais été mesurée** ;
c'est la première chose à faire avec une sonde rapide. Rester à 45 V pour les
essais prolongés tant que ce n'est pas fait — le passage de 45 à 50 V n'apporte
aucune information nouvelle, le rendement étant plat, et consomme toute la
marge du composant.

**Pas de limitation de courant dans la boucle.** Une consigne inatteignable
fait monter le duty jusqu'au déclenchement verrouillé, sans rien pour écrêter
avant. La Trip Zone sait le faire en mode cycle-par-cycle, qui écourte la
période en cours sans verrouiller. C'est le manque le plus significatif côté
firmware.

**Troisième point d'étalonnage** pour départager gain et offset sur `VIN` et
`IIN` — de préférence en changeant la charge plutôt que l'entrée, pour sortir
franchement du domaine exploré.

**Réglage de la boucle.** `CTRL_SHIFT = 12` n'a jamais été ajusté. L'enveloppe
du courant ondule de ±10 % à quelques kilohertz, signe que la boucle n'est pas
parfaitement stabilisée. Le gain petit-signal `Vin/(1−D)²` croît fortement avec
le rapport cyclique et le zéro du demi-plan droit descend : ce qui est stable à
35 V l'est moins à 50.

**Réglage `V1SET` absent de l'IHM** — spécifié dans `esp32_agent.md`, à
implémenter côté ESP32.

**Étage 2**, quand il sera monté : déclenchement ADC de `I2` sur le `CMPB`
d'ePWM2, et déphasage entre les deux ePWM pour l'EMI. Les deux sont liés et se
décident ensemble, sur mesures. Et le MOSFET de décharge 600 V reste
indispensable avant toute haute tension.
