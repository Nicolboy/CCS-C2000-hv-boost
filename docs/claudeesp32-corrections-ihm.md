# Prompt — Corrections IHM web (côté ESP32-C3)

> À donner à une session Claude Code travaillant sur le **firmware ESP32-C3**.
> Fait suite à `claudeesp32-etage2-interface.md`, dont l'implémentation a été
> relue. Rédigé le 26/08/2026 depuis le dépôt TMS320.

---

## 0. Avant tout : ce qui est correct, à ne pas refaire

La relecture de la page servie est **positive sur l'essentiel**. Ne pas
réécrire, ne pas restructurer : les corrections ci-dessous sont ponctuelles.

Sont vérifiés et justes, à laisser tels quels :

- **`FAULT_LABELS` correspond exactement à `fault_code_t`**, index par index
  (`protocol.h` côté TMS320, codes 0 à 9). Ne pas y toucher.
- Les garde-fous : `setPalier()` bloqué en marche, `adjOffset()` refusé quand
  le palier vaut 0, décalage borné à 0-100.
- L'arithmétique de `VOSET` est **entière** (`palier + offset`), comme demandé.
- Le serveur fait autorité : `refresh()` réécrit `state.run` / `state.ht`
  depuis `/api/state` à chaque cycle.
- Le traitement des défauts transitoires (1 EMUSTOP, 8 LINKLOST), qui évite
  d'afficher « KO » pendant une coupure de liaison attendue.

Le firmware **TMS320 ne doit pas être modifié**. Si un besoin semble l'exiger,
le signaler plutôt que de le faire.

---

## 1. Échelles des jauges — valeurs à appliquer

Le tableau `GAUGES` porte des bornes qui ne correspondent plus au matériel.
Valeurs corrigées :

| `key` | `min` | `max` | Change ? | Motif |
|---|---|---|---|---|
| `vin` | 0 | 25 | — | inchangé |
| `iin` | 0 | 3 | — | inchangé |
| `v1` | 0 | **55** | ✅ | était 50 |
| `i1` | 0 | **4** | ✅ | était 3 |
| `vout` | **0** | 500 | ✅ | était `min:200` |
| `i2` | 0 | 3 | — | inchangé |
| `iout` | 0 | **150** | ✅ | était 50 |

### `iout` : 50 → 150 mA

La chaîne de mesure a été **redimensionnée côté matériel** (shunt 10 → 1 Ω,
résistance de gain 1,5 k → 4,7 k). Pleine échelle réelle : **172 mA**. Domaine
d'exploitation retenu : **150 mA sous 200 V, 100 mA sous 500 V**.

À 50 mA la jauge saturait donc à 100 % alors qu'on est au tiers de l'usage
normal — bargraph rouge collé en butée pendant toute l'exploitation.

### `vout` : `min` 200 → 0

`min:200` rendait la jauge **aveugle sous 200 V**. Or pendant toute la mise en
route de l'étage 2, la sortie vaut approximativement V_inter, soit 20 à 50 V :
`(11 − 200) / 300` donne un négatif, écrêté à 0, et le bargraph reste à zéro.

`min:0` est strictement meilleur, y compris en exploitation : une sortie sous
200 V signifie soit une rampe en cours, soit un défaut. Dans les deux cas on
veut la voir bouger.

### `v1` : 50 → 55, et `i1` : 3 → 4 A

Aligner l'échelle sur le **seuil de coupure**, pas sur la consigne :

- `CTRL_V1_OV_TRIP_V` = 55 V ;
- `SAFETY_ISHUNT_THRESHOLD_A` = 4,0 A crête.

Avec `max:50`, la jauge V1 affichait 100 % — donc **rouge** via
`severityColor()` — au point de fonctionnement nominal, puisque `V1SET` vaut
désormais 50,0 V fixe. Un indicateur qui crie au danger en marche normale
finit par ne plus être lu.

Une fois l'échelle calée sur le seuil, le code couleur redevient
interprétable : 90 % de l'échelle = on approche réellement de la coupure.

### `i2` : reste à 3 A, volontairement

**Ne pas « harmoniser » avec `i1`.** L'asymétrie est voulue : l'étage 2 traite
la même puissance sous une tension d'entrée bien plus élevée, son courant est
donc structurellement plus faible. Une échelle 0-3 A y donne une résolution
utile.

Le seuil de coupure reste 4,0 A sur les deux étages. La jauge I2 passe donc au
rouge un peu avant le déclenchement — c'est accepté.

---

## 2. `syncVoset()` éteint le bouton qui vient d'être pressé

