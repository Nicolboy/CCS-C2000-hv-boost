#ifndef CALIB_H
#define CALIB_H

// Horloge : INTOSC1 (10 MHz nominal) + PLL (DSP28_PLLCR=12, DSP28_DIVSEL=2,
// deja les valeurs par defaut de f2802x_examples.h pour ce device) -> 60 MHz.
// PLLLOCKPRD doit etre ecrit AVANT InitSysCtrl() (qui attend le verrouillage
// PLL en interne) car l'oscillateur interne impose un minimum de 10000.
#define CLK_PLLLOCKPRD      10000
#define CLK_SYSCLKOUT_HZ    60000000UL

// LEDs (LQFP48 PT) : bleue = GPIO12 (broche 47), rouge = GPIO33 (broche 36)
// Point ouvert §9 du prompt : polarite a confirmer au premier test.
// Observation bring-up (carte 2) : GPIO33 haut = LED rouge visuellement allumee
// -> cablage actif-haut, pas actif-bas comme suppose initialement.
#define LED_ACTIVE_LOW      0

// Valeur de champ AIOMUX1 pour basculer une broche AIOx en mode analogique
#define GPIO_ANALOG_MODE    2

// =====================================================================
// Constantes de calibration des chaines de mesure.
// Source : docs/mesure-cartepuissance.md (mesures reelles sur la carte
// elevateur). Aucune de ces valeurs ne doit apparaitre ailleurs.
//
// ---------------------------------------------------------------------
// CARTE DE PUISSANCE V0.2 -- ETALONNAGE PARTIEL. LIRE AVANT D'EXPLOITER.
// ---------------------------------------------------------------------
// Tout l'etalonnage historique a ete fait sur la carte V0.1 : il est
// archive dans docs/calibration-V0.1.md et NE S'APPLIQUE PLUS tel quel.
// Etat voie par voie sur V0.2 :
//
//   V1   [V0.2] refait, deux points, confiance elevee
//   IIN  [V0.2] refait, INA293A2 monte, valeur theorique confirmee
//   VIN  [V0.1] ecart ~1 %, non separe gain/offset -- A REFAIRE
//   VOUT [V0.1] non verifiee sur V0.2
//   I1   [V0.1] NON VERIFIEE -- pilote le seuil de protection, cf. plus bas
//   I2   [V0.1] jamais mesuree, ni sur V0.1 ni sur V0.2
//   IOUT [V0.1] jamais mesuree
//   NTC  [V0.1] resistance fixe reelle jamais confirmee
//
// Methode imposee pour toute reprise : en CONTINU, PWM inhibe, sur la
// valeur BRUTE de l'ADC lue au debogueur -- jamais sur l'affichage du
// serveur, arrondi a une decimale, qui interdit de separer un gain d'un
// offset. Pieges detailles dans docs/calibration-V0.1.md section 4.
// =====================================================================

// ---- Chaines de tension (doc §1) ------------------------------------
// Gain = Vreel / Vadc, soit l'inverse du rapport du pont diviseur.
// VIN et V1 tombent sur les valeurs theoriques ; VOUT s'en ecarte de
// 1,8 % (tolerance des resistances), sans consequence : la pleine
// echelle reelle (600 V) reste au-dessus des 400 V vises.
// RECALES en charge, convertisseur en regulation a 35 V sur 220 ohms, avec
// deux points d'entree (9,94 V / 620 mA et 15,07 V / 416 mA) references au
// voltmetre et a l'amperemetre. Valables SEULEMENT depuis l'ajout des RC
// 1 kOhm + 10 nF sur les sorties des suiveurs : avant, la reinjection de
// charge de l'ADC faisait lire V1 jusqu'a 16 % trop haut, de facon bimodale.
// Tout etalonnage anterieur a ce filtre est a jeter.
// VIN recale sur le multimetre : 17,50 V lus pour 17,15 V reels, mesures au
// meme noeud (condensateur d'entree, la ou est pique le pont diviseur).
// 11,00 x 17,15/17,50 = 10,78. UN SEUL point : l'offset n'est pas separe du
// gain, a reprendre avec un second point a tension d'entree differente.
// Etalonnees sur la VALEUR BRUTE DE L'ADC, lue au debogueur, contre le
// voltmetre, a un point CONTINU sans decoupage (PWM inhibe, charge de 42
// ohms derriere la diode) : aucune ambiguite d'instant d'echantillonnage,
// et les noeuds sondes sont a basse impedance donc le voltmetre y est
// parfaitement valide.
//
//   VIN : brut 2742 -> 2,2091 V   voltmetre 24,10 V  ->  10,909
//   V1  : brut  944 -> 0,7605 V   voltmetre 23,44 V  ->  30,82
//
// Un gain pur suffit pour ces deux voies : pont diviseur suivi d'un
// suiveur TLV9151, dont l'offset se compte en microvolts. C'est l'inverse
// de IIN, ou le courant d'offset du ZXCT1109 impose un second point.
//
// Les 0,66 V d'ecart entre les deux mesures sont la chute directe de la
// diode FFSD2065 a 0,55 A -- coherent, et c'est ce qui a permis de voir
// que V1 lisait trop haut : l'ADC affichait la meme tension des deux
// cotes de la diode.
// [V0.2] V1 REFAITE. Le pont diviseur a change entre V0.1 et V0.2 : l'erreur
// est un GAIN PUR, sans offset, ce que deux points ecartes de 86 % etablissent
// sans ambiguite (le meme rapport a 0,01 % pres ne peut pas venir d'un offset).
//
//   43,68 V au multimetre / 46,0 V annonces  ->  30,82 x 43,68/46,0 = 29,266
//   23,55 V au multimetre / 24,8 V annonces  ->  30,82 x 23,55/24,8 = 29,266
//
// Le second point a ete pris en CONTINU, PWM inhibe : VIN 24,18 V et V1
// 23,55 V, soit 0,63 V d'ecart -- la chute directe de la FFSD2065. Un boost ne
// peut pas sortir moins que son entree, la conduction etait donc purement
// L -> diode -> charge, sans ambiguite d'instant d'echantillonnage.
//
// A CONFIRMER : ces deux points viennent de l'affichage du serveur, arrondi a
// une decimale. Le rapport est solide, la troisieme decimale ne l'est pas.
#define MEAS_V1_GAIN_V_PER_V     29.27f   // broche 16, [V0.2]

// [V0.1] NON VERIFIEES SUR V0.2.
// VIN : ecart d'environ 1 % constate sur V0.2 (19,93 V lus 19,7 ; 24,18 V lus
// 24,0). Les deux points donnent 11,036 et 10,991 -- ils ne se recoupent pas
// assez pour separer un gain d'un offset a la resolution de l'affichage. La
// valeur theorique du pont etant 11,00, il est probable que V0.2 soit revenue
// dessus, mais ce n'est PAS mesure. Reprendre sur la valeur brute de l'ADC.
#define MEAS_VIN_GAIN_V_PER_V    10.909f  // broche 10, coef mesure 0,09

// ---- VOUT : etalonnee le 20/08/2026, gain ET offset ------------------
//
// LA LOI N'EST PAS UN GAIN PUR. Une LED temoin "HT presente" est en SERIE
// dans le pont, entre la chaine haute et le point milieu :
//
//   +HT -- R32 330k -- R31 330k -- R30 330k -- LED1 --|>|-- tap -- R10 5,6k -- GND
//
//   V_HT = Vadc x MEAS_VOUT_GAIN_V_PER_V + MEAS_VOUT_OFFSET_V
//
// Deux points releves au multimetre en sortie de suiveur :
//   12,65 V -> 0,062 V     49,00 V -> 0,268 V
//   -> gain 176,5   offset 1,71 V
//
// Trois determinations INDEPENDANTES concordent a 1 % : le rapport calcule
// depuis la netlist (990k/5,6k = 177,8), l'ajustement sur les deux points
// ci-dessus, et la chute de LED mesuree directement (1,5 V a 12 V, 1,6 V a
// 49 V). D'ou 177,0 -- moyenne arrondie a la precision reelle, pas plus.
//
// L'ANCIENNE VALEUR 181,82 ETAIT UNE VALEUR DE CONCEPTION, jamais mesuree,
// et elle ignorait la LED : 2,7 % de gain en trop, plus 1,7 V manquants.
//
// POINT A 200 V, RELEVE LE 20/08/2026 -- premier regime etabli de l'etage 2,
// a vide : multimetre 200,4 V, firmware 200,0 V. Ecart 0,2 %.
//
// La loi ajustee sur 12 et 49 V tient donc a un facteur QUATRE au-dela de
// son dernier point, ce qui n'allait pas de soi : c'est une extrapolation
// qui a ete verifiee, pas supposee. Les 0,4 V representent moins de TROIS
// counts d'ADC (un count vaut 0,142 V de sortie a ce niveau) -- l'ecart est
// au niveau de la quantification, il n'y a rien a corriger et affiner
// deplacerait le seuil de survoltage sans rien gagner.
//
// Consequence directe : VOUT_OV_TRIP_RAW, donc la coupure a 520 V, s'appuie
// desormais sur une chaine verifiee en son milieu et non plus seulement en
// bas d'echelle. Rappel de ce qui a ete evite : avec l'ancienne constante de
// 181,82, cette meme "coupure a 520 V" agissait en realite vers 347 V.
//
// POINT A 500 V, LE MEME SOIR -- L'ETALONNAGE EST CLOS.
// Multimetre 500 V, firmware 500 a 502 V. La loi tient donc sur QUATRE
// points : 12,65 / 49 / 200 / 500 V, soit un rapport 40, avec au pire 0,4 %
// d'ecart. Aucune extrapolation ne subsiste.
//
// Consequence : VOUT_OV_TRIP_RAW, donc la coupure a 520 V, s'appuie
// desormais sur une chaine verifiee JUSQU'AU VOISINAGE IMMEDIAT du seuil.
// Elle ne reposait auparavant que sur un calcul -- et l'ancienne constante
// de 181,82 faisait agir cette meme "coupure a 520 V" vers 347 V.
//
// ATTENTION A LA MARGE D'EXPLOITATION : a la consigne maximale de 500 V, il
// ne reste que 20 V jusqu'au seuil, soit 4 %. Le depassement mesure sur
// l'etage 1 lors d'un delestage etait de 2 % apres correction du PID ; sur
// l'etage 2, dont le condensateur est cent fois plus petit, il n'a pas
// encore ete caracterise. Un delestage a pleine tension reste donc un essai
// A FAIRE, avec le scope arme.
//
// PIEGE DE MESURE, decouvert au banc le 20/08/2026 : le clamp D4 sur le
// noeud du pont REDRESSE le couplage de decoupage et fabrique jusqu'a
// +480 mV de continu -- soit 130 V d'erreur d'affichage. Ces deux points
// ont ete releves D4 DEPOSEE. Un RC 10k/1nF en amont reduit l'artefact sans
// le supprimer : les fronts reels sont plus rapides que ce qu'un scope
// 100 MHz montre, et un 1 nF ceramique cesse de filtrer avant.
//
// RESOLU LE 20/08/2026 : clamp 3,3 V pose SUR LA SORTIE DU SUIVEUR, ou le
// signal est basse impedance et propre. Le point a 200 V a ete confirme
// AVEC ce clamp en place -- il ne perturbe pas la mesure, contrairement a
// D4 sur le noeud du pont. Ne jamais revenir a un clamp sur le noeud, et
// ne jamais en retirer un sans le replacer : le pont limite le noeud a
// 2,92 V au seuil de 520 V, mais si R10 s'ouvre le tap part vers le rail
// HT a travers 990 kOhm.
#define MEAS_VOUT_GAIN_V_PER_V   177.0f   // broche 14, [V0.2] mesure
#define MEAS_VOUT_OFFSET_V       1.70f    // chute de LED1, en VOLTS DE SORTIE

