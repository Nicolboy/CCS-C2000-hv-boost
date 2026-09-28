# Documents de référence

Ces documents sont **sous copyright de leurs éditeurs** et ne sont pas
redistribués ici. Ils se téléchargent gratuitement chez le fabricant, sans
inscription.

## Texas Instruments — TMS320F28027

| Référence | Document |
|---|---|
| **SPRS523** | *TMS320F2802x Piccolo Microcontrollers* — datasheet : brochage, caractéristiques électriques, timings |
| **SPRUI09** | *TMS320x2802x Piccolo Technical Reference Manual* — registres, ePWM, ADC, comparateurs, Trip Zone |

Point d'entrée : [ti.com/product/TMS320F28027](https://www.ti.com/product/TMS320F28027),
onglet *Technical documentation*.

**Ce que le projet en a tiré**, pour ne pas avoir à les rouvrir :

- Les constantes et les seuils vérifiés sont dans [`../src/calib.h`](../src/calib.h),
  chacun avec la mesure ou la page qui le fonde.
- La synthèse matérielle est dans [`../hardware.md`](../hardware.md).
- Les points restés `à vérifier` le sont explicitement — ils attendent une
  lecture, pas une estimation.

Un point ouvert qui demandera le TRM : **la largeur réelle du champ
`DCFWINDOW`**. L'en-tête C le déclare en `uint16_t` plein, mais le registre
matériel est plus étroit sur certains membres de la famille. C'est ce qui
bloque l'élargissement de la fenêtre de blanking sur l'étage 2.

## Composants de la carte de puissance

Les datasheets des composants — INA293A2, ZXCT1109 sur la V0.1, drivers,
MOSFET, magnétiques — ne sont pas non plus dans ce dépôt. Les références
exactes figurent dans [`../hardware.md`](../hardware.md) ; chaque fabricant
publie la sienne.
