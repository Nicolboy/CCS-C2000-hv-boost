#include "DSP28x_Project.h"
#include "adc.h"
#include "control.h"
#include "calib.h"

// =====================================================================
// INSTRUMENTATION TEMPORAIRE -- A RETIRER APRES CARACTERISATION
// =====================================================================
//
// Marque la DUREE DE L'ISR ADC sur HV_EN (GPIO32, broche 31) :
//
//   front MONTANT   a l'entree de adc_int1_isr()
//   front DESCENDANT a sa sortie
//
// VERSION PRECEDENTE, ABANDONNEE : le front montant etait produit par une
// ISR ePWM sur CTRU=CMPB, pour marquer la fenetre de conversion. Elle ne
// mesurait que sa propre latence. Deux raisons, constatees au scope :
//
//  1. Le front montant tombait sur le BLOCAGE du MOSFET alors que CMPB est
//     arithmetiquement au milieu de la conduction (pwm.c). L'ecart etait la
//     latence d'entree en interruption -- aggravee par la priorite PIE,
//     ADCINT1 etant groupe 1 et EPWM1_INT groupe 3, donc le marqueur passe
//     toujours apres. A 5 us de periode de decoupage cette latence vaut une
//     fraction notable du temps de conduction : l'instrumentation ne pouvait
//     pas resoudre ce qu'on lui demandait.
//
//  2. Des impulsions raccourcies apparaissaient a intervalles irreguliers.
//     Le front descendant ne pouvant pas avancer (duree de sequence figee
//     par le materiel), c'est le front montant qui etait en retard : l'ISR
//     ePWM attendait derriere l'ISR ADC.
//
// L'instant d'echantillonnage n'a de toute facon pas besoin d'etre mesure :
// il est exact par construction. SOC a CMPB = (CMPA>>1) - 7, l'ADC
// echantillonne a la FIN de sa fenetre d'acquisition soit 7 counts plus
// tard, donc I1 -- premiere voie de la sequence -- tombe exactement a
// CMPA>>1, le milieu de la conduction.
//
// Ce qui reste a mesurer, et que cette version mesure directement, c'est la
// DUREE DE L'ISR. Elle dispose de 9,4 us avant de percuter la sequence
// suivante (15 us de periode moins 5,58 us de conversion). Une fois sur
// CTRL_DECIM, control_tick() franchit la decimation et execute la machine
// d'etat complete : si l'impulsion large depasse alors 9 us, l'ADC ecrit les
// resultats de la sequence N+1 pendant que l'ISR recopie encore ceux de la
// sequence N, et s_raw[] melange deux sequences.
//
// DANGER -- HV_EN commande l'optocoupleur VOM1271, seul organe qui isole
// la charge haute tension. Le faire commuter a 66 kHz laisse ce driver
// photovoltaique, dont le temps de reponse se compte en millisecondes,
// dans un etat indetermine et partiellement conducteur. Sans consequence
// tant que l'etage 2 n'est pas monte ; INACCEPTABLE des qu'une tension
// est presente en sortie.
//
// REMETTRE A 0 AVANT TOUT ESSAI EN TENSION.
//
// Le drapeau lui-meme est declare dans calib.h : bsp_gpio.c en a besoin
// pour neutraliser hv_enable_set() pendant la mesure. Sans cette
// neutralisation, la boucle principale -- qui tourne a quelques dizaines de
// kHz et repasse par enter_safe_state() a chaque tour tant que la marche
// n'est pas demandee -- vient couper l'impulsion EN PLEIN MILIEU de la
// conversion. Constate au scope : sequences tronquees a intervalles
// irreguliers. Les resultats de conversion restent valides, c'est
// uniquement le marqueur qui etait hache.