// ---- Courant d'entree (doc §2) --------------------------------------
//
// AVERTISSEMENT : tout le bloc de commentaire qui suit, jusqu'au marqueur
// [V0.2], decrit la chaine ZXCT1109 de la carte V0.1. Il est conserve pour
// la tracabilite du raisonnement, mais la VALEUR retenue est celle du
// marqueur [V0.2] en fin de bloc. Archive : docs/calibration-V0.1.md.
// Recale sur les deux memes points : 487 counts -> 620 mA et 335 -> 416 mA.
// L'ancienne valeur de 1,2019 sous-estimait de 30 %, ce qui donnait des
// rendements superieurs a 100 % -- c'est par la qu'on a trouve le probleme.
//
// Gain SEUL, offset suppose nul, et c'est un choix delibere : les deux
// points impliqueraient un offset de 25 counts, soit 20 mV ramenes a la
// broche. Le meme calcul donne 30 mV sur VIN, sept fois l'offset maximal
// d'un MCP6001. Un tel ecart ne peut pas etre un offset d'amplificateur : le
// modele a deux points capte donc autre chose (non-linearite, ou simplement
// le bruit des references). Le figer reviendrait a graver une erreur de
// mesure. Ecart residuel avec le gain seul : moins de 2 %.
//
// Un TROISIEME point, a courant nettement different, trancherait.
// CALCULEE, plus etalonnee. Tous les termes sont connus et verifies
// independamment, il n'y a plus lieu d'ajuster empiriquement :
//
//   ZXCT1109 : Gt = 4 mA/V a +/-1,8 % (fiche DS35033, page 3)
//   Rsense   : 0,0208 ohm, mesure au banc (27 mV releves a 1,30 A)
//   Rgain    : 10 kOhm
//
//   Vadc = 0,004 x 0,0208 x I x 10000 = 0,832 V/A  ->  I = Vadc x 1,202
//
// L'ancienne valeur de 1,568 venait d'un ajustement a deux points dont le
// commentaire ci-dessus avouait deja qu'il "captait autre chose" : elle
// faisait lire 30 % TROP HAUT, ce qui correspond aux +27 % constates au
// multimetre (1,80 A affiches pour 1,42 A reels).
//
// Precision attendue : +/-1,8 % (Gt) +/-1 % (Rgain) + l'offset de sortie du
// ZXCT1109, 3 uA typiques soit 2,7 % a 1,4 A. Environ +/-6 % au total.
// Etalonnee SANS PASSER PAR LE SHUNT, a deux points continus (PWM inhibe).
//
// En continu le courant traverse L puis la diode jusqu'a la charge, donc
// I = V1 / Rcharge. V1 etant calibree a 0,1 % et la charge mesuree a
// 39,3 ohms, le courant vrai est connu sans aucun intermediaire :
//
//   24,0 V : brut 610 -> 0,4914 V   I = 23,44/39,3 = 0,5964 A -> 1,2137
//   15,2 V : brut 378 -> 0,3046 V   I = 14,54/39,3 = 0,3700 A -> 1,2149
//
// Deux points separes de 60 % en courant, et le meme rapport a 0,1 % : la
// droite passe par l'origine, l'offset est negligeable. Aucune hypothese
// n'est faite sur la valeur du shunt, la transconductance ou Rgain.
//
// A VIDE la voie lit encore 16 mA. Les integrer comme offset DEGRADE
// l'accord aux points de travail (1,8 % de dispersion au lieu de 0,1 %) :
// on prefere etre juste entre 0,4 et 3 A, la ou la mesure sert.
//
// PIEGES ECARTES EN CHEMIN, a ne pas refaire :
//  - le multimetre est perturbe par le decoupage a 200 kHz et lisait 56 %
//    de moins que l'ADC sur la sortie du ZXCT. Ce n'est PAS un effet de
//    charge : verifie a 24,07 V sous 10 kOhm contre 24,10 V sans, donc
//    10 MOhm d'impedance d'entree. Etalonner cette voie EN CONTINU.
//  - la valeur effective du shunt ressort a 19 mOhm et non aux 22 mOhm
//    nominaux. Tout etalonnage qui s'appuie dessus herite de l'ecart.
//  - la carte de controle etant sur alimentation separee, rien ne
//    contourne le shunt : l'amperemetre et le shunt voient bien le meme
//    courant.
// [V0.2] REFAITE. Tout le raisonnement ci-dessus porte sur le ZXCT1109 de la
// V0.1 : il est ARCHIVE dans docs/calibration-V0.1.md et ne s'applique plus.
// L'INA293A2 est monte sur V0.2, la chaine est entierement differente.
//
//   Vadc = 50 x 0,01 x I = 0,5 V/A   ->   I = Vadc x 2,000
//
// Confirme au banc, en continu et PWM inhibe, sans faire d'hypothese sur le
// shunt -- c'est la sortie de l'ampli qui est mesuree directement :
//
//   126 mV en sortie d'INA293A2 (gain 50) pour 0,27 A au multimetre
//     -> Vshunt = 126/50 = 2,52 mV  ->  Rsense = 9,3 mOhm
//   le shunt de 10 mOhm est donc bien celui monte, a 7 % pres (tolerance du
//   shunt + resolution du multimetre sur un courant aussi faible).
//
// Recoupement independant, cote ADC : le serveur affichait 0,16 A avec
// l'ancien gain de 1,214, soit Vadc = 132 mV -- a 5 % des 126 mV lus a
// l'ampli. L'ADC lisait donc deja correctement la broche ; seule la constante
// etait restee celle du ZXCT1109. Avec 2,000 : 0,264 A contre 0,27 A mesures.
//
// A CONFIRMER : un seul point, a courant faible (0,27 A). Un second point
// vers 1 a 2 A verrouillerait l'absence d'offset, que la fiche de l'INA293
// rend deja tres probable (~1,5 mA ramene au courant, contre ~37 mA pour le
// ZXCT1109 qu'il remplace).
// [V0.2] RECALE LE 17/08/2026 SUR MESURE, apres correction d'un defaut de
// masse (deux alimentations non reliees) qui invalidait tous les releves
// anterieurs -- y compris le point a 0,27 A qui semblait confirmer 2,000.
//
// La valeur de 2,000 etait THEORIQUE (50 x 0,010 ohm -> 0,5 V/A). Mesure au
// point de fonctionnement, elle fait lire 12 % trop haut :
//
//   offset : carte alimentee, RUN=0, CHARGE DEBRANCHEE (indispensable ici :
//            charge branchee le courant passe encore par L et la diode, donc
//            par le shunt d'entree, contrairement au shunt de I1 qui est
//            dans la source du MOSFET et ne voit rien a RUN=0)
//            -> 0,00 a 0,02 A, soit ~5 mV. Negligeable, retenu a zero.
//
//   gain   : 0,97 A au multimetre affiches 1,05 a 1,15 A (centre 1,10)
//            -> 2,000 x 0,97 / 1,09 = 1,78 A/V
//
// Soit 0,562 V/A au lieu de 0,5 theoriques : 11,2 mOhm effectifs pour un
// shunt de 10 mOhm. Le meme exces d'une dizaine de pour cent que sur I1, et
// probablement la meme cause -- du cuivre inclus dans la boucle de mesure.
//
// UN SEUL POINT, et la lecture y bat de +/-4,5 % pour une raison non
// elucidee (le RC 1k/10nF est pourtant en place). A confirmer par plusieurs
// points obtenus en faisant varier V1 sur la charge de 100 ohms : 30, 35, 40
// et 46 V donnent environ 0,41 / 0,55 / 0,73 / 0,97 A, de quoi ajuster une
// droite sans aucune charge supplementaire.
//
// Sans consequence pour la securite : IIN ne pilote aucune protection. Elle
// sert a la telemetrie et au repliement de puissance (LIM), pas encore
// implemente.
#define MEAS_IIN_A_PER_V         1.78f    // [V0.2] mesure, un seul point
#define MEAS_IIN_OFFSET_V        0.000f   // [V0.2] mesure : ~5 mV, negligeable

// RAPPEL MATERIEL : contrairement au ZXCT1109 qui se nourrissait de la ligne
// mesuree, l'INA293A2 exige une alimentation separee de 2,7 a 5,5 V.
// Le rail 3,3 V doit donc lui parvenir -- et etre protege (TVS 3,6 V).
//
// Pleine echelle 6,6 A, environ 6,4 A utiles (la sortie ne monte pas tout a
// fait au rail). Resolution 1,6 mA par LSB, soit l'ordre de grandeur de
// l'offset : inutile de chercher plus fin.

// ---- Courant de sortie (doc §4) -------------------------------------
// ZXCT1109 en version FLOTTANTE, protege cote HT par T13 (PNP FFMT560).
// Sortie en COURANT, convertie en tension par la resistance de charge :
//
//   gain = Rsense x GT x Rgain = 1 Ohm x 4,08 mA/V x 4700 = 19,18 V/A
//
// GT = 4,08 mA/V vient du datasheet DS35033 p.3 (table ZXCT1107/1109),
// pas d'une mesure. Redimensionnement du 26/08/2026, shunt 10 Ohm -> 1 Ohm
// et Rgain 1,5 k -> 4,7 k, pour porter la pleine echelle de 54 a 172 mA :
// le domaine vise est 150 mA sous 200 V et 100 mA sous 500 V.
//
// POURQUOI 1 OHM ET PAS 2 : la transconductance n'est GARANTIE que pour
// VSENSE de 10 a 150 mV (p.3). A 1 Ohm cette fenetre se transpose exactement
// en 10 a 150 mA, donc les deux points de fonctionnement y tombent (150 et
// 100 mV). A 2 Ohm on serait a 300 mV, soit le double de la borne haute --
// la courbe p.5 y reste droite, mais plus rien n'est garanti.
//
// ---- MESUREE le 20/08/2026, et LA CHAINE COMPRESSE ------------------
// Trois points, charge resistive de 473 Ohm mesuree a l'ohmmetre, courant
// verifie a l'amperemetre en serie :
//
//   I reel     serveur    Vadc      V/A     PWM
//   27,3 mA    30,0 mA    0,5753    21,07   inhibe
//   47,7 mA    50,0 mA    0,9588    20,10   inhibe
//   103,6 mA  100,0 mA    1,9176    18,51   en marche
//
// Le gain DECROIT de 12 % entre 27 et 104 mA, de facon monotone. Les deux
// premiers points sont PWM inhibe, donc hors de tout soupcon d'artefact :
// la tendance est etablie par eux seuls. Suspect principal : la compliance
// de sortie du ZXCT1109, dont la broche OUT sert aussi de substrat (note 1
// p.2) -- courbe OUTPUT CURRENT vs OUTPUT VOLTAGE p.5. Le rapport de
// transfert de T13, non quantifie dans le depot materiel, peut s'y ajouter.
//
// AUCUNE CONSTANTE UNIQUE NE PEUT SERVIR TOUTE LA PLAGE. Le choix retenu
// est de caler sur la ZONE D'USAGE REELLE, 10 a 20 mA, donc sur le point le
// plus bas :
//
//   MEAS_IOUT_A_PER_V = 1 / 21,07 = 0,0475
//
// CONSEQUENCE ASSUMEE : la voie SOUS-LIT d'environ 12 % vers 100 mA, et
// davantage a 150. Ne jamais s'en servir pour une courbe de rendement a
// pleine charge sans corriger. C'est ecrit ici parce que rien dans le code
// ne le laisse deviner.
//
// ARTEFACT DE DECOUPAGE, traite mais pas clos. IOUT est en fin de sequence
// ADC, echantillonne a une phase arbitraire du cycle : adc.c n'optimise
// l'instant que pour I1. Le commentaire de ce fichier affirmait que le
// courant y est continu -- c'etait vrai de la chaine IIN sur V0.1, pas de
// IOUT, dont le shunt est en aval de HV_EN. L'ajout du 10 nF oublie en
// sortie de suiveur a ramene l'erreur de -12,6 % a -3,5 % a 104 mA.
// Passer C14 de 100 pF a 10 nF (R9 = 10 k est deja en place en amont du
// suiveur) supprimerait le reste. SANS RISQUE sur cette voie : measure_iout()
// n'alimente que la telemetrie, aucun seuil ni regulation, donc aucune
// latence a preserver -- contrairement a VOUT.
#define MEAS_IOUT_A_PER_V        0.0475f

// Offset du zero : MESURE NUL le 20/08/2026, PWM inhibe et sortie ouverte.
// L'affichage descend au dixieme de mA et indique 0,0 -- donc moins de
// 0,05 mA de residu.
//
// C'est BIEN MEILLEUR que le pire cas redoute. Le datasheet donne un courant
// de sortie residuel de 3 uA typique et 10 uA maximum a VSENSE = 0 (p.3),
// soit jusqu'a 2,45 mA avec un shunt de 1 Ohm -- ce qui aurait fait 12 a
// 24 % d'erreur sur la zone d'usage 10-20 mA. C'etait la principale reserve
// contre le passage a 1 Ohm ; elle est levee.
//
// Consequence de methode : l'offset etant mesure nul, UN SEUL POINT suffit
// desormais a fixer le gain. La regle des deux points n'existait que pour
// separer gain et offset.
#define MEAS_IOUT_OFFSET_V       0.0f

