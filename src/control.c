#include "control.h"
#include "pwm.h"
#include "adc.h"
#include "bsp_gpio.h"
#include "calib.h"

// =====================================================================
// Conversions volts <-> counts ADC bruts.
//
// Toute la boucle rapide travaille en COUNTS, jamais en volts : c'est ce
// qui permet de n'utiliser que des additions, comparaisons et decalages
// dans l'ISR. Les expressions ci-dessous sont evaluees a la compilation.
//
//   raw = V * 4096 / (VREF * gain_de_la_voie)
// =====================================================================
#define V1_RAW_PER_VOLT     (4096.0f / (ADC_VREF_V * MEAS_V1_GAIN_V_PER_V))
#define VOUT_RAW_PER_VOLT   (4096.0f / (ADC_VREF_V * MEAS_VOUT_GAIN_V_PER_V))

#define V1_VOLTS_TO_RAW(v)     ((int32_t)((v) * V1_RAW_PER_VOLT + 0.5f))
#define VOUT_VOLTS_TO_RAW(v)   ((int32_t)((v) * VOUT_RAW_PER_VOLT + 0.5f))

// La rampe avance de moins d'un count par pas : il faut donc des bits
// fractionnaires, sinon elle n'avancerait jamais. Q8 suffit largement.
#define RAMP_FRAC_BITS  8

#define V1_RAMP_STEP_Q8 \
    ((int32_t)(CTRL_RAMP_V1_V_PER_STEP * V1_RAW_PER_VOLT * 256.0f + 0.5f))
#define VOUT_RAMP_STEP_Q8 \
    ((int32_t)(CTRL_RAMP_VOUT_V_PER_STEP * VOUT_RAW_PER_VOLT * 256.0f + 0.5f))

#define V1_OV_TRIP_RAW     V1_VOLTS_TO_RAW(CTRL_V1_OV_TRIP_V)
#define VOUT_OV_TRIP_RAW   VOUT_VOLTS_TO_RAW(CTRL_VOUT_OV_TRIP_V)

#define V1_SETTLE_TOL_RAW    V1_VOLTS_TO_RAW(CTRL_SETTLE_TOL_V1_V)
#define VOUT_SETTLE_TOL_RAW  VOUT_VOLTS_TO_RAW(CTRL_SETTLE_TOL_VOUT_V)

// Indices internes des deux etages.
#define S1  0U
#define S2  1U

static const stage_id_t k_stages[2] = {STAGE_1, STAGE_2};

// ---- Etat --------------------------------------------------------------
static volatile ctrl_state_t s_state = CTRL_STATE_IDLE;
static volatile fault_code_t s_fault = FAULT_NONE;

// Demandes venues de la boucle principale. On ne modifie pas s_state
// directement depuis le contexte principal : les requetes sont consommees
// par control_tick(), ce qui evite toute transition a moitie appliquee.
static volatile bool s_run_requested = false;
static volatile bool s_trip_requested = false;

// Etage 2 actif ? A false (consigne de sortie nulle), la machine s'arrete a
// l'etage 1 etabli et l'etage 2 reste a duty 0. Ne change JAMAIS en marche :
// control_set_setpoints() refuse une bascule tant qu'on n'est pas a l'arret.
static volatile bool s_s2_enabled = false;

// Consignes cibles, en counts (ecrites par la boucle principale).
static volatile int32_t s_target_raw[2] = {0, 0};
// Consignes effectivement appliquees, rampees, en Q8.
static int32_t s_ramp_q8[2] = {0, 0};

static int32_t s_accum[2] = {0, 0};      // integrateur, en counts << CTRL_SHIFT
static int32_t s_accum_max[2] = {0, 0};
static uint16_t s_duty_max_counts[2] = {0, 0};
static uint16_t s_settle_count = 0;

static uint16_t s_decim = 0;

// ---- Utilitaires -------------------------------------------------------

static void stage_reset(uint16_t i)
{
    s_accum[i] = 0;
    pwm_set_duty_counts(k_stages[i], 0U);
}

static void both_off(void)
{
    stage_reset(S1);
    stage_reset(S2);
    s_settle_count = 0;
}

