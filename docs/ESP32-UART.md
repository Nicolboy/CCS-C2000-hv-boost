# Liaison UART ESP32-C3 <-> TMS320

Ce document decrit le protocole que le firmware TMS320 doit implementer
pour dialoguer avec l'ESP32-C3 (cote ESP32 deja fonctionnel, voir
`src/tms_link.cpp` / `include/protocol.h`). Le TMS320 envoie sa telemetrie
periodiquement, et recoit periodiquement les commandes de l'operateur.

> **Mise a jour** : coefficients de calibration remplaces par les valeurs
> mesurees sur la carte reelle (voir `mesure-cartepuissance.md`), et ajout
> du champ `FAULT`.

## Cablage

| Signal | ESP32-C3 (config.h) | TMS320 |
|---|---|---|
| ESP32 TX -> TMS320 RX | GPIO7 | GPIO28, broche 48 |
| ESP32 RX <- TMS320 TX | GPIO6 | GPIO29, broche 1 |
| GND commun | GND | GND |

Niveaux logiques 3,3 V des deux cotes -- **verifie, carte CPU validee**.

## Parametres UART

- Vitesse : **57600 bauds**
- Format : **8N1** (8 bits de donnees, pas de parite, 1 bit de stop)
- Cote ESP32 : `Serial1`, defini dans `TmsLink::begin()`

## Format des trames

Protocole ASCII texte, inspire du format NMEA, termine par `\n` (LF).
Chaque trame est encadree par `$` ... `*XX`, ou `XX` est un checksum XOR
en hexadecimal (2 chiffres, majuscules).

### Telemetrie : TMS320 -> ESP32 (periodique)

```
$T,FAULT=0,STATE=4,REJ=0,FREQ1=200000,FREQ2=100000,DUTY1=68.4,DUTY2=0.0,VIN=12.1,IIN=0.61,V1=35.0,I1=0.62,T1=38.4,VOUT=0.3,I2=0.00,T2=36.1,IOUT=0.00,V1SP=35.0,VOSP=0.0*XX
```

(exemple reel en mode **etage 1 seul** : `VOSP=0.0`, `DUTY2=0.0`.)

### Ordre des champs : `FAULT`, `STATE` et `REJ` viennent en premier

Ce n'est pas cosmetique. La trame complete fait environ **175 caracteres
sur les 200** autorises, et le formateur cote TMS320 **abandonne** un champ
qui ne tiendrait pas plutot que de deborder. Comme un champ absent laisse
l'ESP32 sur sa derniere valeur connue, emettre `FAULT` en fin de trame
signifierait qu'un debordement le fait disparaitre -- et que l'ESP32
continue d'afficher `FAULT=0` pendant qu'un defaut reel est actif.

L'etat de securite passe donc avant les mesures : perdre `T2` ou `IOUT` est
sans consequence, perdre `FAULT` ne l'est pas. L'ESP32 ne doit jamais
supposer un ordre autre que celui-ci pour les trois premiers champs.

Champs (`TAG=valeur`, separes par des virgules, ordre libre) :

| Tag | Unite | Description |
|---|---|---|
| `FREQ1` | Hz | Frequence de decoupage etage 1 (200 kHz) |
| `FREQ2` | Hz | Frequence de decoupage etage 2 (100 kHz) |
| `DUTY1` | % | Rapport cyclique etage 1 |
| `DUTY2` | % | Rapport cyclique etage 2 |
| `VIN` | V | Tension d'entree |
| `IIN` | A | Courant d'entree |
| `V1` | V | Tension intermediaire (sortie etage 1) |
| `I1` | A | Courant shunt MOSFET etage 1 |
| `T1` | degC | Temperature NTC etage 1 |
| `VOUT` | V | Tension de sortie HT |
| `I2` | A | Courant shunt MOSFET etage 2 |
| `T2` | degC | Temperature NTC etage 2 |
| `IOUT` | A | Courant de sortie |
| `V1SP` | V | Consigne V_inter **reellement appliquee** |
| `VOSP` | V | Consigne sortie **reellement appliquee** |
| `STATE` | code 0-5 | Etat de la machine de conduite (voir ci-dessous) |
| `REJ` | compteur | Consignes refusees depuis le demarrage |
| `FAULT` | code 0-8 | Defaut courant (voir ci-dessous) |

### Etats `STATE`