// ---- Shunts MOSFET 0,02 ohm (doc §3) --------------------------------
// Amplis TLV9151 montes sur la carte (remplacent les MCP6001 de banc).
//
// OFFSETS REMESURES a courant nul, carte alimentee au repos : la tension
// lue EST l'offset, aucune injection n'est necessaire. Deux releves ont
// donne 4 puis 0 count sur I1, et 0 count sur I2 -- soit AU PLUS 3,2 mV,
// donc sous la resolution de l'ADC (0,8 mV/LSB). Contre 47,3 et 34,0 mV
// avec les MCP6001 : le Vos d'entree est passe sous 0,1 mV une fois
// ramene par le gain de 31,5.
//
// Retenir zero est le choix JUSTE et le choix SUR : c'est la meilleure
// estimation, et pour le seuil de protection ci-dessous un offset
// sous-estime abaisse le code DAC, donc fait declencher un peu plus tot.
//
// Limite a connaitre : l'ampli est en alimentation simple, sa sortie ne
// peut pas descendre sous 0 V. Un Vos negatif serait donc ecrete et
// indiscernable de zero -- et les tres faibles courants sont perdus dans
// ce plancher. Sans consequence pour la protection (seuil a 1,9 V), mais
// la mesure de courant n'est pas exploitable pres de zero.
//
// Le GAIN est fixe par le reseau RF/RG et n'a pas ete retouche : il reste
// donc la seule partie non reverifiee de cette chaine. Les deux voies
// different d'environ 1 %, garder deux jeux de constantes, ne jamais
// moyenner.
// GAIN DE I1 MESURE, enfin -- il n'etait jusqu'ici que calcule (0,02 ohm x
// 31,3), et faux de 29 %. La mesure n'etait pas possible avec les TLV9151 :
// a 144 kHz de bande passante en boucle fermee ils n'avaient pas fini de
// monter a l'instant d'echantillonnage. Le TSV791 (50 MHz, tau = 100 ns)
// restitue la rampe fidelement, et la voie devient etalonnable.
//
// METHODE, et c'est elle qui compte : en conduction continue, le courant
// d'inductance A MI-CONDUCTION vaut exactement le courant d'entree. Aucune
// hypothese sur L, sur le shunt ou sur le gain de l'ampli.
//
//   Iin (amperemetre)            2,58 A
//   V a mi-conduction (scope)    2,072 V
//   ligne de base hors conduction  -0,034 V
//   -> (2,072 + 0,034) / 2,58 =  0,816 V/A
//
// Contre-verification : le firmware annoncait 3,33 A, soit 2,10 V avec son
// ancienne constante -- exactement la tension lue au scope. Le firmware et
// l'oscilloscope voyaient donc la meme chose, seule la constante etait fausse.
//
// NE PAS utiliser la methode par la PENTE : dV/dt = gain x Vin/L confond le
// gain avec l'inductance reelle, connue a +/-20 % au mieux (tolerance, plus
// la derive en courant continu). Deux tentatives ont donne 0,62 puis 0,73.
//
// D'OU VIENNENT LES 29 % ? Pas elucide. 0,816/31,3 = 25,9 mOhm de resistance
// effective pour 20 nominaux. La ligne de base negative signale bien du
// cuivre partage entre l'extremite froide du shunt et le chemin de retour,
// mais 34 mV n'en representent que 0,4 mOhm -- loin des 6 manquants. Reste
// donc a verifier a l'ohmmetre la valeur reelle du shunt et du reseau Rf/Rg.
// Tant que ce n'est pas fait, MEAS_I2_GAIN_V_PER_A est suspecte du meme
// ecart : meme schema, memes references.
//
// PROVISOIRE : un seul point de fonctionnement. VIN s'est ecartee de 3 % au
// meme releve alors qu'elle etait juste a 0,2 % au precedent -- le repliement
// residuel redistribue les erreurs entre voies quand le rapport cyclique
// change. A confirmer a une autre consigne.
// [V0.2] LES DEUX ETAGES N'ONT PLUS LE MEME SHUNT. C'est le point a retenir :
// I1 est passe a 0,01 ohm, I2 est reste a 0,02 ohm. Toute formule, tout seuil
// et tout raisonnement qui les traitait comme identiques est a revoir.
//
// I1 [V0.2] MESURE, directement en sortie d'ampli, sans hypothese sur le
// shunt ni sur Rf/Rg :
//
//   10 A -> 3,40 V   ->   0,34 V/A   (soit 0,010 ohm x G_ampli 34)
//
// Pleine echelle : 3,3 / 0,34 = 9,7 A. Le domaine d'exploitation vise est
// 3 A moyen / 6 A crete, largement dedans. C'est la raison d'etre du
// changement : l'ancienne chaine plafonnait a 4,04 A, ce qui interdisait la
// pleine puissance en dessous de 19 V d'entree (cf. hardware.md).
//
// I2 [V0.1] TOUJOURS JAMAIS MESURE. Le shunt est reste a 0,02 ohm. SI le
// reseau Rf/Rg est le meme que sur I1 (G = 34), le gain vaudrait 0,68 V/A --
// c'est une DEDUCTION, pas une mesure, et la valeur ci-dessous est celle,
// non mesuree elle aussi, qui datait de V0.1. Elle est conservee parce
// qu'elle est PLUS BASSE que la deduction : le code DAC qui en decoule fait
// declencher plus tot, ce qui est le sens sur de l'erreur.
// A mesurer comme I1 avant de peupler l'etage 2.
// [V0.2] GAIN MESURE LE 17/08/2026, PAR DEUX ROUTES INDEPENDANTES.
//
// Point de mesure : Vin 24,1 V, V1 45,42 V, I_in 0,97 A (multimetre), charge
// 100 ohms, L = 47 uH, F = 200,91 kHz. Sortie d'ampli au scope, voie C1.
//
//  A) par la moyenne. Le shunt etant dans la SOURCE du MOSFET, il ne voit le
//     courant que pendant la conduction, et le rapport de ce courant moyen au
//     courant d'entree ne depend QUE des tensions -- ni de L, ni du rapport
//     cyclique, ni du mode de conduction (en CCM parce que D = (V1-Vin)/V1,
//     en DCM parce que l'equilibre des volt-secondes donne le meme rapport
//     t_on/(t_on+t_fall)) :
//         gain = moyenne x V1 / (I_in x (V1 - Vin))
//     La moyenne se tire des deux valeurs affichees par l'instrument,
//     sans lecture graphique :  moyenne = racine(DC_RMS^2 - AC_RMS^2)
//         racine(444,38^2 - 358,47^2) = 262,6 mV
//         0,2626 x 45,42 / (0,97 x 21,32) = 0,577 V/A
//
//  B) par l'amplitude de la rampe, qui n'utilise NI I_in NI le rendement :
//         dI = Vin x t_on / L = 24,1 x 2,336 us / 47 uH = 1,198 A
//         gain = dV / dI = 0,68 / 1,198 = 0,568 V/A
//
// Deux chemins sans grandeur commune, 1,6 % d'ecart. Retenu : 0,57 V/A.
//
// CE QUI A FAIT ERRER AVANT (a ne pas refaire) : la valeur precedente de
// 0,34 V/A venait d'une injection a 10 A lue 3,40 V. Or la pleine echelle de
// la chaine est de 3,3/0,57 = 5,8 A : a 10 A l'amplificateur etait en butee,
// et 3,40 V etait le rail. Diviser une tension de saturation par le courant
// qui l'a provoquee ne donne pas un gain. Toute reprise doit se faire AU
// POINT DE FONCTIONNEMENT, jamais par injection hors gamme.
//
// RESTE OUVERT : 0,57 / 34 = 16,8 mOhm alors que le shunt est un 10 mOhm a
// 1 %. Le gain bout-en-bout est ce qui compte ici et il est mesure deux fois,
// mais ce facteur 1,7 dans la decomposition signifie qu'un element du chemin
// de mesure n'est pas ce que dit le schema. A elucider.
//
// OFFSET MESURE le 17/08/2026, carte alimentee et RUN=0, donc aucun courant
// dans le shunt. Deux releves : 0,06 A charge branchee, 0,05 A charge
// debranchee -- soit 0,055 x 0,57 = 31 mV ramenes a la sortie de l'ampli.
//
// Que les deux coincident est attendu et rassurant : a RUN=0 le MOSFET est
// bloque, le shunt de SOURCE ne voit donc aucun courant dans les deux cas.
// C'est bien un offset de chaine, constant, et non un effet du point de
// fonctionnement. L'ecart de 0,01 A est la resolution de l'affichage.
//
// C'est, au millivolt pres, la "ligne de base negative de -34 mV" deja
// relevee sur V0.1 : le meme defaut a survecu a la revision de carte. Il
// merite d'etre compris plutot que compense indefiniment -- l'hypothese
// avancee en V0.1 etait du cuivre partage entre l'extremite froide du shunt
// et le chemin de retour.
//
// HYPOTHESE A CONFIRMER : la conversion suppose que le firmware tournait
// deja avec MEAS_I1_GAIN_V_PER_A = 0,57. S'il portait encore 0,34, l'offset
// vaut 19 mV et non 31. L'ecart est sans consequence pratique (12 mV devant
// les 262 mV du signal et les 2,28 V du seuil), mais la valeur est a
// reprendre si la mesure est refaite.
//
// Effet sur la protection : SAFETY_DAC_CODE_STAGE1 ajoute cet offset au
// seuil, qui represente donc bien 4,0 A AU-DESSUS de la ligne de base, et
// non 4,0 A comptes depuis zero volt. C'est le comportement voulu.
// AFFINE LE 18/08/2026 apres ajout d'un RC d'entree 1 kOhm / 100 pF sur
// l'ampli (tau = 100 ns) : la bosse d'etablissement de ~1 us a disparu, la
// rampe est lineaire des la sortie du front, et l'etalonnage est enfin fait
// sur un signal propre. Le RC n'a pas change le gain -- il a rendu sa mesure
// possible. Point : Vin 24,1 V, V1 45,42 V, I_in 1,00 A, charge 100 ohms.
//
//   Route A (moyenne)  : (0,3045 - 0,031) x 45,42 / (1,00 x 21,32) = 0,583
//   Route B (curseur a mi-conduction, ou i_L vaut EXACTEMENT I_in, sans
//            aucune hypothese sur L ni sur le mode de conduction) :
//                        (0,613 - 0,031) / 1,00 = 0,582
//
// Validation croisee de la mesure elle-meme : le rapport cyclique lu sur la
// grille (C2 PosDuty = 46,62 %) tombe a 0,3 % de (V1-Vin)/V1 = 46,94 %.
#define MEAS_I1_OFFSET_V         0.031f  // [V0.2] mesure a RUN=0
#define MEAS_I1_GAIN_V_PER_A     0.58f   // [V0.2] mesure, deux routes
#define MEAS_I2_OFFSET_V         0.0f
#define MEAS_I2_GAIN_V_PER_A     0.637f  // [V0.2] verifie le 20/08, cf. ci-dessous

// =====================================================================
// LES DEUX VOIES DE COURANT : ARTEFACT INDUCTIF ET FILTRAGE
// Soiree du 20/08/2026, premiere mise sous tension de l'etage 2.
// A lire avant de toucher a un seuil, a un gain ou a un condensateur.
// =====================================================================
//
// ---- 1. LE GAIN DE I2 EST ENFIN VERIFIE ------------------------------
//
// Il portait "[V0.1] JAMAIS mesure" depuis l'origine. Trois routes
// independantes concordent maintenant a 7 % :
//   - mesure directe : 100 mV aux bornes du shunt -> 3,4 V en sortie -> 34
//   - schema : montage NON INVERSEUR, 1 + R_f/R_g = 1 + 3300/100    -> 34
//   - la constante elle-meme : 0,637 / 0,020                        -> 31,9
// Le seuil de 4 A vaut donc bien environ 4 A. Ce n'etait pas lui le
// probleme des declenchements de cette soiree.
//
// ---- 2. CE QUI DECLENCHAIT : L.di/dt, PAS DU COURANT -----------------
//
// Les deux voies presentaient des pointes de ~100 mV aux bornes du shunt,
// soit 3,4 V en sortie d'ampli -- au-dessus des seuils (2,351 V pour
// l'etage 1, 2,548 V pour l'etage 2). Le comparateur coupait donc a juste
// titre sur ce qu'il mesurait. Mais ces 100 mV n'etaient pas du courant :
//
//   - forme : etroites, BIPOLAIRES, calees sur les fronts de commutation.
//     Une chute resistive suivrait la rampe du courant ;
//   - amplitude : V = L.di/dt = 2 nH x 50 A/us = 100 mV, soit l'inductance
//     propre d'un shunt CMS et de ses acces ;
//   - IMPOSSIBILITE ARITHMETIQUE : avec L2 = 440 uH (2 x Coilcraft
//     MSS1583-224 en serie, valeur confirmee) et 0,5 us de conduction sous
//     50 V, le courant d'inductance ne peut pas depasser
//     dI = V.t/L = 57 mA. Les 3,4 V correspondaient a 5,3 A, soit 93 fois
//     le maximum physiquement atteignable.
//
// Fausses pistes ecartees en chemin, pour qu'on ne les reprenne pas :
// reamorcage de grille, sonnerie DCM (la frequence collait, l'amplitude
// non : Z0 = sqrt(L/C) ~ 1200 ohms ne donne que 125 mA), ampli oscillant
// (il amplifiait fidelement : 100 mV x 34 = 3,4 V, au poil).
//
// ---- 3. LE CORRECTIF POSE : 1 nF SUR LA CONTRE-REACTION --------------
//
// 1 nF en parallele sur R_f = 3,3 kOhm, SUR LES DEUX VOIES.
//   pole = 1/(2.pi.3300.1e-9) = 48 kHz,  tau = 3,3 us
//
// POURQUOI SUR LA CONTRE-REACTION ET PAS ENTRE LES ENTREES. Un essai a
// 1 nF entre les entrees a fait osciller l'ampli et declencher aussitot,
// A VIDE. La source est le shunt, 20 mOhm, donc une entree + tenue de
// facon tres raide : vu du noeud inverseur, ce condensateur est
// electriquement un condensateur vers la MASSE ALTERNATIVE. Il vient en
// parallele sur R_g et affaiblit la contre-reaction en haute frequence --
// pole a 1,6 MHz avec 1 nF, en pleine bande de l'AOP : bosse de gain,
// marge de phase perdue. A 100 pF le pole est a 16 MHz, donc inoffensif :
// c'est pour ca que la valeur d'origine tenait.
// Un condensateur de CONTRE-REACTION, lui, est stabilisant par
// construction. Et sur un montage non inverseur le gain ne tombe pas a
// zero mais a UN, ce qui suffit : la pointe ressort a ~1 V au lieu de
// 3,4 V.
//
// ---- 4. LE PRIX PAYE, QU'IL FAUT CONNAITRE ---------------------------
//
// 48 kHz est EN DESSOUS des frequences de decoupage (100 et 200 kHz).
// Gain effectif : 34 en continu, ~12 a 100 kHz, ~2,6 a 1 MHz.
//
//   POUR LA TELEMETRIE, c'est un progres : la voie donne le courant MOYEN
//   et ne depend plus de l'instant d'echantillonnage -- le defaut qui a
//   fait errer l'etalonnage de IOUT.
//
//   POUR LA PROTECTION, c'est un ARBITRAGE : le comparateur coupe
//   desormais sur la MOYENNE et non sur la CRETE. La contrainte de crete
//   du MOSFET n'est plus surveillee directement.
//
// CE QUI L'AUTORISE, et c'est un calcul, pas une impression :
//   di/dt max = Vin/L = 50 V / 440 uH = 114 mA/us
//   -> pendant les 3,3 us du filtre, le courant ne peut bouger que de
//      380 mA, soit 10 % d'un seuil a 4 A.
// Une inductance de 440 uH interdit les variations rapides : filtrer a
// 3,3 us ne coute presque rien en protection reelle. SI L'INDUCTANCE
// CHANGE, CE RAISONNEMENT EST A REFAIRE.
//
// ---- 5. CE QUI N'EST PAS CORRIGE -------------------------------------
//
// On a empeche l'ampli de transmettre l'artefact au comparateur. On ne
// l'a pas supprime : le shunt montre toujours ses pointes et son offset.
//
// L'OFFSET NEGATIF reste, lui, non explique : -25 a -30 mV sur l'etage 2,
// jumeaux des -34 mV documentes sur l'etage 1 depuis V0.1. Le meme ecart
// sur les DEUX etages n'est pas un hasard, mais l'hypothese du "cuivre
// partage" n'a PAS ete confirmee -- voir ci-dessous.
//
// ---- 6. CE QUI A ETE ELIMINE PAR L'ESSAI, ET C'EST LE PLUS UTILE -----
//
// Deux modifications successives, le meme soir, ont tranche entre les
// hypotheses restantes :
//
//   a) PAIRE KELVIN TORSADEE, des pastilles du shunt aux entrees de
//      l'ampli. AUCUN EFFET : crete inchangee, 1,50 -> 1,55 V. Cela elimine
//      d'un coup le cuivre partage SUR LE CHEMIN DE MESURE et la surface de
//      boucle -- les deux seuls mecanismes qu'une torsade corrige. La note
//      qui attribuait l'artefact au defaut de prise Kelvin etait donc
//      FAUSSE, et elle a ete corrigee ici.
//
//   b) 10 nF SUR LA BROCHE, en aval des 100 ohms de sortie d'ampli
//      (tau = 1 us). CRETE DIVISEE PAR 2,5 : 1,5 -> 0,6 V.
//
// Le raisonnement decisif est celui-ci : l'ampli est limite a tau = 3,3 us
// par son condensateur de contre-reaction. Il lui est PHYSIQUEMENT
// IMPOSSIBLE de restituer en sortie une impulsion de 150 ns qui serait
// arrivee par ses entrees. Puisqu'elle y est quand meme, elle est injectee
// AU NIVEAU DE LA SORTIE OU APRES -- piste, broche, ou alimentation de
// l'AOP. D'ou l'inefficacite de (a) et l'efficacite de (b).
//
// ---- 7. LE DECLENCHEMENT EST STOCHASTIQUE ---------------------------
//
// Releve sur 4 secondes a 500 ms/div : l'enveloppe du signal est
// STATIONNAIRE -- ni derive, ni oscillation croissante, ni escalier. Le
// convertisseur ne s'emballe pas.
//
// Mais ses cretes chevauchent le seuil en permanence. Ce qui empeche une
// coupure immediate est SAFETY_COMP_QUALSEL = 31, qui exige ~0,5 us de
// depassement CONTINU : la quasi-totalite des pointes sont plus breves et
// sont rejetees. De loin en loin, l'une est assez large et passe.
//
// D'ou le delai aleatoire observe -- 1 s, 4 s, 5-6 s -- qui est une loi de
// probabilite et non un mecanisme. Et d'ou le fait que chaque filtrage
// supplementaire RALLONGE le delai sans jamais regler quoi que ce soit : on
// deplace la distribution, sa queue atteint toujours le seuil. Une
// protection qui declenche au bout de dix minutes au lieu de cinq secondes
// n'est pas plus juste, elle est plus patiente.
//
// NE PAS POURSUIVRE DANS CETTE VOIE. Les deux reponses reelles sont :
//   - le BLANKING : masquer le comparateur pendant la fenetre de
//     commutation (sous-module Digital Compare, DCFCTL). A VERIFIER DANS LE
//     TRM : TZSEL.DCAEVT1 prend aujourd'hui l'evenement NON filtre, et rien
//     ne dit que la fenetre puisse porter sur celui-la sur F2802x ;
//   - reduire le parasite a sa source, ce qui est un sujet de routage.
//
// A NOTER : apres le 10 nF, la montee a 500 V a donne des cretes a 0,55 V
// pour un seuil a 2,351 V, soit une marge de 4. La loi d'echelle "artefact
// proportionnel a Vout", tiree des points a 200 et 300 V, ne vaut donc que
// pour la chaine d'AVANT ce condensateur.