// Rampe la consigne appliquee vers la cible, d'un pas au plus. Ne fait
// qu'ajouter ou soustraire.
static void ramp_toward(uint16_t i, int32_t step_q8)
{
    int32_t target_q8 = s_target_raw[i] << RAMP_FRAC_BITS;

    if (s_ramp_q8[i] < target_q8)
    {
        s_ramp_q8[i] += step_q8;
        if (s_ramp_q8[i] > target_q8)
        {
            s_ramp_q8[i] = target_q8;
        }
    }
    else if (s_ramp_q8[i] > target_q8)
    {
        s_ramp_q8[i] -= step_q8;
        if (s_ramp_q8[i] < target_q8)
        {
            s_ramp_q8[i] = target_q8;
        }
    }
}

// Integrateur pur : accum += erreur, duty = accum >> CTRL_SHIFT.
// Aucune multiplication, aucune division. Renvoie l'erreur, dont l'appelant
// se sert pour juger de l'etablissement.
static int32_t regulate(uint16_t i, int32_t measured_raw)
{
    int32_t error = (s_ramp_q8[i] >> RAMP_FRAC_BITS) - measured_raw;
    int32_t duty;

    s_accum[i] += error;

    // Anti-emballement : l'accumulateur est borne aux memes limites que le
    // duty, donc il ne peut pas accumuler une avance qu'il faudrait ensuite
    // "derouler" avant que la sortie ne reagisse.
    if (s_accum[i] < 0)
    {
        s_accum[i] = 0;
    }
    else if (s_accum[i] > s_accum_max[i])
    {
        s_accum[i] = s_accum_max[i];
    }

    duty = s_accum[i] >> CTRL_SHIFT;
    pwm_set_duty_counts(k_stages[i], (uint16_t)duty);

    return error;
}

static bool settled(int32_t error, int32_t tol_raw)
{
    if (error < 0)
    {
        error = -error;
    }
    return (error <= tol_raw);
}

// ---- API ---------------------------------------------------------------

void control_init(void)
{
    uint16_t i;

    for (i = 0U; i < 2U; i++)
    {
        uint16_t period = pwm_get_period_counts(k_stages[i]);

        s_duty_max_counts[i] = (uint16_t)(CTRL_DUTY_MAX * (float)period);
        s_accum_max[i] = ((int32_t)s_duty_max_counts[i]) << CTRL_SHIFT;

        s_target_raw[i] = 0;
        s_ramp_q8[i] = 0;
        s_accum[i] = 0;
    }

    s_state = CTRL_STATE_IDLE;
    s_fault = FAULT_NONE;
    s_s2_enabled = false; // rien ne demarre l'etage 2 sans consigne explicite
    s_run_requested = false;
    s_trip_requested = false;
    s_settle_count = 0;
    s_decim = 0;

    both_off();
}

bool control_set_setpoints(float v1_set_v, float vout_set_v)
{
    // Consigne de sortie nulle = etage 2 desactive (CTRL_VOSET_DISABLED).
    bool s2_on = (vout_set_v > CTRL_VOSET_DISABLED);

    // Refus en bloc si l'une des deux est hors bornes : on ne sature pas et
    // on n'applique pas la moitie d'une commande.
    if (v1_set_v < CTRL_V1_SET_MIN_V || v1_set_v > CTRL_V1_SET_MAX_V)
    {
        return false;
    }
    if (vout_set_v < CTRL_VOSET_DISABLED)
    {
        return false; // negatif : ni une consigne, ni la desactivation
    }
    if (s2_on
        && (vout_set_v < CTRL_VOUT_SET_MIN_V || vout_set_v > CTRL_VOUT_SET_MAX_V))
    {
        return false;
    }

    // Activer ou desactiver l'etage 2 EN MARCHE est refuse. L'activer
    // ferait demarrer l'etage 2 avec un integrateur et une rampe hors
    // contexte, donc par un a-coup de rapport cyclique ; le desactiver
    // couperait la sortie sans passer par l'etat sur. Il faut repasser par
    // RUN=0, ce qui ramene la machine en IDLE et reinitialise les rampes.
    if ((s2_on != s_s2_enabled) && (s_state != CTRL_STATE_IDLE))
    {
        return false;
    }

    s_target_raw[S1] = V1_VOLTS_TO_RAW(v1_set_v);
    s_target_raw[S2] = s2_on ? VOUT_VOLTS_TO_RAW(vout_set_v) : 0;
    s_s2_enabled = s2_on;
    return true;
}