**Symptôme.** Appuyer sur **300V**. Une seconde plus tard, `refresh()` reçoit
`vout_sp = 300`, la boucle teste `p = 200`, calcule `off = 100`, accepte — et
l'IHM affiche **200V allumé avec +100V**. Le bouton 300 s'est éteint seul.

**Cause.** Les paliers se recouvrent (200-300, 300-400, 400-500), la
décomposition de VOSP n'est donc pas unique, et la boucle retient le premier
palier dans l'ordre croissant.

**Correctif** — conserver le palier courant tant qu'il reste cohérent, et
balayer du plus haut au plus bas en repli, ce qui donne le décalage le plus
petit, c'est-à-dire la lecture la plus naturelle :

```js
function syncVoset(voutSp){
  if (voutSp === 0) { state.palier = 0; state.offset = 0; return; }
  const keep = Math.round(voutSp - state.palier);
  if (state.palier !== 0 && keep >= 0 && keep <= 100) { state.offset = keep; return; }
  for (const p of [400,300,200]) {
    const off = Math.round(voutSp - p);
    if (off >= 0 && off <= 100) { state.palier = p; state.offset = off; return; }
  }
}
```

VOSP reste la vérité : on ne change que **la façon de le décomposer** en
palier + décalage, jamais la valeur affichée ni celle envoyée.

---

## 3. Afficher `STATE`

`STATE` n'apparaît nulle part dans l'IHM. Or c'est lui qui dit si l'étage 2
est réellement engagé :

| `STATE` | Signification |
|---|---|
| 0 | IDLE |
| 1 | START_S1 — étage 1 monte |
| 2 | RUN_S1 — étage 1 établi, étage 2 encore à zéro |
| 3 | START_S2 — étage 2 monte |
| 4 | RUN — régime établi |

`0 → 1 → 4` = mode étage 1 seul. `0 → 1 → 2 → 3 → 4` = étage 2 engagé. C'est
la **seule** distinction observable, et les critères de recette du prompt
précédent reposent dessus.

À afficher en texte, à côté de `FAULT`, avec le libellé et le code. Ajouter le
champ à `/api/state` s'il n'y est pas.

Afficher `DUTY2` au même endroit serait un plus : c'est le second témoin que
l'étage 2 travaille.

---

## 4. Vérification, sans modification a priori

`send()` n'émet que `ht`, `run` et `voutset` — **pas `v1set`**. C'est
précisément le piège du §3.2 du prompt précédent : un tag absent laisse le
TMS320 sur sa dernière valeur, et son initialisation est **15 V**.

Les relevés montrent `V1SP = 50.0 V`, ce qui prouve que le firmware insère
bien `V1SET=50.0` côté serveur lors de la construction de la trame `$C`. C'est
correct.

**Ce qu'il faut faire** : vérifier dans `tms_link.cpp` que le tag est émis dans
**chaque** trame et non une seule fois, puis y laisser un commentaire
expliquant pourquoi il ne doit jamais disparaître. Une refonte ultérieure de la
page ne doit pas pouvoir supprimer ce comportement sans que personne ne le
remarque — la panne serait silencieuse : alimentation à 15 V au lieu de 50, et
`REJ` immobile.

---

## 5. Recette

1. Jauge `iout` : injecter ou simuler 100 mA → remplissage aux deux tiers, pas
   en butée.
2. Jauge `vout` en mode étage 1 seul : la sortie vaut ~V1, le remplissage doit
   être **non nul** et suivre V1.
3. Jauge `v1` avec `V1SET=50` : autour de 90 % — pas 100 %, et pas rouge en
   marche normale.
4. Presser **300V** à l'arrêt, attendre trois rafraîchissements : le bouton
   **300V** reste allumé, le décalage affiche **+0V**.
5. Presser **+10V** cinq fois en marche : `VOSP` monte à 350, le bouton 300V
   reste allumé, `REJ` ne bouge pas.
6. `STATE` visible et cohérent : `0 → 1 → 4` avec le palier OFF,
   `0 → 1 → 2 → 3 → 4` avec un palier non nul.
7. `V1SP` remonte **50.0** en continu, y compris après plusieurs minutes.

---

## 6. À lire

- `claudeesp32-etage2-interface.md` — le prompt d'origine, dont les règles
  restent en vigueur (pas de validation locale, `RUN=0` pour changer de
  palier, aucun acquittement de défaut) ;
- `CLAUDE-esp32.md` — règles de comportement, prime sur tout ;
- `ESP32-UART.md` — protocole, codes de défaut, états.