// ---- NTC B57451V5103J062 (PROMPT §5) --------------------------------
// Montage : 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
// La NTC etant du cote 3,3 V, la tension MONTE avec la temperature :
//     R_ntc = R_fixe * (VREF - Vadc) / Vadc
// (le PROMPT §5 donne cette relation inversee ; verifie sur ses propres
// points de repere : 0,75 V a 0 C -> 34,2 kOhm, ce que beta=4000 predit,
// alors que la forme inversee donnerait 2,94 kOhm, donc du chaud.)
//
// SENS VALIDE sur les vraies thermistances montees : rechauffer une NTC a
// la main fait MONTER la valeur affichee. Le point restait indecidable
// avec les 10 k fixes de test, qui donnent 25 C que la formule soit droite
// ou inversee. C'etait un vrai trou de securite : inversee, une surchauffe
// se serait lue comme un refroidissement et la coupure a 80 C n'aurait
// jamais declenche.
// Releve de coherence a l'ambiante : 2219 et 2208 counts -> 28,8 et
// 28,5 C sur une carte alimentee, thermistances a 5 mm des MOSFET.
//
// R_fixe suppose a 10 kOhm : reste a confirmer sur la carte (doc §6).
#define MEAS_NTC_R_FIXED_OHM     10000.0f
#define MEAS_NTC_R25_OHM         10000.0f
#define MEAS_NTC_BETA_K          4000.0f
#define MEAS_NTC_T25_K           298.15f
#define MEAS_KELVIN_OFFSET       273.15f

// ---- Seuil de protection rapide (doc §5) ----------------------------
// Le seuil est defini en AMPERES ; le code DAC est calcule par macro a
// partir du gain et de l'offset mesures, jamais ecrit en dur. Ainsi le
// futur passage au TLV9151 ne touchera que les constantes ci-dessus.
//
// Formule TI : V = DACVAL * (VDDA - VSSA) / 1023  -> 1023, pas 4096.
// Le declenchement a ete valide au banc a 1,5 A (courant injectable), puis le
// seuil remis a sa valeur de service.
//
// PORTE DE 3,0 A 4,5 A. Le comparateur surveille le courant INSTANTANE dans
// le shunt, donc le SOMMET de la rampe d'inductance, pas sa moyenne :
//
//   I_crete = I_moyen + Vin x D / (2 x L x f)
//
// A 50 W sous 22,4 V le crete atteint deja 3,01 A -- le seuil de 3,0 A etait
// donc SOUS le courant de fonctionnement nominal. Ce n'etait plus une
// protection mais une limite d'exploitation, et elle se manifestait par des
// declenchements a 42 ohms de charge.
//
//   Vin      I_moyen   dI c-a-c   I_crete
//   22,4 V    2,40 A    1,22 A     3,01 A
//   15,0 V    3,50 A    1,08 A     4,04 A
//   13,5 V    3,90 A    1,02 A     4,41 A   <- limite du seuil retenu
//   10,0 V    5,26 A    0,83 A     5,68 A   <- HORS PORTEE, voir ci-dessous
//
// PLAFOND DE LA CHAINE DE MESURE, et c'est lui qui tranche. Il s'est
// effondre quand le gain reel a ete mesure :
//
//   suppose : 0,02 ohm x 31,3 = 0,626 V/A  ->  pleine echelle 5,3 A
//   MESURE  :                    0,816 V/A  ->  pleine echelle 4,04 A
//
// Le seuil ne peut donc PAS depasser ~3,9 A, saturation de l'ampli comprise.
// Retenu : 3,5 A, ce qui reproduit exactement le point de declenchement
// valide au banc a 50 W -- l'ancien reglage de "4,5 A" avec l'ancien gain
// produisait un code DAC de 2,84 V, soit 3,48 A reels. Rien ne change sur la
// carte, le chiffre est simplement devenu honnete.
//
//   Vin      I_moyen   dI c-a-c   I_crete    marge sous 3,5 A
//   22,4 V    2,40 A    1,22 A     3,01 A         16 %
//   15,0 V    3,50 A    1,08 A     4,04 A     DECLENCHE
//
// PLEINE PUISSANCE SEULEMENT AU-DESSUS DE ~19 V D'ENTREE. En dessous, le
// courant crete d'un fonctionnement a 50 W depasse ce que la chaine sait
// restituer : aucun seuil ne peut y etre place, la mesure sature avant.
//
// [V0.2] LE TABLEAU CI-DESSUS EST PERIME. Le shunt de l'etage 1 est passe a
// 0,01 ohm et la chaine a 0,34 V/A : la pleine echelle monte de 4,04 A a
// 9,7 A, et la limite "pleine puissance seulement au-dessus de 19 V" tombe.
// A 10 V d'entree, 50 W donnent 5,68 A crete -- desormais mesurables, avec
// un seuil placable au-dessus. Le repliement de puissance (tag LIM) n'est
// donc plus necessaire POUR CETTE RAISON.
//
// SEUIL RELEVE DE 3,5 A A 7,0 A. Ce n'est pas un confort, c'est une
// obligation : le domaine d'exploitation vise est 3 A moyen / 6 A CRETE, et
// le comparateur surveille le courant INSTANTANE. Laisser 3,5 A avec le
// nouveau gain ferait declencher la protection en fonctionnement normal.
//
//   etage 1 : 7,0 x 0,34  = 2,38 V  ->  code DAC 738   (< 1023, pas d'ecretage)
//
// A CONFIRMER, et ce n'est pas une question de mesure : 7,0 A crete doit
// rester sous la SOA du NTD100N70GN1 ET sous le courant de saturation de
// l'inductance -- valeur qui n'est toujours pas tranchee (47 ou 100 uH).
// C'est la seule constante de ce fichier qui se choisisse a partir des
// fiches des composants et non d'une mesure au banc.
//
// PIEGE PROPRE AUX DEUX SHUNTS DIFFERENTS : cette constante est UNIQUE mais
// ne represente plus le meme courant sur les deux etages, puisque chaque
// SAFETY_DAC_CODE_STAGEx applique le gain de SON etage. Avec un I2 deduit a
// 0,68 V/A, 7,0 A demanderait 4,76 V : au-dela de la reference du DAC, donc
// ECRETE A 1023 par SAFETY_DAC_CODE_FROM_V -- l'etage 2 declencherait en
// realite vers 4,9 A, silencieusement. Sans consequence tant que l'etage 2
// n'est pas peuple ; a traiter en scindant la constante en deux (une par
// etage) le jour ou il le sera.
// ABAISSE DE 7,0 A 4,0 LE 17/08/2026, ET CE N'ETAIT PAS UN REGLAGE DE
// CONFORT : AVEC LE GAIN MESURE, 7,0 A DESACTIVAIT LA PROTECTION.
//
//   7,0 A x 0,57 V/A = 3,99 V  >  SAFETY_DAC_VREF_V (3,3 V)
//
// SAFETY_DAC_CODE_FROM_V ecrete alors a 1023, soit un seuil place a 3,3 V --
// que l'amplificateur, qui sature vers 3,2 V, N'ATTEINT JAMAIS. Le
// comparateur ne pouvait donc plus basculer, sur aucun courant. C'est le
// piege decrit ci-dessus pour l'etage 2, qui s'appliquait en fait a l'etage 1
// sans que rien ne le signale : ni defaut, ni LED, ni trace en telemetrie.
// Une protection muette ressemble en tout point a une protection qui n'a pas
// eu a se declencher.
//
// Le 7,0 lui-meme decoulait du gain errone de 0,34 V/A (injection a 10 A en
// pleine saturation, cf. MEAS_I1_GAIN_V_PER_A) : il avait ete choisi pour
// couvrir une pleine echelle supposee de 9,7 A, qui n'a jamais existe.
//
// CHOIX DE 4,0 A, avec le gain reel :
//   seuil au DAC   4,0 x 0,57 = 2,28 V  ->  code 707, sous 1023, pas d'ecretage
//   pleine echelle 3,3 / 0,57 = 5,8 A   ->  le seuil est atteignable
//   a 50 W, le courant crete atteint ~2,9 A  ->  38 % de marge
//
// A revoir si la puissance de travail augmente : le plafond utile est
// d'environ 5 A, au-dela l'ampli sature avant le comparateur.
#define SAFETY_ISHUNT_THRESHOLD_A   4.0f    // [V0.2] crete, cf. ci-dessus
#define SAFETY_DAC_VREF_V           3.3f

// Qualification du comparateur : nombre d'echantillons SYSCLK consecutifs
// au-dessus du seuil avant que la sortie ne bascule. 31 -> environ 530 ns.
//
// N'est PAS un confort : sans elle, la protection se declenchait sur des
// pointes de commutation invisibles a l'oscilloscope. Mesure a l'appui --
// en abaissant DACVAL par paliers, convertisseur a 35 V, le basculement
// survient entre 0,81 et 0,97 V alors que le sommet de la rampe de courant
// ne depasse pas 0,50 V. Le comparateur, qui repond en 30 ns, voit donc un
// depassement de 0,35 V que 7,7 MHz d'echantillonnage ne resolvent pas. A
// 45 V de sortie ce depassement atteint 1 V et franchit le seuil de service,
// ce qui rendait toute montee en tension impossible.
//
// COUT : la detection d'une vraie surintensite est retardee d'autant. Sur un
// defaut franc, di/dt = Vin/L = 10 V / 47 uH = 0,21 A/us, donc 530 ns
// represente 0,11 A de depassement sur un seuil de 3 A. Negligeable.
//
// Exige SYNCSEL = 1 : la qualification ne porte que sur la sortie
// SYNCHRONISEE du comparateur. En asynchrone, ce champ est sans effet.
#define SAFETY_COMP_QUALSEL         31U

// ECRETAGE OBLIGATOIRE. DACVAL est un champ de 10 bits : un code superieur a
// 1023 y est TRONQUE, pas sature. Un seuil de 3,67 V donnerait 1138, tronque
// a 114, soit un declenchement des 0,45 A -- l'inverse de l'effet voulu.
// C'est arrive en relevant MEAS_I1_GAIN_V_PER_A de 0,631 a 0,816.
#define SAFETY_DAC_CODE_FROM_V(v_)                                        \
    ((uint16_t)(((v_) >= SAFETY_DAC_VREF_V)                               \
                    ? 1023.0f                                             \
                    : ((v_) / SAFETY_DAC_VREF_V * 1023.0f + 0.5f)))

// Seuil ramene a la sortie de l'ampli : Vadc = offset + I * gain.
#define SAFETY_DAC_CODE_STAGE1                                            \
    SAFETY_DAC_CODE_FROM_V(MEAS_I1_OFFSET_V                               \
                           + SAFETY_ISHUNT_THRESHOLD_A * MEAS_I1_GAIN_V_PER_A)
#define SAFETY_DAC_CODE_STAGE2                                            \
    SAFETY_DAC_CODE_FROM_V(MEAS_I2_OFFSET_V                               \
                           + SAFETY_ISHUNT_THRESHOLD_A * MEAS_I2_GAIN_V_PER_A)

// ---- Fenetre d'aveuglement du comparateur (blanking) ----------------
//
// ETAGE 1 UNIQUEMENT, et ce n'est pas un choix arbitraire : la fenetre ne
// peut etre ancree que sur CTR = 0 ou CTR = PRD (PULSESEL n'offre rien
// d'autre). Depuis l'inversion de l'Action Qualifier, le BLOCAGE de
// l'etage 1 -- le front qui rayonne -- tombe exactement sur CTR = 0. La
// fenetre le couvre donc avec un decalage CONSTANT. L'etage 2, reste en
// convention d'origine, a son blocage sur CMPA, mobile : il exigerait de
// piloter DCFOFFSET depuis l'ISR a chaque pas. C'est precisement ce que
// l'inversion evite, et la raison pour laquelle elle precedait le blanking.
//
// LE PRIX, A NE PAS MINIMISER. Pendant la fenetre la protection de
// surintensite de l'etage 1 est AVEUGLE, et c'est l'instant exact ou un
// court-circuit franc se manifesterait. Cet aveuglement s'EMPILE sur
// SAFETY_COMP_QUALSEL, qui exige deja ~0,5 us de depassement continu. Ce
// n'est pas un reglage de confort, c'est un compromis de securite assume
// pour depasser 430 V.
//
// DIMENSIONNEMENT. TBCLK = 60 MHz, donc 16,67 ns par count. La pointe
// parasite relevee au scope dure ~150 ns. 30 counts = 500 ns couvrent la
// pointe et sa queue sans plus. A CONFIRMER AU SCOPE : elargir seulement
// si la coupure persiste, jamais "par precaution".
//
// DCFOFFSET = 0 : la fenetre demarre a l'instant meme du blocage. La
// sonnerie SUIT le front, il n'y a rien a couvrir avant.
// REMIS A 0 LE 23/08/2026. Essaye a 440 V : sans effet, et on sait
// maintenant pourquoi. L'evenement qui declenche n'est PAS une pointe de
// commutation mais une salve d'emballement du courant de 60 a 130 us --
// la fenetre en couvre 0,5. On aveuglait la protection pendant un
// deux-centieme de la perturbation.
//
// A NE REACTIVER QUE si une pointe de commutation est un jour identifiee
// au scope comme franchissant le seuil. Ce n'est pas le cas aujourd'hui.
#define SAFETY_BLANK_STAGE1          0
#define SAFETY_BLANK_OFFSET_COUNTS   0U
#define SAFETY_BLANK_WINDOW_COUNTS  30U
// Le garde-fou de coherence avec PWM_AQ_TAIL_STAGE1 est plus bas dans ce
// fichier : ce symbole n'est pas encore defini ici.

// ---- Protection thermique (logicielle, PROMPT §6 etape 4) -----------
// Contrairement a la surintensite, la thermique est lente : le logiciel
// suffit, aucun chemin materiel n'est requis.
// PROVISOIRE : seuil a confirmer selon la tenue reelle du MOSFET et
// l'implantation des NTC sur la carte.
// L'hysteresis evite que l'etat oscille autour du point de bascule.
#define SAFETY_OVERTEMP_C        80.0f
#define SAFETY_OVERTEMP_HYST_C   10.0f

