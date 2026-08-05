#include "DSP28x_Project.h"
#include "adc.h"
#include "control.h"
#include "calib.h"

// Valeurs du champ CHSEL (TRM SPRUI09A) : groupe A = 0x0..0x7,
// groupe B = 0x8..0xF. L'ordre suit adc_channel_t / SOC0..SOC8.
static const uint16_t k_chsel[ADC_CH_COUNT] = {
    0x0, // ADCINA0 - VIN
    0x1, // ADCINA1 - IIN
    0x2, // ADCINA2 - I1 (shunt etage 1)
    0x3, // ADCINA3 - IOUT
    0x4, // ADCINA4 - I2 (shunt etage 2)
    0xA, // ADCINB2 - VOUT
    0xC, // ADCINB4 - V1
    0xE, // ADCINB6 - T1
    0xF  // ADCINB7 - T2
};

static volatile uint16_t s_raw[ADC_CH_COUNT];
static volatile uint32_t s_seq_count = 0;

// Note de mesure (instrumentation retiree) : la duree de cette ISR a ete
// mesuree au scope en basculant une broche a l'entree et a la sortie.
// Resultat apres correction des etats d'attente de la flash : 4,9 us pour
// une periode de 15 us, soit 33 % de charge CPU a 66,85 kHz.
// Ne PAS refaire cette mesure sur GPIO32 : c'est HV_EN, la commande de
// l'optocoupleur VOM1271. Utiliser une broche libre (GPIO4, 6 ou 7).

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
        soc->bit.TRIGSEL = 5U;             // ePWM1 SOCA
        soc->bit.ACQPS = ADC_ACQPS_CYCLES; // fenetre S/H
    }

    // ADCINT1 declenchee par la fin du dernier SOC de la sequence.
    AdcRegs.INTSEL1N2.bit.INT1SEL = (uint16_t)ADC_CH_COUNT - 1U;
    AdcRegs.INTSEL1N2.bit.INT1CONT = 0;
    AdcRegs.INTSEL1N2.bit.INT1E = 1;

    PieVectTable.ADCINT1 = &adc_int1_isr;
    EDIS;

    IER |= M_INT1;
    PieCtrlRegs.PIEIER1.bit.INTx1 = 1; // ADCINT1

    // Declenchement par ePWM1 : SOCA sur passage a zero du compteur.
    // SOCAPRD = 3 -> une sequence tous les 3 cycles PWM. A 200 kHz cela fait
    // ~66 kHz, largement suffisant pour la telemetrie et la surveillance,
    // sans saturer le CPU d'interruptions.
    EALLOW;
    EPwm1Regs.ETSEL.bit.SOCASEL = 1; // ET_CTR_ZERO
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

    for (i = 0U; i < (uint16_t)ADC_CH_COUNT; i++)
    {
        s_raw[i] = res[i];
    }

    s_seq_count++;

    // Point d'appel cadence de la commande (PROMPT §6 etape 8 : dt constant,
    // dans l'ISR ADC ou PWM). Une sequence ADC complete = une decision.
    // Doit rester tres court : ni flottant, ni division, ni attente.
    control_tick();

    AdcRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