void control_set_run(bool run)
{
    s_run_requested = run;
}

void control_restart(void)
{
    // La course avec l'ISR est benigne et voulue : si elle s'intercale entre
    // ces deux lignes, elle traite l'etat IDLE, repart de la tension mesuree
    // avec un accumulateur nul, et le both_off() qui suit ne fait que remettre
    // a zero ce qui l'est deja. Aucun etat incoherent n'est atteignable, donc
    // pas besoin de masquer les interruptions.
    s_state = CTRL_STATE_IDLE;
    both_off();
}

void control_trip(void)
{
    s_trip_requested = true;
}

ctrl_state_t control_get_state(void)
{
    return s_state;
}

fault_code_t control_get_fault(void)
{
    return s_fault;
}

float control_get_v1_setpoint(void)
{
    return (float)s_target_raw[S1] / V1_RAW_PER_VOLT;
}

float control_get_vout_setpoint(void)
{
    return (float)s_target_raw[S2] / VOUT_RAW_PER_VOLT;
}

bool control_s2_enabled(void)
{
    return s_s2_enabled;
}

bool control_hv_allowed(void)
{
    // Etage 2 desactive : il n'y a pas de sortie HT a mettre sous tension,
    // meme une fois l'etage 1 etabli. HV_EN reste donc interdit.
    return (s_state == CTRL_STATE_RUN) && s_s2_enabled;
}

// ---- Boucle de conduite, appelee depuis l'ISR ADC ----------------------