// ---- ANTI-REBOND DES VOIES NTC (23/08/2026) -------------------------
// Les voies T1/T2 ne sont PAS tamponnees (voir adc.h) : elles attaquent
// l'entree de l'ADC directement depuis le point milieu du pont, sans
// suiveur ni filtre actif. Un SEUL echantillon aberrant suffisait a
// verrouiller la carte -- defaut code 5 constate le 22/08 avec une
// temperature reelle stabilisee a 44 degres pour un seuil a 80, puis de
// nouveau le 23/08 en montant vers 410 V, T2 relevee a 40 degres.
//
// Ce n'est pas thermique et ca ne peut pas l'etre : aucune NTC 0805 sur un
// pont 10k/10k ne franchit 40 degres entre deux echantillons. C'est le
// bruit de commutation, qui croit avec la tension de sortie -- d'ou
// l'apparition du defaut a la montee en tension et pas avant.
//
// Meme principe que CTRL_VIN_UV_COUNTS : N mesures CONSECUTIVES au-dessus
// du seuil avant de verrouiller. La thermique se compte en secondes, donc
// la temporisation est GRATUITE -- contrairement a une surintensite, rien
// ne se degrade pendant 50 ms.
//
// DECIMATION, ET POURQUOI ELLE EST OBLIGATOIRE. La boucle principale est
// libre, sans cadencement : elle tourne beaucoup plus vite que les 15 us
// d'une sequence ADC. Compter des PASSAGES DE BOUCLE compterait donc N fois
// le MEME echantillon latche, et l'anti-rebond ne filtrerait strictement
// RIEN tout en ayant l'air correct. On compte des sequences ADC.
//
// 667 sequences a 66,7 kHz font 10 ms ; 5 mesures espacees de 10 ms
// couvrent 50 ms, soit plusieurs milliers de periodes de decoupage a des
// phases non correlees. Une pointe de commutation n'y survit pas, un vrai
// echauffement si.
//
// NE TOUCHE PAS AU VERROUILLAGE. Ce reglage ne fait que retarder la mise a
// vrai de s_overtemp_tX. control_trip() latche toujours par-dessus, donc
// l'hysteresis de relachement reste inoperante comme avant -- c'est
// l'incoherence signalee dans hardware.md §7, dont la correction n'a PAS
// ete autorisee et n'est pas faite ici.
// PORTE A 100 LE 23/08/2026, SOIT 1 SECONDE. Les 5 mesures (50 ms) ont
// suffi jusqu'a 430 V et le defaut est reapparu a 440 V : effet de SEUIL,
// pas de degre. C'est la signature d'une rectification dans les diodes de
// protection de l'entree ADC -- sous le seuil de conduction le RC filtre,
// au-dessus il se fabrique du CONTINU, que rien ne filtre.
//
// 1 s reste gratuit : la fiche TDK donne tau_c = 10 s pour un 0805 sur
// PCB (p.5), le capteur est donc lui-meme un passe-bas dix fois plus lent
// que cette temporisation. On ne masque aucune dynamique thermique reelle.
//
// SERT AUSSI DE DISCRIMINATEUR. Si le code 5 disparait a 440 V, la
// perturbation etait seulement plus longue que 50 ms. S'il persiste, c'est
// bien du continu et le correctif est MATERIEL : 100 nF au POINT MILIEU du
// pont, au plus pres de la thermistance -- pas en aval du 10 k serie, ou
// se trouvent deja C8 et C24, qui filtrent ce qui arrive a l'ADC mais ne
// protegent pas le noeud ou le couplage se produit.
#define SAFETY_OVERTEMP_DECIM_SEQ  667U
#define SAFETY_OVERTEMP_COUNTS     100U

// ---- Timeout de la liaison ESP32 (PROMPT §6 etape 7) ----------------
// Sans trame $C valide au-dela de ce delai, la liaison est declaree
// perdue. La securite ne depend jamais de l'ESP32.
#define UART_LINK_TIMEOUT_MS     2000U

// ADC : reference interne obligatoire (VREFHI partage avec ADCINA0/VIN),
// pleine echelle 3,3 V. Conversion brut -> volts : V = raw * 3.3 / 4096.
#define ADC_VREF_V          3.3f

// Fenetre d'echantillonnage : ACQPS + 1 cycles SYSCLK -- et non ADCCLK, la
// distinction compte pour tout ce qui suit. Duree totale d'une voie =
// acquisition + 13 cycles ADCCLK de conversion (30 MHz, soit 433 ns), les
// deux phases etant sequentielles a cause de ADCNONOVERLAP.
//
// DEUX fenetres, parce que les sources n'ont rien de comparable.
//
// Voies TAMPONNEES (toutes sauf les thermistances) : un suiveur suivi d'un
// RC 1 kOhm + 10 nF a la broche. Ce n'est plus l'ampli qui charge le
// condensateur d'echantillonnage mais le condensateur local, avec une
// constante de temps de quelques nanosecondes. 7 cycles suffisent donc
// largement, et c'est CE raccourcissement qui rend possible le placement de
// toutes les voies utiles dans la fenetre propre du cycle de decoupage.
// 6 est le minimum autorise par le silicium.
//
// Voies THERMISTANCES : pas de suiveur, source haute impedance. Elles
// gardent une fenetre longue. Elles sont lentes, leur position dans la
// sequence n'a aucune importance.
#define ADC_ACQPS_FAST      6    // 7 cycles = 117 ns -> 550 ns par voie
#define ADC_ACQPS_SLOW      25   // 26 cycles = 433 ns -> 866 ns par voie

// Duree d'une voie rapide, en counts de TBCLK. TBCLK = SYSCLK ici (TB_DIV1
// et HSPCLKDIV = 1), la conversion est donc directe : 550 ns = 33 counts.
#define ADC_FAST_SLOT_COUNTS   33

// Placement du declenchement, en counts avant le milieu de la conduction.
//
// Un essai de decalage aux TROIS QUARTS a ete tente puis ANNULE le
// 17/08/2026 : il destabilisait la regulation. Motif detaille dans
// pwm_apply_adc_trigger() (pwm.c) -- a lire avant de retenter.
//
// On centre sur I1 SEULE, et non sur le groupe des quatre voies rapides.
// Seul terme retenu : la fenetre d'acquisition, l'ADC echantillonnant a sa
// FIN. I1 etant en tete de sequence, son instant d'echantillonnage tombe
// alors exactement sur le point vise.
//
// La version precedente centrait le GROUPE, ce qui rejetait I1 1,5 creneau
// en avance -- 250 ns apres l'amorcage a D = 0,43, en pleine transition.
// Deux raisons de ne plus le faire :
//
//  1. I1 est la seule voie dont l'instant compte. VIN, V1 et VOUT sont des
//     tensions aux bornes de gros condensateurs, derriere un RC 1 k/10 nF
//     dont les 10 us moyennent deja sur deux periodes de decoupage. IIN et
//     IOUT passent par des ZXCT1109 dont le shunt est EN AMONT du
//     condensateur d'entree : verifie au scope, le courant y est continu,
//     sans ondulation ni pointe de commutation.
//
//  2. Les quatre voies rapides occupent 4 x 33 = 132 counts alors que la
//     conduction n'en dure que 128 a ce rapport cyclique. Le groupe ne
//     rentre pas dans la fenetre propre : vouloir l'y centrer n'avait pas
//     de solution.
// +30 COUNTS AJOUTES LE 23/08/2026, PAR LA MESURE.
//
// Balayage automatique a 430 V (ADC_TRIG_SWEEP), 7 pas de 10 counts,
// enveloppe d'oscillation relevee au scope en mode Scan :
//
//   -30  calme     nominal  BRUYANT     +20  BRUYANT
//   -20  calme     +10      calme       +30  CALME (30 s confirmees)
//   -10  calme
//
// L'ENVELOPPE DEPEND DONC BIEN DE L'INSTANT D'ECHANTILLONNAGE : le
// bouclage decrit dans pwm.c (bruit sur V1 -> integrateur -> duty -> le
// front bouge -> le bruit change) est actif. C'est ce qui a permis de NE
// PAS depenser de marge de gain pour stabiliser la boucle.
//
// ATTENTION, LA REPONSE N'EST PAS MONOTONE. Une explication par le seul
// debordement de la sequence ADC sur le front d'extinction predirait une
// amelioration monotone ; ce n'est pas ce qu'on mesure. Le mecanisme reel
// n'est PAS etabli, et les deux zones bruyantes espacees de 20 counts
// (333 ns) ne correspondent ni aux 550 ns entre voies ADC, ni a la periode
// de l'etage 2. NE PAS extrapoler cette valeur a un autre point de
// fonctionnement sans refaire le balayage.
//
// >>> +30 ESSAYE PUIS RETIRE LE MEME JOUR. <<<
// Le balayage donnait +30 comme point calme, mais l'essai de bout en bout
// l'a CONTREDIT : 440 V tenait 2 min avant, 430 V n'a tenu que 10 s avec.
// L'enveloppe observee pendant 30 s de balayage n'a donc pas predit le
// comportement en tenue longue.
//
// LECON. Le balayage mesure une AMPLITUDE D'ENVELOPPE sur quelques
// dizaines de secondes ; la grandeur qui compte est le TEMPS AVANT
// COUPURE, qui suit une loi de probabilite a queue longue. Les deux ne
// sont pas le meme critere, et le premier ne substitue pas au second.
// Toute reprise du balayage doit se conclure par un essai de tenue.
#define ADC_TRIG_LEAD_COUNTS   (ADC_ACQPS_FAST + 1)

// ---- BALAYAGE AUTOMATIQUE DE L'INSTANT D'ECHANTILLONNAGE ------------
// BANC UNIQUEMENT. DOIT rester a 0 en fonctionnement normal.
//
// POURQUOI IL EXISTE. Le bouclage d'auto-entretien decrit dans pwm.c --
// bruit de commutation sur V1 -> l'integrateur corrige -> le duty bouge ->
// le front se deplace -> le bruit change -- se teste en deplacant l'instant
// d'echantillonnage et en regardant si l'enveloppe d'oscillation change
// d'amplitude. Cela devait se faire au debogueur sur g_adc_trig_lead.
//
// IMPOSSIBLE EN PRATIQUE : la session JTAG decroche des que la carte
// commute (point ouvert connu), et la sonde XDS100v3 refuse le test de
// connexion en dessous de 1 MHz de TCLK. Le firmware balaie donc lui-meme,
// et le SCOPE SEUL suffit a lire le resultat.
//
// PROTOCOLE. Le balayage demarre a la demande de marche et avance d'un pas
// toutes les ADC_TRIG_SWEEP_DWELL_TICKS (base 10 ms). Il part de
// -(STEPS/2) pas et monte, puis SE FIGE a la derniere valeur -- il ne
// reboucle pas, pour qu'un releve tardif reste interpretable. Il suffit
// donc de compter les intervalles depuis RUN pour savoir ou on est.
//
// 7 pas de 10 counts (167 ns) toutes les 10 s : 70 s de balayage, couvrant
// -30 a +30 counts autour de la valeur nominale. Tient largement dans les
// 2 a 3 minutes que la carte soutient a 430 V.
//
// Les ecretages de pwm_apply_adc_trigger() bornent CMPB des deux cotes :
// le balayage ne peut pas sortir de la plage utile ni eteindre la sequence
// ADC. Si un reglage fait franchement osciller, ca coupe -- et c'est en soi
// le resultat cherche.
// Remis a 0 le 23/08/2026, essai fait : le resultat est fige dans
// ADC_TRIG_LEAD_COUNTS ci-dessus. Repasser a 1 pour rebalayer a un autre
// point de fonctionnement -- la valeur retenue n'y est pas transposable.
#define ADC_TRIG_SWEEP            0
#define ADC_TRIG_SWEEP_STEPS      7
#define ADC_TRIG_SWEEP_STEP       10
#define ADC_TRIG_SWEEP_DWELL_TICKS 1000U  // 10 s a 10 ms

// INSTRUMENTATION TEMPORAIRE -- voir l'en-tete d'adc.c pour le detail et
// pour l'avertissement de securite. A 1, la DUREE DE L'ISR ADC est marquee
// sur HV_EN et bsp_gpio.c neutralise hv_enable_set().
// REMETTRE A 0 AVANT TOUT ESSAI EN TENSION.
#define ADC_TIMING_PROBE   0

// ---- ESSAI ETAGE 2 EN BOUCLE OUVERTE --------------------------------
//
// DANGEREUX DES QUE LE MOSFET DE L'ETAGE 2 EST MONTE. A 1, l'etage 2 sort
// un rapport cyclique FIXE, sans aucune contre-reaction : sur un boost
// alimente par V_inter, la tension de sortie monte alors jusqu'au seuil de
// coupure a 520 V, ou jusqu'a la destruction si ce seuil defaille.
// N'a de sens QUE tant que le MOSFET principal de l'etage 2 est ABSENT --
// et le firmware n'a aucun moyen de le verifier. REMETTRE A 0 AVANT DE LE
// PEUPLER.
//
// Objet de l'essai : verifier la chaine EPWM2A -> porte ET IC9 -> driver,
// l'armement de Stage2-EN, la commande de HV_EN, et lire une telemetrie
// stable sur les voies VOUT / I2 / IOUT / T2.
//
// CE QUI RESTE INTACT, et ce n'est pas negociable : la decharge active, les
// Trip Zones, les seuils de survoltage, le verrouillage des defauts et
// l'etat sur. L'essai ouvre une sortie PWM, il ne desarme aucune securite.
//
// CE QUI CHANGE, aux trois seuls endroits marques STAGE2_OPENLOOP_TEST :
//   control.c  - l'etage 2 sort le duty fixe en regime etabli (CTRL_STATE_RUN)
//                et lui seul ; both_off() le remet a zero sur defaut.
//              - toute consigne VOSET activant l'etage 2 est REFUSEE, pour
//                que regulation et boucle ouverte ne puissent pas coexister.
//              - HV_EN n'exige plus l'etage 2 regule, seulement RUN.
//   main.c     - la porte ET de l'etage 2 et sa sortie ePWM sont armees.
//
// HV_EN reste subordonne a la commande HT de l'operateur : l'essai le rend
// possible, il ne le force pas.
#define STAGE2_OPENLOOP_TEST       0

// Rapport cyclique fixe de l'etage 2, en POURCENT ENTIER -- le
// preprocesseur ne sait pas comparer des flottants, et ce garde-fou vaut
// mieux qu'une ecriture plus jolie.
#define STAGE2_OPENLOOP_DUTY_PCT   20U

#if STAGE2_OPENLOOP_TEST
#if (STAGE2_OPENLOOP_DUTY_PCT > 50U)
#error "STAGE2_OPENLOOP_DUTY_PCT > 50 % en boucle ouverte : refus. Sans contre-reaction, le duty fixe determine seul la tension de sortie."
#endif
#if ADC_TIMING_PROBE
#error "ADC_TIMING_PROBE neutralise hv_enable_set() : HV_EN n'obeirait pas pendant l'essai. Les deux sont exclusifs."
#endif
#endif

// =====================================================================
// Regulation et bornes d'exploitation
// =====================================================================

// ---- Bornes des consignes acceptees ---------------------------------
// Une consigne hors de ces bornes est REFUSEE : l'ancienne est conservee
// et le rejet est signale en telemetrie. On ne sature pas silencieusement,
// sinon une erreur de commande passerait inapercue.
#define CTRL_V1_SET_MIN_V       15.0f
// 77 et non 75, alors que le POINT DE FONCTIONNEMENT retenu est 75 V fixe :
// se poser pile sur une borne de validation en virgule flottante est
// fragile. Une conversion texte->flottant cote ESP32 rendant 75,000001
// ferait refuser la consigne a chaque trame, REJ monterait, et V1 resterait
// silencieusement a sa valeur d'init (15 V). 2 V de jeu suppriment le cas.
//
// Sans effet sur la marge reelle : le plafond est un garde-fou, pas la
// consigne. L'ESP32 envoie 75,0 fixe, donc 7 V subsistent jusqu'au seuil de
// survoltage CTRL_V1_OV_TRIP_V (82 V).
//
// PASSAGE DE 50 A 75 V, 23/08/2026. Motif : l'etage 2 y gagne sur trois
// axes -- D tombe de 0,90 a 0,84, son courant d'entree baisse de 1,6x a
// puissance egale (donc moins de courant commute, donc moins de pointe
// L.di/dt sur les shunts), et l'ondulation dans C6 baisse de 0,61 a 0,54 A.
// L'ETAGE 1 N'Y GAGNE RIEN : I1 est fixe par VIN et la puissance d'entree,
// pas par V1. Son rapport cyclique monte au contraire de 0,60 a 0,73 a
// VIN = 20 V, donc son ondulation croit d'environ 25 %.
//
// LIMITE HAUTE, NE PAS DEPASSER SANS CHANGER C6. Le condensateur de sortie
// de l'etage 1 (UCM2A221M, refs C6/C7) est un chimique 100 V. A 75 V on est
// a 75 % du calibre, et le seuil de survoltage a 82 V y ajoute un transitoire
// a 82 %. C'est C6 qui fixe ce plafond, pas la chaine de mesure.
#define CTRL_V1_SET_MAX_V       77.0f
#define CTRL_VOUT_SET_MIN_V    200.0f
#define CTRL_VOUT_SET_MAX_V    500.0f