| Code | Etat | Signification |
|---|---|---|
| `0` | IDLE | tout coupe, decharge active, en attente de `RUN=1` |
| `1` | START_S1 | etage 1 en montee, consigne rampee depuis VIN |
| `2` | RUN_S1 | V_inter etablie, etage 2 encore a zero -- **transitoire** |
| `3` | START_S2 | etage 2 en montee, consigne rampee depuis V1 |
| `4` | RUN | les deux etages regules -- **seul etat ou `HT=1` est pris en compte** |
| `5` | FAULT | etat sur verrouille |

`STATE=2` ne dure **qu'un seul pas de regulation, soit environ 200 us**.
Aucun interrogateur cadence a la seconde ne l'observera jamais : ne pas
ecrire de logique qui l'attend.

En mode **etage 1 seul** (`VOSET=0`, voir plus bas), la machine passe
directement de `1` a `4` : `2` et `3` ne sont jamais traverses, et `4`
signifie alors « etage 1 etabli », pas « les deux etages etablis ». Le seul
moyen de distinguer les deux cas est `VOSP` : nul en mode etage 1 seul.

Le demarrage est **cascade** : V_inter est etablie et stabilisee avant que
l'etage 2 ne demarre, pour qu'il parte d'une tension d'entree connue.

### Codes `FAULT`

| Code | Signification |
|---|---|
| `0` | Aucun defaut |
| `1` | EMUSTOP -- le debogueur a arrete le CPU |
| `2` | Surintensite I1 (etage 1) |
| `3` | Surintensite I2 (etage 2) |
| `4` | Temperature critique T1 (> 80 degC) |
| `5` | Temperature critique T2 (> 80 degC) |
| `6` | Survoltage V_inter (> 55 V) |
| `7` | Survoltage sortie (> 520 V) |
| `8` | Liaison ESP32 perdue (> 2 s sans trame `$C` valide) |

**Priorite quand plusieurs defauts coexistent :**

```
2 > 3 > 6 > 7 > 4 > 5 > 8 > 1
```

Les survoltages (6, 7) passent **avant** les surtemperatures, et EMUSTOP a
la priorite la plus BASSE malgre son numero : en developpement il se
declenche a chaque halte du debogueur et ne doit jamais masquer une
surintensite reelle. `8` prime sur `1`.

### Defauts verrouilles et defauts transitoires

| Codes | Nature | Effet |
|---|---|---|
| `2` a `7` | **verrouilles** | Coupure definitive, cycle d'alimentation requis |
| `1`, `8` | transitoires | Se relevent seuls |

Un superviseur qui interromprait une campagne de mesure sur **n'importe
quel** `FAULT != 0` s'arreterait a tort sur un simple hoquet de liaison.
Seuls les codes `2` a `7` sont des arrets definitifs.

**Il n'existe volontairement pas de code "court-circuit".** Un
court-circuit franchit le meme comparateur et le meme seuil qu'une
surintensite ; le firmware ne dispose d'aucune information permettant de
les distinguer. Les codes 2 et 3 couvrent les deux cas.

**Le code 8 (liaison perdue) n'est pas verrouille** : au-dela de 2 s sans
trame `$C` valide, le TMS320 replie en etat sur (PWM inhibes, HT coupee,
decharge active) mais repart des que la liaison revient et que `RUN=1` est
redemande. La securite ne depend jamais de l'ESP32, mais une coupure de
liaison n'est pas une avarie de puissance.

#### Verrouillage : pas d'effacement par le protocole

Contrairement a ce que supposait une version precedente de ce document,
**un defaut ne s'efface JAMAIS automatiquement**, ni a la disparition de
sa cause, ni par commande UART. Il n'existe aucun tag `$C` permettant de
l'acquitter.

Le seul moyen de repartir est de **couper puis retablir l'alimentation**.
C'est un choix deliberé (PROMPT §8) : un defaut de puissance doit exiger
une intervention humaine et un examen de la carte.

Consequence pour l'ESP32 : tant que le TMS320 emet un `FAULT != 0`, il
continuera de l'emettre indefiniment. L'IHM doit donc afficher ce code de
maniere persistante, sans attendre un retour spontane a 0.

> Rappel (PROMPT §2) : la protection reelle -- comparateurs + Trip Zone
> materiel -- ne depend jamais de cette liaison UART. `FAULT` n'est qu'un
> report d'information vers l'IHM, envoye apres coup ; la coupure a deja
> eu lieu en materiel avant meme que la trame ne soit construite.

#### Icone danger : pilotee par VOUT, jamais par FAULT

L'avertissement haute tension doit dependre **uniquement de `VOUT`**, et
rester affiche independamment de l'etat de `FAULT`.