void control_tick(void)
{
    int32_t v1_raw;
    int32_t vout_raw;
    int32_t error;

    v1_raw = (int32_t)adc_get_raw(ADC_CH_V1);
    vout_raw = (int32_t)adc_get_raw(ADC_CH_VOUT);

    // --- Protections : AVANT la decimation, a chaque sequence ADC --------
    // Deux comparaisons sur les valeurs brutes, seuils pre-calcules : le
    // cout est negligeable, la difference de latence ne l'est pas.
    //
    // Ces tests etaient auparavant places APRES la decimation, donc executes
    // seulement une fois sur CTRL_DECIM : jusqu'a 195 us d'aveuglement. A
    // vide, l'inductance continue de deverser son courant dans un
    // condensateur que plus rien ne decharge -- la tension depasse largement
    // le seuil avant qu'il ne soit seulement lu, et c'est le drain du MOSFET
    // qui encaisse le depassement. Un MOSFET a ete detruit ainsi, charge
    // oubliee. Ici la detection tombe a 15 us, soit une periode ADC.
    if (v1_raw > V1_OV_TRIP_RAW)
    {
        s_fault = FAULT_OVERVOLTAGE_V1;
        s_trip_requested = true;
    }
    else if (vout_raw > VOUT_OV_TRIP_RAW)
    {
        s_fault = FAULT_OVERVOLTAGE_VOUT;
        s_trip_requested = true;
    }

    // La coupure elle-meme n'attend pas non plus le pas de regulation.
    if (s_trip_requested)
    {
        s_trip_requested = false;
        s_state = CTRL_STATE_FAULT;
    }
    if (s_state == CTRL_STATE_FAULT)
    {
        both_off();
        return;
    }

    // Decimation : donne un pas de regulation a dt rigoureusement constant.
    s_decim++;
    if (s_decim < CTRL_DECIM)
    {
        return;
    }
    s_decim = 0U;

    // Une demande de trip venue de la boucle principale (control_trip())
    // peut s'etre presentee entre-temps.
    if (s_trip_requested)
    {
        s_trip_requested = false;
        s_state = CTRL_STATE_FAULT;
    }

    // Un defaut est VERROUILLE : seul control_init() en sort.
    if (s_state == CTRL_STATE_FAULT)
    {
        both_off();
        return;
    }

    // L'arret demande ramene toujours a l'etat de repos. Le duty est force a
    // zero a CHAQUE pas et non seulement sur la transition : au repos, rien
    // ne doit pouvoir laisser une valeur residuelle dans CMPA.
    if (!s_run_requested)
    {
        s_state = CTRL_STATE_IDLE;
        both_off();
        return;
    }

    switch (s_state)
    {
    case CTRL_STATE_IDLE:
        // Le depart de rampe est la tension MESUREE, pas zero : a 0 % de
        // duty un boost laisse deja passer Vin par L et la diode, la sortie
        // n'est donc jamais nulle.
        s_ramp_q8[S1] = v1_raw << RAMP_FRAC_BITS;
        s_ramp_q8[S2] = vout_raw << RAMP_FRAC_BITS;
        s_accum[S1] = 0;
        s_accum[S2] = 0;
        s_settle_count = 0;
        s_state = CTRL_STATE_START_S1;
        break;

    case CTRL_STATE_START_S1:
        ramp_toward(S1, V1_RAMP_STEP_Q8);
        error = regulate(S1, v1_raw);
        stage_reset(S2); // l'etage 2 reste a zero pendant la montee du 1

        if ((s_ramp_q8[S1] == (s_target_raw[S1] << RAMP_FRAC_BITS))
            && settled(error, V1_SETTLE_TOL_RAW))
        {
            s_settle_count++;
            if (s_settle_count >= CTRL_SETTLE_STEPS)
            {
                s_settle_count = 0;
                // Etage 2 desactive : l'etablissement de l'etage 1 EST le
                // regime etabli, on ne passe jamais par START_S2. Sans cela
                // la machine y resterait indefiniment, la sortie ne pouvant
                // pas atteindre sa consigne sans etage de puissance monte.
                s_state = s_s2_enabled ? CTRL_STATE_RUN_S1 : CTRL_STATE_RUN;
            }
        }
        else
        {
            s_settle_count = 0;
        }
        break;

    case CTRL_STATE_RUN_S1:
        // V_inter etablie : l'etage 2 peut demarrer avec une tension
        // d'entree connue, donc un rapport cyclique initial coherent.
        ramp_toward(S1, V1_RAMP_STEP_Q8);
        (void)regulate(S1, v1_raw);
        s_ramp_q8[S2] = vout_raw << RAMP_FRAC_BITS;
        s_accum[S2] = 0;
        s_state = CTRL_STATE_START_S2;
        break;

    case CTRL_STATE_START_S2:
        ramp_toward(S1, V1_RAMP_STEP_Q8);
        (void)regulate(S1, v1_raw);

        ramp_toward(S2, VOUT_RAMP_STEP_Q8);
        error = regulate(S2, vout_raw);

        if ((s_ramp_q8[S2] == (s_target_raw[S2] << RAMP_FRAC_BITS))
            && settled(error, VOUT_SETTLE_TOL_RAW))
        {
            s_settle_count++;
            if (s_settle_count >= CTRL_SETTLE_STEPS)
            {
                s_settle_count = 0;
                s_state = CTRL_STATE_RUN;
            }
        }
        else
        {
            s_settle_count = 0;
        }
        break;

    case CTRL_STATE_RUN:
    default:
        // Regime etabli. Les rampes restent actives : un changement de
        // consigne en marche est suivi progressivement, ce qui est
        // exactement le cas d'usage des essais par paliers.
        ramp_toward(S1, V1_RAMP_STEP_Q8);
        (void)regulate(S1, v1_raw);

        if (s_s2_enabled)
        {
            ramp_toward(S2, VOUT_RAMP_STEP_Q8);
            (void)regulate(S2, vout_raw);
        }
        else
        {
            // Duty force a zero a CHAQUE pas, pas seulement sur la
            // transition : rien ne doit pouvoir laisser une valeur
            // residuelle dans CMPA de l'etage 2.
            stage_reset(S2);
        }
        break;
    }
}