// Consigne de sortie EXACTEMENT nulle : convention signifiant "etage 2
// desactive". La machine s'arrete alors a l'etage 1 etabli, l'etage 2 reste
// a duty 0 et HV_EN est interdit. C'est le mode de validation du seul
// premier etage, quand le MOSFET, la diode et l'inductance de l'etage 2 ne
// sont pas montes -- sans lui la machine resterait bloquee en START_S2, la
// sortie ne pouvant jamais atteindre 200 V.
//
// Zero est sans ambiguite : ce n'est pas une consigne de sortie plausible,
// et toute valeur strictement comprise entre 0 et la borne basse reste
// refusee comme avant.
#define CTRL_VOSET_DISABLED      0.0f

// ---- Seuils de coupure en survoltage --------------------------------
// Verifies dans l'ISR ADC par simple comparaison sur la valeur brute.
// Restent sous les pleines echelles mesurees, donc la mesure ne sature
// jamais avant que la protection n'agisse.
//
// ATTENTION, LA PLEINE ECHELLE V1 VAUT 96,6 V ET NON 103. Les 103 V
// dataient du gain V0.1 (30,82) et trainent encore dans hardware.md. Avec le
// gain V0.2 mesure (MEAS_V1_GAIN_V_PER_V = 29,27), la conversion vaut
// 4096 / (3,3 x 29,27) = 42,4 raw/V, soit 96,6 V de pleine echelle. Le seuil
// a 82 V est donc a 85 % de l'echelle : l'invariant ci-dessus tient toujours,
// avec moins de marge qu'annonce. Au-dela de 90 V il faudrait revoir le pont
// diviseur avant de toucher a ce seuil.
#define CTRL_V1_OV_TRIP_V        82.0f
#define CTRL_VOUT_OV_TRIP_V     520.0f

// ---- Sous-tension d'entree ------------------------------------------
// Tension d'entree minimale de conception : 10 V. En dessous, defaut
// verrouille et coupure du PWM, comme pour une survoltage.
//
// Le seuil est place 0,5 V SOUS le minimum annonce. A 10 V pile, l'ondulation
// d'entree et le creux d'un echelon de charge feraient sinon tomber un defaut
// dont on ne sort qu'en coupant l'alimentation.
#define CTRL_VIN_UV_TRIP_V        9.5f

// ARMEMENT. Au demarrage VIN traverse forcement la zone basse pendant la
// montee de l'alimentation : surveiller des le reset rendrait la carte
// impossible a demarrer. La surveillance ne s'arme donc qu'apres que VIN a
// une fois depasse ce seuil, et elle le reste jusqu'au prochain reset.
#define CTRL_VIN_UV_ARM_V        12.0f

// ANTI-REBOND. Nombre de sequences ADC consecutives sous le seuil avant de
// verrouiller. 5 a 66,7 kHz font 75 us, soit quinze periodes de decoupage :
// une ondulation ne peut pas y survivre, un vrai effondrement si.
//
// Ce n'est pas du filtrage qui masque le probleme -- c'est de l'anti-rebond
// sur une protection VERROUILLANTE, dont un declenchement intempestif coute
// un cycle d'alimentation complet.
#define CTRL_VIN_UV_COUNTS        5U

// ---- REARMEMENT AUTOMATIQUE, SOUS-TENSION D'ENTREE UNIQUEMENT --------
//
// A 1, un defaut FAULT_UNDERVOLTAGE_VIN se deverrouille tout seul quand
// l'alimentation d'entree est franchement revenue. LES AUTRES DEFAUTS NE
// SONT PAS CONCERNES et ne doivent pas l'etre : surintensite, surtension et
// surtemperature signalent une avarie ou un fonctionnement hors domaine,
// dont la disparition apparente ne prouve rien.
//
// POURQUOI CELUI-LA PEUT L'ETRE : la sous-tension d'entree decrit l'etat de
// la SOURCE, pas du convertisseur. Rien n'a ete endommage, et le seuil de
// coupure (9,5 V) est deja separe du seuil d'armement (12,0 V) par 2,5 V
// d'hysteresis -- de quoi exclure tout battement.
//
// LE PIEGE, ET POURQUOI IL Y A UN PLAFOND DE TENTATIVES : si Vin s'effondre
// PARCE QUE le convertisseur tire trop, le rearmement cree une boucle
// demarrage -> appel de courant -> effondrement -> coupure -> demarrage.
// Chaque cycle repasse par la rampe complete, donc par le duty le plus
// eleve, sur un MOSFET qui n'a pas refroidi. Pire : il masquerait une source
// sous-dimensionnee en la transformant en clignotement. Apres
// CTRL_VIN_UV_MAX_RETRIES tentatives, ce n'est plus un incident mais un
// diagnostic -- le defaut se verrouille pour de bon.
#define CTRL_VIN_UV_REARM         1

// Duree pendant laquelle VIN doit rester au-dessus de CTRL_VIN_UV_ARM_V
// avant de rearmer, en pas de regulation (~5,13 kHz). 2560 -> ~500 ms.
// Ce n'est pas de l'anti-rebond mais une exigence de STABILITE : un simple
// franchissement instantane serait satisfait par le rebond d'une source qui
// vient justement de s'effondrer.
#define CTRL_VIN_UV_REARM_STEPS   2560U

// Le compteur est remis a zero des que la machine atteint CTRL_STATE_RUN,
// c'est-a-dire apres un etablissement REEL de l'etage 1 (rampe terminee et
// ecart sous tolerance pendant CTRL_SETTLE_STEPS). Une source correcte qui
// bronche une fois par heure ne verrouillera donc jamais, alors qu'un cycle
// qui n'atteint jamais le regime etabli s'arrete au bout de trois essais.
#define CTRL_VIN_UV_MAX_RETRIES   3U

// ---- Limites de rapport cyclique ------------------------------------
// duty max < 1 imperativement : a 500 V depuis 35 V il faut deja D = 0,93,
// la marge est donc mince. duty min a 0 : un boost a 0 % laisse malgre tout
// passer Vin par L et la diode, ce n'est pas une coupure.
#define CTRL_DUTY_MIN            0.0f
#define CTRL_DUTY_MAX            0.95f

// ---- Loi de commande PID, virgule fixe -------------------------------
//
//   duty_counts = P + I + D
//
// Les trois gains sont des PUISSANCES DE DEUX : la loi ne contient que des
// decalages, des additions et des comparaisons. C'est la regle du projet
// pour tout ce qui tourne en ISR -- ni multiplication, ni division.
//
//   P = erreur                << CTRL_KP_SHIFT   ->  Kp = 2^CTRL_KP_SHIFT
//   I = accumulateur          >> CTRL_KI_SHIFT   ->  Ki = 1 / 2^CTRL_KI_SHIFT
//   D = (erreur - precedente) << CTRL_KD_SHIFT   ->  Kd = 2^CTRL_KD_SHIFT
//
// ATTENTION AU SENS DES DECALAGES. Kp et Kd sont des decalages a GAUCHE,
// Ki un decalage a DROITE. Ce n'est pas une coquette : l'unite de l'erreur
// est le count d'ADC, celle de la sortie le count de duty, et le rapport
// utile entre les deux est SUPERIEUR A 1 (voir le dimensionnement de Kp
// ci-dessous). Une formulation en `erreur >> n` serait structurellement
// incapable d'atteindre le gain necessaire.
//
// ---- Dimensionnement de Kp, a partir du releve du 20/08/2026 ---------
//
// Delestage a 100 mA, etage 1 a 50 V, mesure au scope a 160 kHz :
// la sortie monte de 49,0 a 54,2 V en 5 ms, puis reste sur ce palier plus
// de 30 ms sans que l'integrateur seul ne la ramene (il ne retire que 6
// counts de duty sur 300 pendant ce temps). Le seuil de survoltage etant a
// 55 V, un tir precedent avait verrouille un defaut.
//
// De ce releve on tire :
//   - condensateur de sortie   C = P/(V.dV/dt) = 5/(50 x 1040) ~ 100 uF
//   - sensibilite              dV/dD = Vin/(1-D)^2 ~ 178 V par unite de D,
//                              soit ~0,6 V par count de duty sur 300
//   - echelle de mesure        1 V = 39,8 counts d'ADC sur la voie V1
//
// Pour que la montee s'arrete vers +1 V, il faut retirer environ 120 counts
// de duty pour 40 counts d'erreur, soit Kp ~ 3. La valeur de DEPART est
// volontairement fixee au TIERS de ce calcul (Kp = 1), parce que L et C
// restent des estimations et que le calcul ne vaut qu'au point mesure.
//
// METHODE D'AFFINAGE : monter d'UN decalage a la fois, et observer le CREUX
// DE BRANCHEMENT de la charge, pas le delestage. Meme information dynamique,
// energie bien plus faible, et aucun risque d'approcher les 55 V pendant la
// recherche. Symptome d'un Kp excessif : oscillation entretenue autour de la
// consigne.
#define CTRL_KP_SHIFT             0U   // Kp = 1  (prudent : calcul -> 3)

// ---- DIVISION SUPPLEMENTAIRE DU TERME P (23/08/2026) ----------------
// Kp EFFECTIF = 2^CTRL_KP_SHIFT / 2^CTRL_KP_DIV_SHIFT.
//
// POURQUOI CE SECOND SYMBOLE plutot que descendre CTRL_KP_SHIFT : c'est un
// decalage a GAUCHE, 0 en est le minimum. Kp ne pouvait pas descendre sous
// 1 sans ce complement.
//
// MOTIF. L'enveloppe du courant oscillait a 2,3 kHz avec une cadence de
// regulation a 5,13 kHz, soit exactement f/2. Doubler la cadence (195 ->
// 98 us) a fait SUIVRE l'oscillation a ~5 kHz au lieu de la supprimer : le
// pole reste au voisinage de z = -1, donc le gain est encore trop eleve.
// La cadence ayant deja ete doublee, c'est le gain qui doit baisser.
//
// LE PRIX, A CONNAITRE. Le terme P est le SEUL qui reponde a un delestage
// -- l'integrateur a besoin de plusieurs pas pour batir sa correction. Le
// diviser par deux divise par deux la reponse au delestage, et il ne reste
// que 20 V de marge sous la coupure a 520 V, jamais testes en delestage
// sur l'etage 2. A caracteriser avant toute montee a 500 V.
#define CTRL_KP_DIV_SHIFT         1U   // Kp effectif = 0,5

// ---- Resultat mesure avec Kp = 1, le 20/08/2026 ----------------------
//
// Meme delestage qu'au releve ci-dessus :
//   depassement        5,2 V  ->  1,0 V     (marge sous 55 V : 0,8 V -> ~5 V)
//   extinction         palier de 30 ms+  ->  1,5 ms
//
// Sur la marche, un train amorti apparait a 5,19 kHz mesure au curseur.
// C'EST LE PAS DE REGULATION LUI-MEME (195 us -> 5,13 kHz), PAS UNE
// INSTABILITE. La distinction est essentielle et se lit sur la frequence :
//   - une boucle echantillonnee instable oscille a fs/2, soit 2,56 kHz,
//     un echantillon dessus, le suivant dessous ;
//   - a fs, on voit la discretisation de la commande : avec Kp = 1 et une
//     erreur de quelques dizaines de counts, le duty saute de plusieurs
//     pour-cent en un seul tick, et la serie de sauts s'eteint avec l'erreur.
// La resonance propre du convertisseur, elle, est ailleurs : L/(1-D)^2 avec
// 100 uF donne ~445 Hz, soit moins d'une periode sur la fenetre observee.
//
// Il reste donc de la marge avant fs/2. Elle n'est PAS consommee, et c'est
// un choix de conception, pas une precaution : l'etage 1 doit rester LENT
// devant l'etage 2 (voir la note sur la cascade plus bas). Ne pas monter Kp
// sans reprendre cette note.

// Gain integral inchange depuis le bring-up : c'est la seule valeur validee
// en marche, elle sert de reference pendant le reglage de Kp. Il supprime
// l'erreur statique, il ne fait pas la vitesse -- un integrateur pur retarde
// de 90 degres a toute frequence, augmenter son gain coute de l'amortis-
// sement exactement autant que ca rapporte de rapidite. C'est le terme
// proportionnel qui apporte les deux ensemble.
// PORTE DE 12 A 13 LE 23/08/2026, EN MEME TEMPS QUE CTRL_TICK_PERIOD_US.
// Ce n'est PAS un rereglage du PID : la cadence ayant double, diviser Ki
// par pas par deux conserve EXACTEMENT la meme constante de temps
// integrale en secondes. La boucle est identique, seulement echantillonnee
// deux fois plus vite. Les deux constantes sont indissociables -- toucher
// l'une sans l'autre change le comportement.
//
// s_accum_max suit automatiquement : control.c le calcule en
// duty_max << CTRL_KI_SHIFT.
#define CTRL_KI_SHIFT            13U   // Ki = 1/8192 par pas

// ---- Terme derive : DESACTIVE ----------------------------------------
//
// A 0, le terme n'est pas calcule du tout (compile hors de la boucle).
//
// POURQUOI IL RESTE A ZERO, et ce n'est pas une etape a franchir plus tard
// par principe : la voie V1 est echantillonnee UNE SEULE FOIS par cycle de
// decoupage, avec une quantification de 25 mV par count. Une derivee sur ce
// signal amplifie la quantification et le bruit d'echantillonnage bien avant
// d'apporter de l'amortissement. Un PI suffit sur un boost, et c'est la
// solution usuelle.
//
// Si le besoin s'en faisait sentir, il faudrait d'abord filtrer la mesure --
// donc rajouter du retard, donc reprendre le reglage de Kp depuis le debut.
#define CTRL_KD_ENABLE            0
#define CTRL_KD_SHIFT             0U