Un defaut ne decharge pas le condensateur de sortie. Apres une coupure,
`HV_EN` isole la charge mais les 400 V restent presents. Faire disparaitre
l'icone danger au moment d'un defaut reviendrait a rassurer l'operateur
precisement quand le risque est maximal.

La carte de puissance comporte deux dispositifs complementaires :

- un **bleeder de 1 MOhm avec LED rouge en serie**, indication passive
  qui reste valable alimentation coupee (~42 s pour passer sous 50 V) ;
- un **circuit de decharge actif** (MOSFET + 47 kOhm, ~2 s de 500 V a
  50 V), commande par le TMS320 sur GPIO5 / broche 40, **a logique
  inversee** : il conduit des que la commande disparait, donc au reset,
  en cas de plantage et alimentation coupee.

**Frequence d'envoi : toutes les 300 ms.** L'ESP32 considere la liaison
perdue (`TMS:KO`) si aucune trame `$T` valide n'est recue depuis plus de
**2 secondes**. Le TMS320 applique le meme timeout dans l'autre sens.

Tous les champs sont optionnels individuellement (un champ absent garde
sa derniere valeur cote ESP32) ; en pratique le TMS320 les envoie tous.
Si une trame devait depasser la longueur maximale, le firmware abandonne
des champs entiers plutot que d'en tronquer un.

### Calibration des mesures analogiques (cote TMS320)

Coefficients appliques sur la lecture ADC (`Vadc`, en volts) pour obtenir
la valeur physique. **Valeurs mesurees sur la carte reelle**, sauf mention
contraire -- voir `mesure-cartepuissance.md` pour la tracabilite.

| Grandeur | Pleine echelle (3,3 V) | Relation | Statut |
|---|---|---|---|
| `VIN` | 36,7 V | `VIN = Vadc x 11,11` | mesure |
| `IIN` | 3,97 A | `IIN = Vadc x 1,2019` | mesure (1 point) |
| `V1` | 103,1 V (usage limite a 50 V) | `V1 = Vadc x 31,25` | mesure |
| `VOUT` | 600 V | `VOUT = Vadc x 181,82` | mesure |
| `IOUT` | 50 mA (a confirmer) | `IOUT = Vadc x 0,016667` | **a mesurer** |
| `I1` | 5,15 A | `I1 = (Vadc - 0,0473) / 0,631` | **provisoire (MCP6001)** |
| `I2` | 5,12 A | `I2 = (Vadc - 0,0340) / 0,637` | **provisoire (MCP6001)** |

**Notes :**

- **I1/I2** : le gain differe d'environ 1 % entre les deux voies et chaque
  voie a un offset non nul a courant nul (47,3 / 34,0 mV). Ne jamais
  utiliser un gain unique pour les deux. Ces valeurs sont mesurees avec
  les **MCP6001 de banc** ; le composant definitif est le TLV9151. Le gain
  devrait rester valable (fixe par le reseau de resistances) mais
  **l'offset devra etre integralement remesure**.
- **MCP6001, limite de bande passante** : 1 MHz de produit gain-bande a un
  gain de ~31 plafonne vers 32 kHz. Pour proteger un decoupage a 200 kHz,
  les 30 ns du comparateur ne servent a rien si l'ampli en amont met des
  dizaines de microsecondes. **La protection rapide n'est pas reellement
  rapide tant que le MCP6001 est en place.**
- **NTC** : montage 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
  La NTC etant cote 3,3 V, la relation est
  `R_ntc = R_fixe x (VREF - Vadc) / Vadc`. Les thermistances sont a 5 mm
  des MOSFET : elles mesurent le cuivre, pas la jonction, avec un ecart
  statique et un retard thermique notables. Le seuil de 80 degC est donc
  **provisoire**, a recaler sur la temperature de boitier en charge.
- **IOUT** : aucune mesure reelle a ce jour.

### Commande : ESP32 -> TMS320 (periodique + a chaque changement)

```
$C,RUN=1,V1SET=35.0,VOSET=400.0,HT=1*XX
```

| Tag | Valeurs | Description |
|---|---|---|
| `RUN` | 0 ou 1 | Demande de marche. A 0, retour immediat a l'arret |
| `V1SET` | **15 a 50** | Consigne V_inter, en volts |
| `VOSET` | **200 a 500**, ou **0** | Consigne sortie HT en volts ; `0` desactive l'etage 2 |
| `HT` | 0 ou 1 | Sortie HT. N'a d'effet qu'en `STATE=4` |
| `PWM1`, `PWM2` | 0 ou 1 | **Historiques**, acceptes mais sans effet |