// Valeurs du champ CHSEL (TRM SPRUI09A) : groupe A = 0x0..0x7,
// groupe B = 0x8..0xF. L'ordre suit adc_channel_t / SOC0..SOC8.
static const uint16_t k_chsel[ADC_CH_COUNT] = {
    0x2, // ADCINA2 - I1   (shunt etage 1)  \ groupe critique, en tete de
    0xC, // ADCINB4 - V1                    | sequence et fenetre courte,
    0x0, // ADCINA0 - VIN                   | cf. adc.h
    0x1, // ADCINA1 - IIN                   /
    0x3, // ADCINA3 - IOUT
    0x4, // ADCINA4 - I2   (shunt etage 2)
    0xA, // ADCINB2 - VOUT
    0xE, // ADCINB6 - T1   (non tamponnee, fenetre longue)
    0xF  // ADCINB7 - T2   (non tamponnee, fenetre longue)
};

static volatile uint16_t s_raw[ADC_CH_COUNT];
static volatile uint32_t s_seq_count = 0;

// Note de mesure : la duree de cette ISR a deja ete relevee au scope, a
// 4,9 us pour une periode de 15 us, soit 33 % de charge CPU a 66,85 kHz.
// C'etait AVANT que control_tick() ne porte la machine d'etat et la
// regulation -- d'ou la reprise de la mesure, cf. ADC_TIMING_PROBE.
// Des que l'etage 2 sera peuple, deplacer le marqueur sur une broche libre
// (GPIO4, 6 ou 7) : GPIO32 est HV_EN, la commande de l'optocoupleur VOM1271.

interrupt void adc_int1_isr(void);

void adc_init(void)
{
    uint16_t i;

    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1;

    // Sequence de mise sous tension du bloc analogique (f2802x_adc.c).
    AdcRegs.ADCCTL1.bit.ADCBGPWD = 1;
    AdcRegs.ADCCTL1.bit.ADCREFPWD = 1;
    AdcRegs.ADCCTL1.bit.ADCPWDN = 1;
    AdcRegs.ADCCTL1.bit.ADCENABLE = 1;
    AdcRegs.ADCCTL1.bit.ADCREFSEL = 0; // reference interne, pleine echelle 3,3 V
    EDIS;

    DELAY_US(1000L); // stabilisation de la reference avant toute conversion

    EALLOW;
    // Impulsion d'interruption en fin de conversion, quand le resultat est
    // effectivement dans ADCRESULTx (et non en debut de conversion).
    AdcRegs.ADCCTL1.bit.INTPULSEPOS = 1;

    // ADCCLK = SYSCLKOUT/2 = 30 MHz, et non-overlap : le mode overlap
    // raccourcit la fenetre d'echantillonnage, inutile ici.
    AdcRegs.ADCCTL2.bit.CLKDIV2EN = 1;
    AdcRegs.ADCCTL2.bit.ADCNONOVERLAP = 1;

    // Les ADCSOCxCTL sont des registres 16 bits contigus : on les parcourt
    // par indexation directe de l'union.
    for (i = 0U; i < (uint16_t)ADC_CH_COUNT; i++)
    {
        volatile union ADCSOCxCTL_REG *soc = (&AdcRegs.ADCSOC0CTL) + i;

        soc->bit.CHSEL = k_chsel[i];
        soc->bit.TRIGSEL = 5U; // ePWM1 SOCA

        // Fenetre courte sur les voies tamponnees, longue sur les deux
        // thermistances qui n'ont pas de suiveur. Chaque SOC porte son
        // propre ACQPS, il n'y a donc rien a sacrifier a l'autre.
        soc->bit.ACQPS = (i < (uint16_t)(ADC_CH_COUNT - 2U))
                             ? (uint16_t)ADC_ACQPS_FAST
                             : (uint16_t)ADC_ACQPS_SLOW;
    }

    // Duree de la sequence : 7 voies rapides a 550 ns et 2 lentes a 866 ns,
    // soit 5,58 us. Elle tient donc dans une periode de decoupage, la ou les
    // 7,8 us d'avant en debordaient largement.
    //
    // ADCINT1 declenchee par la fin du dernier SOC de la sequence.
    AdcRegs.INTSEL1N2.bit.INT1SEL = (uint16_t)ADC_CH_COUNT - 1U;
    AdcRegs.INTSEL1N2.bit.INT1CONT = 0;
    AdcRegs.INTSEL1N2.bit.INT1E = 1;

    PieVectTable.ADCINT1 = &adc_int1_isr;
    EDIS;

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1; // ADCINT1

    // Declenchement par ePWM1 sur CTRU=CMPB, et non plus sur le passage a
    // zero. CMPB est place par pwm.c au milieu de la conduction du MOSFET,
    // decale en amont de la fenetre d'acquisition.
    //
    // Le declenchement a zero avait deux defauts : il faisait echantillonner
    // VIN exactement sur le front de mise en conduction, a chaque sequence et
    // quel que soit le duty ; et l'instant de blocage, qui se deplace avec le
    // duty, venait tomber dans la fenetre d'une voie ou d'une autre. Le milieu
    // de la conduction est a l'inverse le point le plus eloigne des DEUX
    // fronts, donc le plus calme du cycle.
    //
    // SOCAPRD = 3 -> une sequence tous les 3 cycles PWM. A 200 kHz cela fait
    // ~66 kHz, largement suffisant pour la telemetrie et la surveillance,
    // sans saturer le CPU d'interruptions. La sequence complete dure 7,8 us
    // (9 voies x 866 ns), elle tient donc dans les 15 us disponibles.
    EALLOW;
    EPwm1Regs.ETSEL.bit.SOCASEL = ET_CTRU_CMPB;
    EPwm1Regs.ETPS.bit.SOCAPRD = 3;
    EPwm1Regs.ETSEL.bit.SOCAEN = 1;
    EDIS;
}