// ---- Cascade : pourquoi l'etage 1 doit rester LENT -------------------
//
// Un etage 2 regule se comporte, vu de son entree, comme une charge a
// PUISSANCE CONSTANTE : si V_inter baisse, il tire plus de courant. C'est
// une resistance differentielle NEGATIVE, environ -500 Ohms a 50 V et 5 W.
// Deux boucles de vitesses voisines qui s'affrontent a travers ca, c'est le
// mecanisme classique d'instabilite en cascade.
//
// Ce qui protege ici n'est PAS la lenteur de la boucle, c'est le RAPPORT DES
// CONDENSATEURS : 100 uF sur l'etage 1 contre 1 uF sur l'etage 2. Dans la
// bande ou l'etage 2 travaille, l'impedance de sortie de l'etage 1 est fixee
// par son condensateur et non par son correcteur -- 1,6 Ohm a 1 kHz, contre
// 500 Ohms de charge negative. L'etage 1 est raide par construction, il peut
// donc se permettre d'etre lent.
//
// PLANCHER : "lent" s'arrete la ou l'etage 1 doit repondre pour LUI-MEME,
// c'est-a-dire son propre delestage, celui qui verrouillait un defaut de
// survoltage avant l'ajout de P. Kp = 1 satisfait ce plancher avec ~5 V de
// marge. C'est le critere a revalider si un gain change.
//
// ---- CONSEQUENCE POUR L'ETAGE 2, A TRANCHER AVANT DE PEUPLER LE MOSFET --
//
// Avec 1 uF au lieu de 100 uF, dV/dt est CENT FOIS plus rapide a puissance
// comparable : le transitoire de 5 ms mesure sur V1 devient une CINQUANTAINE
// DE MICROSECONDES sur V_HT. Le pas de regulation valant 195 us, le
// transitoire est termine AVANT LE PREMIER PAS.
//
// Aucun reglage de PID ne peut y repondre -- la boucle arrivera toujours
// apres. Trois issues, aucune n'est un reglage logiciel :
//   - augmenter le condensateur de sortie de l'etage 2, pour ramener le
//     transitoire dans la bande de la boucle ;
//   - accelerer le pas de regulation de l'etage 2 (couteux, et borne par le
//     temps d'ISR deja mesure a l'oscilloscope) ;
//   - s'en remettre au materiel : Trip Zones et seuil a 520 V, qui jouent
//     deja ce role.

#if CTRL_KD_ENABLE
#warning "Terme derive actif : la voie V1 n'est echantillonnee qu'une fois par cycle de decoupage, verifier le bruit sur DUTY avant d'y croire."
#endif

// Periode du pas de regulation, en microsecondes, imposee par le CPU Timer 1.
// 195 us -> 5128 Hz, soit exactement la cadence qu'obtenait la decimation par
// 13 des 66,7 kHz de l'ADC : les gains PID et les vitesses de rampe gardent donc
// la meme signification qu'avant le changement de contexte.
//
// La regulation a quitte l'ISR ADC : une sequence sur treize, celle-ci
// depassait 9 us et debordait sur la conversion suivante. Voir control.c.
// ---- DIVISE PAR DEUX LE 23/08/2026 ----------------------------------
// MESURE, pas hypothese : l'enveloppe du courant d'entree oscille en
// permanence a ~2,3 kHz au scope, contre 5,128/2 = 2,56 kHz attendus.
// C'est une OSCILLATION SOUS-HARMONIQUE a f_echantillonnage/2, signature
// d'un pole discret passe au voisinage de z = -1 : la boucle a trop de
// gain POUR SA CADENCE. Ce n'est pas un reglage de PID a retoucher, c'est
// la cadence qui est trop lente pour le plant.
//
// Doubler la cadence divise par deux le gain de boucle PAR ECHANTILLON --
// le plant a moitie moins de temps pour bouger entre deux corrections --
// et eloigne donc le pole de z = -1.
//
// SECOND EFFET, tout aussi important : l'excursion en BOUCLE OUVERTE est
// divisee par deux. Les salves d'emballement relevees au scope duraient
// 130 us et tenaient donc ENTIEREMENT dans un intervalle de 195 us : le
// regulateur ne les voyait qu'une fois terminees.
//
// LE PLANT S'EST DURCI EN COURS DE ROUTE. Le passage de V1 a 75 V a monte
// D de 0,60 a 0,733, donc baisse le zero a droite du boost -- qui varie
// en (1-D)^2 -- d'un facteur 2,2. On a rendu le convertisseur plus
// difficile a commander en gardant la meme cadence de boucle.
//
// COMPENSATIONS OBLIGATOIRES, sans quoi ce changement EMPIRE tout :
// CTRL_KI_SHIFT et les deux CTRL_RAMP_* sont exprimes PAR PAS. Doubler la
// cadence sans les diviser par deux doublerait le gain integral par
// seconde et la vitesse de rampe -- on remplacerait une instabilite par
// une autre. Les trois constantes vont ensemble.
//
// BUDGET CPU : Timer 1 passe de 5,1 a 10,2 kHz. Il est en INT13, preempte
// par l'ISR ADC (INT1), donc l'integrite des mesures n'est pas en jeu ;
// seule la charge totale l'est. A MESURER avec ADC_TIMING_PROBE.
#define CTRL_TICK_PERIOD_US       98UL

// ---- Rampe de demarrage ----------------------------------------------
// On rampe la CONSIGNE et non le duty : la boucle reste fermee pendant
// toute la montee. Exprimee en volts par pas de regulation.
// 0,005 V/pas a 10,2 kHz -> ~51 V/s sur l'etage 1, la montee de 10 a 50 V
// prend donc environ 0,8 s.
//
// DIVISES PAR DEUX LE 23/08/2026 avec CTRL_TICK_PERIOD_US : ces constantes
// sont exprimees PAR PAS, donc les laisser telles quelles aurait double la
// vitesse de rampe en volts par seconde. Les valeurs en V/s sont
// INCHANGEES, c'est le but -- on ne modifie que la cadence de la boucle,
// pas son comportement.
#define CTRL_RAMP_V1_V_PER_STEP     0.005f
#define CTRL_RAMP_VOUT_V_PER_STEP   0.05f

// ---- Criteres de passage d'etat --------------------------------------
// L'etage est declare etabli quand l'ecart reste sous tolerance pendant
// cette duree, exprimee en pas de regulation (~5,1 kHz).
#define CTRL_SETTLE_TOL_V1_V        1.0f
#define CTRL_SETTLE_TOL_VOUT_V     10.0f
#define CTRL_SETTLE_STEPS         500U   // ~100 ms

// Frequences de decoupage par etage (point ouvert §9.1 du PROMPT, tranche au
// bring-up). TBCLK = SYSCLKOUT = 60 MHz, TBPRD = SYSCLKOUT/Fpwm - 1 :
//   etage 1 : 200 kHz -> TBPRD = 299
//   etage 2 : 100 kHz -> TBPRD = 599
#define PWM_STAGE1_FREQ_HZ  200000UL
#define PWM_STAGE2_FREQ_HZ  100000UL

// ---- DECALAGE DE PHASE DE L'ETAGE 2 ---------------------------------
//
// Valeur chargee dans TBCTR de l'ePWM2 pendant que TBCLKSYNC est a zero
// (pwm.c). Les deux compteurs demarrent ensuite ensemble sur la meme
// TBCLK, avec un rapport de periode exactement 2:1 : la phase est donc
// FIXE et reproductible, et ce decalage la choisit une fois pour toutes.
//
// RECTIFICATION DU 20/08/2026 : une note precedente affirmait que cette
// phase etait "fixee au hasard du demarrage". C'est faux -- pwm.c chargeait
// deja TBCTR = 0 sur LES DEUX, horloges gelees. Le resultat etait pire
// qu'un alea : les deux commutations tombaient AU MEME INSTANT une periode
// sur deux, additionnant les parasites des deux grilles. C'est ce qui
// donnait, au scope sur I1, une impulsion sur deux d'amplitude differente,
// et 100 kHz mesures sur un signal qui commute a 200.
//
// ---- Comment la valeur est choisie ----------------------------------
//
// En counts de l'ePWM2 (periode 600). L'etage 1 occupe la meme fenetre
// avec deux periodes de 300, ses fronts fixes tombent donc a 0 et 300.
//
// Fronts, dans ce repere :
//   etage 1 : 0 et 300           (FIXES, passage a zero)
//             CMPA1 et 300+CMPA1 (MOBILES, coupure)
//   etage 2 : la valeur ci-dessous            (FIXE)
//             + duty2 x 600                   (MOBILE, coupure)
//
// CMPA1 = D1 x 300 avec D1 = 1 - Vin/V1. Sur la plage d'entree 10 a 24 V
// pour V1 = 50 V, D1 va de 0,52 a 0,80, donc CMPA1 balaie 156 a 240 --
// et son homologue de la seconde periode, 456 a 540.
//
// Il reste deux fenetres libres en pire cas : 240-300 et 540-600. On se
// place au debut de la premiere, l'etage 2 ayant un duty faible (~5 %,
// soit 30 counts) ses deux fronts y tiennent.
#define PWM_STAGE2_PHASE_COUNTS  250U

// ---- CE QUE CE DECALAGE NE PEUT PAS FAIRE ---------------------------
//
// Il separe les fronts FIXES. Il ne peut rien pour les fronts de COUPURE,
// qui se deplacent avec le rapport cyclique, donc avec la tension d'entree
// et avec la charge. Or ce sont eux qui rayonnent le plus : ils
// interrompent le courant d'inductance.
//
// Aucun decalage constant ne les separe a tous les points de
// fonctionnement -- c'est structurel, pas un defaut de reglage. Les
// parasites bougeront donc avec la charge, et il faut s'y attendre.
//
// Ce qu'on gagne quand meme, et qui justifie la ligne :
//   - les fronts fixes cessent de s'additionner ;
//   - la relation entre la commutation de l'etage 2 et l'instant
//     d'echantillonnage de l'ADC (place par adc.c au milieu de la
//     conduction de l'etage 1) devient CHOISIE au lieu d'etre subie.
//
// LA REPONSE STRUCTURELLE, si le couplage restait genant, serait de
// ramener les deux etages a la MEME frequence et de les entrelacer a 180
// degres : le decalage garderait alors son sens a tous les duties. Ca
// touche au dimensionnement de l'etage 2 (1 uF, mode de conduction), donc
// ce n'est pas un simple reglage.
//
// A REVERIFIER AU SCOPE si Vin, V1 ou une frequence de decoupage change :
// les fenetres libres ci-dessus sont calculees pour V1 = 50 V.
#if (PWM_STAGE2_PHASE_COUNTS >= 600U)
#error "PWM_STAGE2_PHASE_COUNTS doit rester sous la periode de l'etage 2 (600 counts a 100 kHz)."
#endif

// =====================================================================
// HRPWM -- RESOLUTION FINE DU RAPPORT CYCLIQUE
// =====================================================================
//
// POURQUOI. Ce n'est pas un raffinement, c'est la reponse a un mode de
// panne mesure au banc le 22/08/2026.
//
// La sensibilite d'un boost est dV/dD = Vin / (1-D)^2. Elle explose quand
// D approche 1, et c'est la que travaille l'etage 2 :
//
//   etage      Vin    Vout   D      periode   dV/dD      UN COUNT VAUT
//   -------------------------------------------------------------------
//   etage 1    20 V   50 V   0,60   300       125 V      0,42 V  (0,83 %)
//   etage 2    50 V   500 V  0,90   600       5000 V     8,3 V   (1,7 %)
//
// L'etage 2 doit donc tenir 500 V avec un quantum de 8,3 V. AUCUNE valeur
// entiere de CMPA ne donne la bonne tension : la regulation bascule en
// permanence entre deux counts adjacents. Ce n'est pas un defaut de
// reglage du PID -- les gains choisissent VERS QUEL count on bascule, pas
// la taille du count.
//
// Et chaque bascule d'un count a D = 0,9 rompt l'equilibre volt-seconde de
// pres de 2 %. L'inductance integre ce residu pendant les 195 us ou la
// commande est figee (CTRL_TICK_PERIOD_US) : le courant monte en escalier
// geometrique, +13 % par periode de decoupage releve au scope, soit un
// facteur 5 en 130 us. La plupart de ces escaliers avortent ; certains
// atteignent le seuil du comparateur et coupent la carte.
//
// C'est ce qui produisait les declenchements "ponctuels" a 400 V sur 50
// kOhm -- donc a 3,2 W, charge negligeable. Ce n'etait ni la puissance, ni
// la charge, ni l'artefact inductif de la chaine de mesure : c'etait la
// quantification du rapport cyclique.
//
// ---- CE QUE HRPWM APPORTE -------------------------------------------
//
// Le MEP (Micro Edge Positioner) subdivise un count de TBCLK en pas
// d'environ 150 ps. A 60 MHz un count vaut 16,67 ns, soit ~111 pas MEP.
// Le quantum de l'etage 2 passe donc de 8,3 V a environ 0,075 V.
//
// ---- CE QUE HRPWM N'APPORTE PAS -------------------------------------
//
// Rien contre les 195 us d'aveuglement entre deux pas de regulation. Un
// transitoire de charge plus rapide que le pas reste non regule, et
// l'escalier reste possible s'il est amorce par autre chose que la
// quantification. La reponse a CE probleme est le mode courant crete
// (comparateur route vers CBC au lieu de OST), qui reste a faire.
//
// HRPWM traite la cause d'aujourd'hui. Il ne rend pas la boucle rapide.
//
// ---- MISE EN OEUVRE --------------------------------------------------
//
// HRPWM n'existe que sur les sorties EPWMxA : les deux etages sont
// eligibles tels que routes (GPIO0 = EPWM1A, GPIO2 = EPWM2A).
//
// Le rapport cyclique circule maintenant en Q8 counts dans control.c et
// pwm.c : les 8 bits de poids faible sont la partie fractionnaire, portee
// par CMPAHR. 256 valeurs demandees pour ~111 pas MEP reels -- la
// redondance est sans consequence, le materiel arrondit.
#define PWM_DUTY_FRAC_BITS   8U

// Activation PAR ETAGE, pour pouvoir monter en deux temps au banc.
//
// L'etage 1 n'a PAS le probleme : 0,42 V par count, c'est deja fin.
// On l'active d'abord parce que c'est l'etage basse tension, celui ou une
// erreur de configuration se paie en observation et non en composant.
// N'attends aucune amelioration visible de son comportement : c'est une
// repetition du mecanisme, pas un correctif.
// LAISSES A 0 LE 22/08/2026 AU SOIR. Le code HRPWM est en place et compile,
// mais l'EDGMODE n'avait pas ete verifie au scope : tant qu'on ne sait
// pas quel front le MEP deplace, la partie fractionnaire peut agir en sens
// inverse de la partie entiere. A 0 sur les deux etages, aucune fraction
// n'est ecrite dans CMPAHR et la carte se comporte EXACTEMENT comme avant.
//
// A remettre a 1 apres l'essai de front, etage 1 d'abord.
// ATTENTION A LA COMBINAISON. PWM_HRPWM_EDGE_TEST ne suffit PAS a lui
// seul : pwm_set_duty_q8() force la partie fractionnaire a zero des que
// stage_has_hrpwm() est faux. Avec STAGE1 a 0, g_hr_test_frac n'aurait
// aucun effet et on conclurait a tort que le MEP ne fonctionne pas.
// L'essai de front exige donc STAGE1 a 1.
//
// LES DEUX ACTIFS depuis le 23/08/2026, front valide au banc (cf. EDGMODE).
//
// Etage 1 verifie en regulation reelle : V1 = 50,1 V au multimetre, et la
// commande sort a 45,625 counts -- donc la fraction est bien appliquee.
// Aucune amelioration visible de son comportement, et c'etait attendu : a
// 0,42 V par count, la resolution n'a jamais ete le facteur limitant sur
// cet etage. Ce qui fait bouger son rapport cyclique de trois ou quatre
// counts a vide, c'est le bruit de mesure sur V1 amplifie par le gain du
// convertisseur en conduction discontinue -- HRPWM n'y peut rien.
//
// L'etage 2 est celui pour lequel tout ceci a ete fait : 8,3 V par count.
#define PWM_HRPWM_STAGE1     1
#define PWM_HRPWM_STAGE2     1