**Tous les tags sont optionnels et un tag inconnu est ignore** : les deux
firmwares peuvent evoluer independamment. Un champ absent garde sa derniere
valeur connue. Une trame ne contenant aucun tag reconnu est rejetee et ne
rafraichit pas le timeout de liaison.

L'ESP32 envoie cette trame toutes les **500 ms**, et immediatement a chaque
changement depuis l'IHM.

#### `VOSET=0` : mode etage 1 seul

`VOSET` **exactement nul** n'est pas une consigne de 0 V, c'est la
convention qui **desactive l'etage 2** :

- la machine s'arrete a `STATE=4` des que l'etage 1 est etabli, sans jamais
  passer par `2` ni `3` ;
- le rapport cyclique de l'etage 2 est force a zero a chaque pas, sa sortie
  ePWM est inhibee et sa porte ET n'est pas armee ;
- **`HT=1` reste sans effet** : il n'y a pas de sortie HT a mettre sous
  tension. `VOSP` renvoie `0.0`.

Ce mode existe parce que sans lui la machine resterait bloquee
indefiniment en `STATE=3`, l'integrateur sature a 95 % : une sortie ne peut
pas atteindre 200 V quand le MOSFET, la diode et l'inductance de l'etage 2
ne sont pas montes. C'est l'etat **par defaut au demarrage** du TMS320.

Zero est sans ambiguite : ce n'est pas une consigne plausible, et toute
valeur strictement comprise entre 0 et 200 reste refusee comme avant. Une
valeur negative est refusee elle aussi.

**Basculer entre les deux modes exige `RUN=0` d'abord.** Une trame qui
activerait ou desactiverait l'etage 2 alors que la machine tourne est
**refusee** (`REJ` s'incremente) : l'activer en marche ferait demarrer
l'etage 2 avec un integrateur et une rampe hors contexte, donc par un
a-coup de rapport cyclique ; le desactiver couperait la sortie sans passer
par l'etat sur. La sequence correcte est `RUN=0`, puis le nouveau `VOSET`,
puis `RUN=1`.

#### Consigne hors bornes : refusee, jamais saturee

Si `V1SET` ou `VOSET` sort de sa plage, **la commande numerique est refusee
en bloc** : les deux anciennes consignes restent appliquees et `REJ`
s'incremente. `RUN` et `HT` de la meme trame restent pris en compte, pour
qu'une consigne aberrante n'empeche jamais un ordre d'arret de passer.

On ne sature pas silencieusement : demander 600 V et obtenir 500 V sans
avertissement laisserait croire que l'alimentation fait ce qu'on lui a
demande. Comparer `VOSET` envoye et `VOSP` recu est le moyen de detecter le
refus.

#### Consignes lentes : c'est l'ESP32 qui orchestre

Le TMS320 ne connait qu'une consigne a la fois et se contente de la reguler.
Une sequence d'essai du type « sortie a 400 V, V_inter de 20 a 35 V par pas
de 5 V » se pilote depuis l'ESP32, en envoyant les consignes une par une et
en attendant la stabilisation entre chaque. Le TMS320 ne contient aucun
sequenceur : il n'y a donc rien a verifier de ce cote, et une liaison perdue
coupe tout immediatement.

Un changement de consigne en marche est suivi **progressivement** : la
rampe interne reste active en `STATE=4`, il n'y a pas de saut de consigne.

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), en hexadecimal majuscule
sur 2 chiffres (`%02X`).

Pour la reception : meme calcul, comparaison au checksum recu, trame
ignoree silencieusement si ca ne correspond pas.

> **Piege rencontre cote TMS320, a ne pas reproduire.** Decoder le
> checksum avec `strtol` sur un buffer non termine par `\0` fait lire
> au-dela de la trame. Et sur C28x, `uint8_t` fait 16 bits (pas
> d'adressage par octet) : un cast `(uint8_t)` ne tronque donc rien, et un
> `"7D"` suivi d'un residu `"7D"` donne 0x7D7D, conserve tel quel. Toute
> trame etait rejetee. Decoder **exactement deux chiffres**, de maniere
> bornee.

## Points de vigilance

- Checksum en **majuscules** sur 2 chiffres, sinon rejet silencieux cote
  ESP32 -- la telemetrie n'avance simplement plus.
- Terminer par `\n` uniquement (`\r` tolere).
- Longueur de ligne maximale : **200 caracteres** (`g_lineBuf[200]` cote
  ESP32, `UART_LINE_MAX` cote TMS320).
