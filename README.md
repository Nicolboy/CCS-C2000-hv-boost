# TMS320F28027 — alimentation haute tension « dual boost »

Firmware de commande d'une alimentation HT pour préamplis à tubes : deux étages
boost en cascade, 10 V de batterie vers 200–500 V, supervisés par un ESP32-C3.

La protection est matérielle et locale au MCU. L'ESP32 propose des consignes ;
**il n'a aucune autorité de sécurité**.

---

## Architecture

| Étage | Entrée | Sortie | Découpage | Rapport cyclique |
|---|---|---|---|---|
| 1 | 10 V batterie | 15 → 50 V | **200 kHz** | ≈ 0,8 max |
| 2 | sortie étage 1 | 200 → 500 V | **100 kHz** | ≈ 0,93 à 500 V |

```
Orchestrateur --HTTP--> ESP32-C3 --UART 57600 $C/$T--> TMS320F28027
                                                            │
                        comparateurs + Trip Zone (30 ns) ───┴──> drivers
```

Charge visée : 10–15 mA, modulée à fréquence audio.

---

## Ce que fait le firmware

| Module | Rôle |
|---|---|
| `control.c` | boucles de régulation des deux étages |
| `safety.c` | comparateurs, DAC de seuil, Trip Zone |
| `pwm.c` | ePWM, temps morts, blanking |
| `adc.c`, `measure.c` | acquisition et mise à l'échelle |
| `uart_link.c` | liaison ESP32, protocole `$C` / `$T` |
| `bsp_clock.c`, `bsp_gpio.c` | horloge interne 60 MHz, brochage |
| `calib.h` | constantes d'étalonnage, **mesurées et non calculées** |

La coupure passe par les **comparateurs analogiques et la Trip Zone**, avec une
latence de l'ordre de 30 ns. Aucune décision de sécurité ne dépend du logiciel
ni de la liaison série : une surintensité coupe les grilles sans que le CPU
n'ait à intervenir.

---

## État

**Carte CPU** — fonctionnelle, JTAG réglé.
**Carte puissance** — en montage, étage 2 non monté.

L'étalonnage décrit correspond à la **carte de puissance V0.2**. Celle-ci a
changé le brochage analogique et la chaîne de mesure du courant d'entrée —
ZXCT1109 remplacé par un INA293A2 — ce qui invalide tout l'étalonnage réalisé
sur V0.1. Celui-ci est conservé intégralement dans
[`docs/calibration-V0.1.md`](docs/calibration-V0.1.md), constantes et points
bruts compris, à rouvrir tel quel si une carte V0.1 revient en service.

---

## Documentation

- [`hardware.md`](hardware.md) — synthèse matérielle : étages, brochage, chaînes
  de mesure, protections
- [`software.md`](software.md) — synthèse logicielle
- [`docs/`](docs/) — journaux de mise au point, étalonnage, protocole ESP32

Ces deux synthèses sont **reconstituées depuis les sources**. En cas de
divergence, `src/` et `docs/` font foi.

Le dossier `docs/` vaut surtout pour ce qu'il garde des séances de mise au
point — ce qui a été mesuré, ce qui a été écarté, et pourquoi. Par exemple :
les `overI1` venaient du filtre d'entrée et non de la carte ; la configuration
de ce filtre a été retenue sur mesure, pas sur calcul.

---

## Compiler

Projet Code Composer Studio pour **TMS320F28027** (LQFP48 PT), horloge interne
60 MHz — X1/X2 non câblés.

Les fichiers `F2802x_*.c` et `F2802x_*.h` proviennent de **C2000Ware** et
restent sous licence Texas Instruments, de type BSD à trois clauses. Leurs
en-têtes de copyright sont conservés tels quels et doivent le rester.

Le reste — `src/`, la documentation, les fichiers de projet — est à moi.

---

## Ce que ce dépôt ne contient pas

Aucun document constructeur — ni la datasheet du F28027, ni le manuel technique
SPRUI09, ni les datasheets des composants de puissance. Ils sont sous copyright
et ne se redistribuent pas.

[`docs/references.md`](docs/references.md) donne les références exactes et où
les télécharger. Ce qui en a été tiré — constantes, seuils, timings — est dans
`src/calib.h` et `hardware.md`, chacun avec la mesure ou la page qui le fonde.