// ---- QUEL FRONT LE MEP DOIT-IL DEPLACER ? ---------------------------
//
// LE POINT A VERIFIER AU BANC AVANT TOUTE MISE EN PUISSANCE. Se tromper
// ici est pire que de ne pas avoir HRPWM du tout.
//
// La conduction du MOSFET est bornee par deux fronts : celui de CTR = 0
// (fixe) et celui de CMPA (mobile). Seul le second est gouverne par
// CMPAHR. Si EDGMODE designe l'autre, le reglage fin s'applique a un front
// que CMPA ne commande pas : la partie fractionnaire agit alors EN SENS
// INVERSE de la partie entiere, et le rapport cyclique cesse d'etre une
// fonction croissante de la consigne. Une boucle fermee sur une commande
// non monotone ne converge pas.
//
// La difficulte vient de DBCTL.POLSEL = DB_ACTV_LO (driver UCC27517
// inverseur, cf. pwm.c) : le front descendant de la broche n'est pas le
// front descendant de l'Action Qualifier. Selon que le MEP est insere en
// amont ou en aval du sous-module Dead-Band, la bonne valeur est HR_FEP ou
// HR_REP -- et c'est exactement le genre de detail de chainage qui nous a
// deja coute une hypothese sur TZCTL.
//
// On ne le tranche pas sur documentation : PWM_HRPWM_EDGE_TEST fige un
// duty et laisse balayer la seule partie fractionnaire depuis le
// debogueur. Le front qui bouge au scope donne la reponse en deux minutes,
// sans puissance, sans risque.
// ---- TRANCHE AU BANC LE 23/08/2026 : HR_REP -------------------------
//
// Mesure, duty fige a CMPA = 150 sur TBPRD = 299, donc 50 % exactement :
//
//   fraction 0    -> PosDuty broche = 49,985 %   (reference)
//   fraction 255  -> PosDuty broche = 50,3 %     avec HR_FEP
//
// L'ecart est de +0,32 point, soit exactement la magnitude attendue pour
// 255/256 de count sur 300 (0,33), mais DANS LE MAUVAIS SENS.
//
// Le MEP ne sait que RETARDER un front. Si le niveau haut s'allonge, c'est
// le front DESCENDANT DE LA BROCHE qui a ete retarde -- celui de CTR = 0,
// que CMPA ne commande pas.
//
// HR_FEP a donc fait exactement ce qu'il annonce, un retard du front
// descendant, mais applique au signal DE LA BROCHE, c'est-a-dire APRES
// l'inversion du Dead-Band (DBCTL.POLSEL = DB_ACTV_LO, cf. pwm.c). Le MEP
// est donc insere EN AVAL du sous-module Dead-Band -- fait etabli par la
// mesure, pas par la documentation.
//
// L'inversion echangeant les deux fronts, celui de CMPA est le MONTANT a la
// broche : c'est HR_REP qu'il faut.
//
// VALIDATION EN TROIS POINTS avec HR_REP, meme montage :
//
//   fraction 0    -> 49,985 %    (reference)
//   fraction 128  -> 49,82 %     deplacement -0,165  (attendu -0,166)
//   fraction 255  -> 49,68 %     deplacement -0,305  (attendu -0,332)
//
// La moitie de la fraction donne la moitie du deplacement : la conversion
// est LINEAIRE et MONOTONE. C'est la seule propriete dont la regulation
// depende reellement -- une commande non monotone ne converge pas.
//
// Un pas de MEP vaut donc 0,0013 point de rapport cyclique. Sur l'etage 2,
// le quantum tombe de 8,3 V a 0,072 V.
//
// A REVERIFIER si DBCTL.POLSEL change un jour : les deux reglages sont lies.
//
// ---- SCINDE PAR ETAGE LE 23/08/2026 ---------------------------------
// Tout le raisonnement ci-dessus suppose la convention AQ d'origine
// (conduction 0 -> CMPA). L'etage 1 est passe a la convention inverse
// (conduction CMPA -> PRD, voir PWM_AQ_TAIL_STAGE1 plus bas), ce qui
// echange une nouvelle fois les fronts : a la broche, CMPA produit
// desormais le front DESCENDANT sur cet etage, donc HR_FEP.
//
// Deux inversions successives, et elles ne se compensent PAS -- le
// Dead-Band inverse le NIVEAU, l'Action Qualifier echange QUEL front est
// commande par CMPA. L'etage 2, reste en convention d'origine, garde
// HR_REP.
//
// LES DEUX VALEURS SONT A REVERIFIER AU BANC avec le protocole ci-dessus
// (duty fige, mesure par PosDuty, trois points pour la monotonie). Elles
// ne se deduisent pas : la position du MEP dans la chaine a ete etablie
// par la mesure, pas par la documentation.
#define PWM_HRPWM_EDGMODE_STAGE1   HR_FEP
#define PWM_HRPWM_EDGMODE_STAGE2   HR_REP

// GARDE-FOU. L'ancien symbole unique ne doit plus exister : une
// configuration perimee qui le definirait encore compilerait en silence
// avec un EDGMODE identique sur les deux etages, dont un faux. C'est
// exactement la classe de panne muette qui a coute la campagne du 23/08
// (SFO qui ne recopie pas MEP_ScaleFactor, s_hrpwm_ok jamais leve) : rien
// ne signale l'erreur, seul le front ne bouge pas.
#ifdef PWM_HRPWM_EDGMODE
#error "PWM_HRPWM_EDGMODE est scinde par etage : utiliser PWM_HRPWM_EDGMODE_STAGE1 / _STAGE2."
#endif

// Banc uniquement : expose g_hr_test_coarse et g_hr_test_frac, ecrits
// depuis le debogueur, et court-circuite le rapport cyclique de l'etage 1.
// DOIT rester a 0 en fonctionnement -- la regulation est alors ignoree sur
// cet etage.
#define PWM_HRPWM_EDGE_TEST  0

// Le MEP ne fonctionne pas si l'impulsion est trop courte ou trop proche
// de la periode : le TRM impose une marge de quelques cycles SYSCLK de
// part et d'autre. En dehors de cette plage on retombe sur la resolution
// entiere, ce qui est sans danger -- c'est le comportement d'avant.
#define PWM_HRPWM_GUARD_COUNTS  3U

#if (PWM_DUTY_FRAC_BITS != 8U)
#error "CMPAHR attend une fraction en Q16 : la conversion Q8 -> Q16 de pwm.c suppose 8 bits."
#endif

// =====================================================================
// Convention de l'Action Qualifier -- QUEL FRONT EST MOBILE
// =====================================================================
//
// CONVENTION D'ORIGINE (valeur 0) : ZRO = AQ_SET, CAU = AQ_CLEAR.
// La conduction occupe 0 -> CMPA. Le MOSFET s'amorce au passage a zero
// (FIXE) et se bloque sur CMPA (MOBILE).
//
// CONVENTION INVERSE (valeur 1) : ZRO = AQ_CLEAR, CAU = AQ_SET.
// La conduction occupe CMPA -> PRD. Le MOSFET s'amorce sur CMPA (MOBILE)
// et se bloque au passage a zero (FIXE).
//
// POURQUOI. Le front qui rayonne est celui du BLOCAGE : c'est lui qui
// interrompt le courant d'inductance. En convention d'origine c'est le
// front mobile, donc l'instant du parasite se deplace avec le rapport
// cyclique -- aucune fenetre de blanking a decalage constant ne peut le
// suivre, et le dephasage des deux etages n'est separable a aucun point
// de fonctionnement. En convention inverse le blocage est ancre au
// passage a zero, et l'amorcage -- qui ETABLIT le courant au lieu de
// l'interrompre -- devient le front mobile. Il rayonne beaucoup moins.
//
// ETAT : etage 1 bascule le 23/08/2026, etage 2 laisse en convention
// d'origine comme temoin de comparaison sur la meme carte.
#define PWM_AQ_TAIL_STAGE1   1
#define PWM_AQ_TAIL_STAGE2   0

// ---- LE RENVERSEMENT DU CODAGE DE L'ETAT DE REPOS -------------------
//
// EN CONVENTION INVERSE, CMPA = 0 SIGNIFIE CONDUCTION PENDANT TOUTE LA
// PERIODE. L'etat de repos et l'etat de pleine conduction ECHANGENT leur
// codage. Un zero ecrit "par prudence" produirait exactement l'inverse de
// ce qu'il croit faire.
//
// PARADE STRUCTURELLE, a ne pas defaire : pwm_set_duty_q8() recoit
// toujours un DUTY, jamais un CMPA, et fait elle-meme la soustraction.
// Tous les appelants -- stage_reset(), enable_power_path(), l'ecretage a
// zero de regulate(), l'init de pwm.c -- gardent donc leur sens d'origine
// et n'ont PAS ete modifies. duty = 0 ne cesse jamais de vouloir dire
// repos, nulle part dans le code.
//
// L'inhibition reelle ne depend d'ailleurs pas du tout de CMPA : elle
// passe par AQCSFRC (en amont du Dead-Band) et par TZ_FORCE_HI (en aval).
// Les deux chemins de securite sont insensibles a cette convention.
//
// Valeur de CMPA correspondant au repos, pour les verifications au
// debogueur : 300 sur l'etage 1 (TBPRD = 299). Si on y lit 0, le
// renversement a ete manque quelque part.
#define PWM_CMPA_AT_REST(prd)   ((uint16_t)((prd) + 1U))

// GARDE-FOU DE COHERENCE. Place ici et non dans la section securite :
// PWM_AQ_TAIL_STAGE1 n'y est pas encore defini, et un #if sur un symbole
// inconnu le lirait comme 0 -- le garde-fou se declencherait a tort, ou
// pire, un garde-fou ecrit dans l'autre sens ne se declencherait jamais.
//
// La fenetre de blanking ne peut etre ancree que sur CTR = 0 ou CTR = PRD.
// Sans l'inversion de l'AQ, le blocage de l'etage 1 tombe sur CMPA, donc a
// un instant MOBILE : la fenetre masquerait un moment quelconque du cycle
// au lieu de la commutation, et le ferait EN SILENCE -- on aveuglerait la
// protection sans rien gagner.
#if SAFETY_BLANK_STAGE1 && !PWM_AQ_TAIL_STAGE1
#error "Blanking etage 1 sans inversion AQ : la fenetre serait ancree sur CTR=0 alors que le blocage tombe sur CMPA, donc a un instant mobile."
#endif

// =====================================================================
// FENETRE DE DEBOGAGE AU DEMARRAGE
// =====================================================================
//
// Duree pendant laquelle la carte tourne dans une boucle vide, SANS AUCUNE
// interruption, juste avant EINT.
//
// POURQUOI. Le C28x met DBGM a 1 a chaque entree d'interruption : le coeur
// est alors dans du code que le debogueur n'a pas le droit d'arreter. Or
// cette carte vit en interruption -- ADC toutes les 15 us, regulation
// toutes les 195 us, plus le timer 10 ms et l'UART. Une fois le firmware
// lance, la sonde ne trouve pratiquement jamais le coeur dans un etat
// debogable, et toute reconnexion a froid echoue sur :
//
//   Error -1133: Device blocked debug access because it is currently
//                executing non-debuggable code
//
// ---- CE QUI S'EST REELLEMENT PASSE LE 22/08/2026 --------------------
//
// A LIRE AVANT DE PERDRE DU TEMPS SUR CE MESSAGE. La cause n'etait PAS
// la charge d'interruptions, contrairement a ce que ce commentaire a
// d'abord affirme. Elle etait dans .theia/launch.json, ou l'IDE avait
// inscrit sur la configuration du dualboost, et sur elle seule :
//
//   <property id="AllowInterruptsWhenHalted"><curValue>1</curValue>
//
// C'est le MODE TEMPS REEL. Le projet temoin TMS-test-io, lui, n'a aucun
// launch.json : il prend les defauts, mode temps reel eteint, et se
// chargeait sans la moindre difficulte sur la MEME carte, avec la MEME
// sonde, a la MEME minute. Une ligne de configuration, rien d'autre.
//
// La boite de dialogue le disait pourtant mot pour mot -- "you may
// cancel, disable realtime mode, and then attempt to connect". L'option a
// ete cherchee dans les menus, dans Debugger Properties et dans le
// .ccxml : les trois endroits ou elle n'est pas. Correctif : supprimer ce
// bloc debuggerSettings du launch.json.
//
// Le Test Connection passait integralement pendant tout ce temps -- chaine
// de scan saine, IR 38 bits, six motifs d'integrite sans erreur. Quand le
// JTAG est bon et que la connexion echoue, regarder la CONFIGURATION avant
// de soupconner le silicium.
//
// La fenetre ci-dessous reste utile malgre tout : elle donne une marge
// franche a la sonde a chaque demarrage, quelle que soit la configuration.
//
// Pendant cette fenetre tout est deja configure a l'etat sur : sorties
// inhibees par AQCSFRC, duty a zero, Trip Zones armees, rien ne convertit.
// Le coeur ne fait qu'attendre. Le debogueur s'y installe sans effort et
// sans manipulation materielle.
//
// A 0, la temporisation disparait et on retrouve le comportement d'avant,
// y compris sa difficulte de reconnexion.
//
// LE CONTOURNEMENT MANUEL, si cette fenetre venait a etre retiree : tenir
// RST a la masse pendant la connexion, et le RELACHER avant le chargement.
// Le maintenir pendant l'ecriture donne "Error -1137: Device is held in
// reset", qui est le signe qu'on est connecte et qu'il faut lacher.
#define BOOT_DEBUG_WINDOW_MS    500UL

// Liaison UART SCI-A (voir docs/ESP32-UART.md) : 57600 8N1.
// LSPCLK = SYSCLKOUT/4 (LOSPCP laisse a sa valeur par defaut par InitSysCtrl).
#define UART_BAUD_RATE      57600UL
#define UART_LSPCLK_HZ      (CLK_SYSCLKOUT_HZ / 4UL)
// BRR = round(LSPCLK / (8 * baud)) - 1 (TRM SPRUI09A, registre SCIHBAUD:SCILBAUD)
#define UART_SCIBRR \
    ((uint16_t)(((UART_LSPCLK_HZ + 4UL * UART_BAUD_RATE) / (8UL * UART_BAUD_RATE)) - 1U))

#define UART_RX_RING_SIZE   64U
#define UART_LINE_MAX       200U   // limite cote ESP32 (g_lineBuf[200])

#endif