uint16_t adc_get_raw(adc_channel_t ch)
{
    if (ch >= ADC_CH_COUNT)
    {
        return 0U;
    }
    return s_raw[ch];
}

float adc_get_volts(adc_channel_t ch)
{
    if (ch >= ADC_CH_COUNT)
    {
        return 0.0f;
    }
    return (float)s_raw[ch] * ADC_VREF_V / 4096.0f;
}

bool adc_any_below(float volts)
{
    uint16_t threshold = (uint16_t)(volts / ADC_VREF_V * 4096.0f + 0.5f);
    uint16_t i;

    for (i = 0U; i < (uint16_t)ADC_CH_COUNT; i++)
    {
        if (s_raw[i] < threshold)
        {
            return true;
        }
    }
    return false;
}

uint32_t adc_get_sequence_count(void)
{
    return s_seq_count;
}

// ISR courte : rangement des valeurs brutes, rien d'autre. Aucune conversion
// physique, aucun flottant (PROMPT §7). La protection rapide ne passe PAS
// par l'ADC, c'est le comparateur qui s'en charge.
interrupt void adc_int1_isr(void)
{
    // Les resultats sont dans un bloc distinct (AdcResult), a une autre
    // adresse que les registres de controle : ADCRESULT0..15 contigus.
    volatile uint16_t *res = &AdcResult.ADCRESULT0;
    uint16_t i;

#if ADC_TIMING_PROBE
    // TOUT PREMIER acte de l'ISR, et le dernier avant l'acquittement : la
    // largeur de l'impulsion EST la duree de l'ISR, latence d'entree exclue.
    // Une impulsion large une fois sur CTRL_DECIM est attendue (pas de
    // regulation) ; au-dela de 9 us elle deborde sur la sequence suivante.
    GpioDataRegs.GPBSET.bit.GPIO32 = 1;
#endif

    for (i = 0U; i < (uint16_t)ADC_CH_COUNT; i++)
    {
        s_raw[i] = res[i];
    }

    s_seq_count++;

    // Surveillance de survoltage SEULE : deux comparaisons sur des valeurs
    // brutes. La regulation, elle, vit sur le CPU Timer 1 -- elle debordait
    // ici une sequence sur treize (cf. control.c et l'en-tete ci-dessus).
    control_fast_check();

#if ADC_TIMING_PROBE
    GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;
#endif

    AdcRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
