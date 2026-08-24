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
#define VIN_RAW_PER_VOLT    (4096.0f / (ADC_VREF_V * MEAS_VIN_GAIN_V_PER_V))

#define V1_VOLTS_TO_RAW(v)     ((int32_t)((v) * V1_RAW_PER_VOLT + 0.5f))
#define VIN_VOLTS_TO_RAW(v)    ((int32_t)((v) * VIN_RAW_PER_VOLT + 0.5f))

// VOUT porte un OFFSET (chute de LED1 en serie dans le pont, cf. calib.h),
// d'ou DEUX conversions qu'il ne faut jamais confondre :
//
//   _VOLTS_TO_RAW  -> une TENSION ABSOLUE. L'offset se soustrait, puisque
//                     le pont ne voit que (V_HT - Vf).
//   _DELTA_TO_RAW  -> un ECART entre deux tensions. L'offset s'annule dans
//                     la difference et ne doit SURTOUT PAS etre soustrait.
//
// Utiliser la premiere pour une tolerance donnerait ici -1,7 V converti en
// counts, soit un critere d'etablissement negatif : l'etage 2 ne se
// declarerait jamais etabli et la machine resterait bloquee en START_S2.
#define VOUT_VOLTS_TO_RAW(v)                                              \
    ((int32_t)(((v) - MEAS_VOUT_OFFSET_V) * VOUT_RAW_PER_VOLT + 0.5f))
#define VOUT_DELTA_TO_RAW(v)   ((int32_t)((v) * VOUT_RAW_PER_VOLT + 0.5f))

// La rampe avance de moins d'un count par pas : il faut donc des bits
// fractionnaires, sinon elle n'avancerait jamais. Q8 suffit largement.
#define RAMP_FRAC_BITS  8

#define V1_RAMP_STEP_Q8 \
    ((int32_t)(CTRL_RAMP_V1_V_PER_STEP * V1_RAW_PER_VOLT * 256.0f + 0.5f))
#define VOUT_RAMP_STEP_Q8 \
    ((int32_t)(CTRL_RAMP_VOUT_V_PER_STEP * VOUT_RAW_PER_VOLT * 256.0f + 0.5f))

#define V1_OV_TRIP_RAW     V1_VOLTS_TO_RAW(CTRL_V1_OV_TRIP_V)
#define VOUT_OV_TRIP_RAW   VOUT_VOLTS_TO_RAW(CTRL_VOUT_OV_TRIP_V)
#define VIN_UV_TRIP_RAW    VIN_VOLTS_TO_RAW(CTRL_VIN_UV_TRIP_V)
#define VIN_UV_ARM_RAW     VIN_VOLTS_TO_RAW(CTRL_VIN_UV_ARM_V)

#define V1_SETTLE_TOL_RAW    V1_VOLTS_TO_RAW(CTRL_SETTLE_TOL_V1_V)
#define VOUT_SETTLE_TOL_RAW  VOUT_DELTA_TO_RAW(CTRL_SETTLE_TOL_VOUT_V)

// GARDE-FOU DE COMPILATION. DACVAL sur 10 bits avait deja produit ce mode de
// panne (cf. SAFETY_DAC_CODE_FROM_V) : un code hors plage ne sature pas, il
// desarme la protection en silence. Ici, un seuil converti au-dela de 4095
// counts ne serait JAMAIS atteint, l'ADC saturant a 4095 -- la coupure de
// survoltage VOUT ne se declencherait plus sur aucune tension.
//
// C'est arrive : avec le gain errone de 181,82 et un pont reel a 121, le
// seuil de "520 V" tombait a 347 V reels ; corriger le seul gain sans ce
// garde-fou l'aurait pousse a 5322 counts, donc hors d'atteinte.
typedef char vout_ov_trip_fits_in_adc[(VOUT_OV_TRIP_RAW <= 4095) ? 1 : -1];

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

// Extinction synchrone des DEUX etages, demandee par la boucle principale
// le temps de sequencer la coupure de HT. Voir control_coast().
static volatile bool s_coast = false;
// Dernier duty commande a chaque etage, en Q8 counts. Point de depart de
// l'extinction progressive.
static int32_t s_coast_duty_q8[2] = {0, 0};
// Plancher de l'extinction, en Q8 counts, calcule a l'init depuis la
// periode de chaque etage.
static int32_t s_coast_floor_q8[2] = {0, 0};

// Sequences ADC consecutives au-dessus des seuils de surtension. Voir
// CTRL_VOUT_OV_COUNTS et CTRL_V1_OV_COUNTS dans calib.h.
static uint16_t s_vout_ov_count = 0U;
static uint16_t s_v1_ov_count = 0U;

// Etage 2 actif ? A false (consigne de sortie nulle), la machine s'arrete a
// l'etage 1 etabli et l'etage 2 reste a duty 0. Ne change JAMAIS en marche :
// control_set_setpoints() refuse une bascule tant qu'on n'est pas a l'arret.
static volatile bool s_s2_enabled = false;

// Consignes cibles, en counts (ecrites par la boucle principale).
static volatile int32_t s_target_raw[2] = {0, 0};
// Consignes effectivement appliquees, rampees, en Q8.
static int32_t s_ramp_q8[2] = {0, 0};

static int32_t s_accum[2] = {0, 0};      // integrateur, en counts << CTRL_KI_SHIFT
static int32_t s_accum_max[2] = {0, 0};
#if CTRL_KD_ENABLE
// Erreur du pas precedent, pour le terme derive. Compilee avec lui : la
// garder inconditionnellement produisait un avertissement "set but never
// used", et un build silencieux vaut mieux qu'une variable de confort.
static int32_t s_prev_error[2] = {0, 0};
#endif
static uint16_t s_duty_max_counts[2] = {0, 0};
static uint16_t s_settle_count = 0;

// Surveillance de sous-tension d'entree. s_vin_armed passe a true la premiere
// fois que VIN franchit CTRL_VIN_UV_ARM_V et n'en redescend jamais : sans
// cela, la montee de l'alimentation au demarrage verrouillerait un defaut.
static bool s_vin_armed = false;
static uint16_t s_vin_uv_count = 0;

#if CTRL_VIN_UV_REARM
// Pas de regulation consecutifs passes au-dessus du seuil d'armement, defaut
// de sous-tension verrouille. Remis a zero des que VIN repasse dessous.
static uint16_t s_vin_rearm_count = 0U;
// Tentatives deja consommees. Remis a zero seulement par un etablissement
// reel (entree en CTRL_STATE_RUN) ou par control_init().
static uint16_t s_vin_retries = 0U;
#endif

#if STAGE2_OPENLOOP_TEST
// Duty fixe de l'essai, converti en counts UNE SEULE FOIS : la periode ePWM
// n'est connue qu'apres pwm_init(), et une multiplication flottante n'a rien
// a faire dans l'ISR de regulation.
static uint16_t s_openloop_counts = 0U;
#endif

// ---- Utilitaires -------------------------------------------------------

static void stage_reset(uint16_t i)
{
    s_accum[i] = 0;
#if CTRL_KD_ENABLE
    s_prev_error[i] = 0;
#endif
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

// Decalage a GAUCHE d'une valeur signee. `v << n` sur un negatif est un
// comportement indefini en C : on decale la valeur absolue et on rend le
// signe. Le compilateur en fait le meme code qu'un decalage nu, mais celui-ci
// est defini par la norme.
static int32_t shl_signed(int32_t v, uint16_t n)
{
    if (v < 0)
    {
        return -((-v) << n);
    }
    return (v << n);
}

// Decalage a DROITE d'une valeur signee. `v >> n` sur un negatif n'est pas
// indefini mais reste DEFINI PAR L'IMPLEMENTATION : la norme laisse le
// choix entre decalage arithmetique et logique. On decale la valeur absolue
// et on rend le signe, ce qui tronque vers zero de facon SYMETRIQUE.
//
// La symetrie compte ici : appliquee au terme proportionnel, une troncature
// asymetrique introduirait un biais systematique dont le signe dependrait
// de celui de l'erreur -- l'integrateur le compenserait en permanence, avec
// une erreur statique residuelle a la clef.
static int32_t shr_signed(int32_t v, uint16_t n)
{
    if (v < 0)
    {
        return -((-v) >> n);
    }
    return (v >> n);
}

// ---- PID, en virgule fixe ----------------------------------------------
//
//   duty_counts = P + I + D
//
// Les trois termes sont calcules SEPAREMENT et gardes dans des variables
// distinctes, meme quand la somme seule suffirait : un releve au debogueur
// dit alors immediatement lequel agit, ce qu'une expression unique ne
// permettrait pas. Le compilateur les elimine s'ils sont inutiles.
//
// Gains : voir calib.h. Kp et Kd decalent a GAUCHE, Ki a DROITE.
// Le terme derive est compile hors de la boucle tant que CTRL_KD_ENABLE
// vaut 0 -- il ne coute alors pas un cycle.
//
// ---- UNITE DE SORTIE : Q8 COUNTS (depuis le 22/08/2026, HRPWM) --------
//
// Le rapport cyclique sort maintenant avec PWM_DUTY_FRAC_BITS bits de
// partie fractionnaire, que pwm.c porte dans CMPAHR. Sans cette partie
// fractionnaire, un count de l'etage 2 vaut 8,3 V de sortie et la boucle ne
// peut que battre entre deux valeurs entieres (voir calib.h).
//
// LES GAINS N'ONT PAS CHANGE DE SENS. CTRL_KP_SHIFT et CTRL_KI_SHIFT
// gardent exactement la definition sous laquelle ils ont ete regles au banc
// le 20/08 -- un gain exprime en COUNTS ENTIERS par count d'erreur. Le
// passage en Q8 est fait ici, au dernier moment, par le seul decalage
// supplementaire de PWM_DUTY_FRAC_BITS. Il n'y a donc rien a re-regler.
//
// Consequence a noter sur l'integrateur : i_term vaut accum >> (KI_SHIFT -
// FRAC) au lieu de accum >> KI_SHIFT, et sa borne doit valoir duty_max_q8 =
// duty_max << FRAC ; ces deux decalages-la se compensent exactement.
// k_gain_div_shift, lui, ne se compense PAS et doit figurer dans la borne --
// voir control_init(), c'est l'omission qui a fausse la boucle jusqu'au 24/08.
//
// Renvoie l'erreur, dont l'appelant se sert pour juger de l'etablissement.
#if (CTRL_KI_SHIFT <= PWM_DUTY_FRAC_BITS)
#error "CTRL_KI_SHIFT doit rester superieur a PWM_DUTY_FRAC_BITS : le decalage du terme integral deviendrait negatif."
#endif

// Division supplementaire du gain, PAR ETAGE. Indexee par le meme i que
// k_stages[], s_accum[] et s_duty_max_counts[] : etage 1 en 0, etage 2 en 1.
// const, donc le compilateur la place en flash et l'indexation ne coute
// qu'un acces -- acceptable dans l'ISR de regulation.
static const uint16_t k_gain_div_shift[2] = {
    CTRL_GAIN_DIV_SHIFT_S1,
    CTRL_GAIN_DIV_SHIFT_S2
};
static int32_t regulate(uint16_t i, int32_t measured_raw)
{
    int32_t error = (s_ramp_q8[i] >> RAMP_FRAC_BITS) - measured_raw;
    int32_t duty_max = ((int32_t)s_duty_max_counts[i]) << PWM_DUTY_FRAC_BITS;
    int32_t accum_prev = s_accum[i];
    int32_t p_term;
    int32_t i_term;
    int32_t d_term;
    int32_t duty;

    // ---- P : proportionnel ---------------------------------------------
    // Agit dans le pas MEME ou l'ecart apparait. C'est lui, et lui seul, qui
    // peut repondre a un delestage : l'integrateur, par construction, a
    // besoin de plusieurs pas pour batir sa correction.
    // Kp effectif = 2^CTRL_KP_SHIFT / 2^CTRL_KP_DIV_SHIFT. Le decalage a
    // gauche ne pouvant pas descendre sous 0, la division en est separee.
    // L'ordre compte : on decale a gauche D'ABORD, donc la troncature de la
    // division porte sur une valeur deja mise a l'echelle Q8 et ne coute
    // pas de resolution sur l'erreur elle-meme.
    //
    // k_gain_div_shift[i] ajoute la division PROPRE A L'ETAGE. Elle est
    // appliquee identiquement a P et a I plus bas, ce qui deplace le gain
    // de boucle sans deplacer le zero du PI. Voir calib.h.
    p_term = shr_signed(shl_signed(error, CTRL_KP_SHIFT + PWM_DUTY_FRAC_BITS),
                        CTRL_KP_DIV_SHIFT + k_gain_div_shift[i]);

    // ---- I : integral ---------------------------------------------------
    // Supprime l'erreur statique. L'accumulateur est borne aux memes limites
    // que le duty : il ne peut pas accumuler une avance qu'il faudrait
    // ensuite "derouler" avant que la sortie ne reagisse.
    s_accum[i] += error;
    if (s_accum[i] < 0)
    {
        s_accum[i] = 0;
    }
    else if (s_accum[i] > s_accum_max[i])
    {
        s_accum[i] = s_accum_max[i];
    }
    i_term = s_accum[i]
             >> (CTRL_KI_SHIFT - PWM_DUTY_FRAC_BITS + k_gain_div_shift[i]);

    // ---- D : derive ------------------------------------------------------
#if CTRL_KD_ENABLE
    d_term = shl_signed(error - s_prev_error[i], CTRL_KD_SHIFT + PWM_DUTY_FRAC_BITS);
    s_prev_error[i] = error;
#else
    d_term = 0;
#endif

    // ---- Somme et saturation ---------------------------------------------
    duty = p_term + i_term + d_term;

    // Anti-emballement, second etage. Le bornage de l'accumulateur ci-dessus
    // ne suffit plus depuis que P existe : la SOMME peut saturer alors que
    // l'accumulateur est en pleine plage. Integrer dans ce cas ne ferait que
    // gonfler une reserve sans effet sur la sortie, qu'il faudrait ensuite
    // depenser avant que la commande ne redescende -- c'est le mecanisme
    // classique du depassement au demarrage. On annule donc l'integration de
    // ce pas, sans toucher a P ni a D, qui restent legitimes.
    if (duty > duty_max || duty < 0)
    {
        s_accum[i] = accum_prev;
        // k_gain_div_shift[i] est INDISPENSABLE ici : sans lui la relecture
        // ne coincide pas avec celle du calcul normal plus haut, et i_term
        // ressort DOUBLE sur l'etage 1 -- dans la branche meme ou la boucle
        // passe son temps quand elle sature. Meme omission que celle qui
        // avait fausse s_accum_max, corrigee le 24/08.
        i_term = accum_prev >> (CTRL_KI_SHIFT - PWM_DUTY_FRAC_BITS
                                + k_gain_div_shift[i]);
        duty = p_term + i_term + d_term;

        if (duty > duty_max)
        {
            duty = duty_max;
        }
        else if (duty < 0)
        {
            duty = 0;
        }
    }

    // ---- Limitation de VITESSE du rapport cyclique -----------------------
    // Le duty peut aller ou il veut, mais pas d'un bond. Voir
    // CTRL_DUTY_SLEW_MAX_COUNTS dans calib.h pour le dimensionnement et pour
    // la raison qui interdit d'employer un simple plafond a la place.
    //
    // s_coast_duty_q8[i] sert de valeur precedente : il est deja ecrit plus
    // bas a chaque pas de regulation ET tenu a jour par coast_stage(), donc
    // il ne peut pas se desynchroniser -- y compris a la sortie d'une
    // extinction sequencee, ou repartir d'une valeur perimee produirait
    // exactement le bond qu'on cherche a interdire.
    {
        int32_t prev = s_coast_duty_q8[i];
        int32_t slew = ((int32_t)CTRL_DUTY_SLEW_MAX_COUNTS)
                       << PWM_DUTY_FRAC_BITS;

        if ((duty - prev) > slew)
        {
            duty = prev + slew;
            s_accum[i] = accum_prev;
        }
        else if ((prev - duty) > slew)
        {
            duty = prev - slew;
            s_accum[i] = accum_prev;
        }
    }

    // Memorise pour que l'extinction progressive sache d'ou partir : sans
    // ca il faudrait relire CMPA, donc convertir en sens inverse et
    // dependre de la convention AQ de l'etage.
    s_coast_duty_q8[i] = duty;

    pwm_set_duty_q8(k_stages[i], (uint32_t)duty);

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

        // ---- BORNE DE L'INTEGRATEUR ------------------------------------
        // i_term se lit accum >> (CTRL_KI_SHIFT - PWM_DUTY_FRAC_BITS +
        // k_gain_div_shift[i]) et doit plafonner a duty_max_q8, c'est-a-dire
        // duty_max_counts << PWM_DUTY_FRAC_BITS. En composant les deux, la
        // borne vaut donc duty_max_counts << (KI_SHIFT + k_gain_div_shift).
        //
        // HISTORIQUE, parce que ce decalage a coute cher. k_gain_div_shift
        // avait ete ajoute au terme integral sans l'etre ici : sur l'etage 1
        // (k_gain_div_shift = 1) i_term plafonnait a la MOITIE de la plage,
        // et la boucle degenerait en proportionnel pur des qu'elle etait
        // chargee. Signature mesuree le 24/08 : consigne 75 V, 75 V affiches
        // a vide et 69 V en charge. Une erreur statique qui SUIT LA CHARGE
        // accuse toujours l'integrateur, jamais le gain proportionnel.
        //
        // Le bug agissait comme un LIMITEUR DE COURANT involontaire : il
        // interdisait a l'etage 1 d'atteindre CTRL_DUTY_MAX = 0,95. Une
        // premiere tentative de correction, seule, avait donc produit overI1
        // a la fermeture de HV_EN -- elle decouvrait une fragilite
        // preexistante au lieu de la creer.
        //
        // Elle n'est levee maintenant que parce que les DEUX parades sont en
        // place : CTRL_DUTY_SLEW_MAX_COUNTS borne la vitesse de montee du
        // duty, et HV_ON_COAST_SEQ supprime l'echelon de charge au lieu de le
        // faire encaisser a la boucle. Ne pas retirer l'une sans l'autre.
        s_accum_max[i] = ((int32_t)s_duty_max_counts[i])
                         << (CTRL_KI_SHIFT + k_gain_div_shift[i]);

        // Plancher de l'extinction sequencee, en Q8 counts. Calcule ici et
        // pas dans coast_stage() : la periode differe d'un etage a l'autre
        // (300 et 600 counts), et une division n'a rien a faire dans l'ISR.
        s_coast_floor_q8[i] =
            (int32_t)(CTRL_COAST_FLOOR_PCT * 0.01f * (float)period) << PWM_DUTY_FRAC_BITS;
        s_coast_duty_q8[i] = 0;

        s_target_raw[i] = 0;
        s_ramp_q8[i] = 0;
        s_accum[i] = 0;
#if CTRL_KD_ENABLE
        s_prev_error[i] = 0;
#endif
    }

#if STAGE2_OPENLOOP_TEST
    s_openloop_counts = (uint16_t)(((uint32_t)pwm_get_period_counts(STAGE_2)
                                    * STAGE2_OPENLOOP_DUTY_PCT) / 100UL);
#endif

    s_state = CTRL_STATE_IDLE;
    s_fault = FAULT_NONE;
    s_s2_enabled = false; // rien ne demarre l'etage 2 sans consigne explicite
    s_run_requested = false;
    s_trip_requested = false;
    s_settle_count = 0;
    s_vin_armed = false;
    s_vin_uv_count = 0U;
#if CTRL_VIN_UV_REARM
    s_vin_rearm_count = 0U;
    s_vin_retries = 0U;
#endif

    both_off();
}

bool control_set_setpoints(float v1_set_v, float vout_set_v)
{
    // Consigne de sortie nulle = etage 2 desactive (CTRL_VOSET_DISABLED).
    bool s2_on = (vout_set_v > CTRL_VOSET_DISABLED);

#if STAGE2_OPENLOOP_TEST
    // Essai en boucle ouverte : l'etage 2 sort deja un duty fixe. Accepter en
    // plus une consigne de sortie ferait cohabiter regulation et boucle
    // ouverte sur le meme etage, sans qu'aucune trace ne dise laquelle pilote.
    // Le refus est compte et remonte en telemetrie (tag REJ), donc visible.
    if (s2_on)
    {
        return false;
    }
#endif

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
    // Symetrique de VOUT_VOLTS_TO_RAW : l'offset a ete soustrait a l'aller,
    // il se rajoute au retour. Sans ca la telemetrie VOSP renverrait 1,7 V de
    // moins que la consigne envoyee, et l'ESP32 croirait la commande refusee.
    if (s_target_raw[S2] == 0)
    {
        return 0.0f; // etage 2 desactive : la convention est zero exact
    }
    return (float)s_target_raw[S2] / VOUT_RAW_PER_VOLT + MEAS_VOUT_OFFSET_V;
}

bool control_s2_enabled(void)
{
    return s_s2_enabled;
}

bool control_hv_allowed(void)
{
#if STAGE2_OPENLOOP_TEST
    // Essai : l'etage 2 n'est pas regule, exiger s_s2_enabled interdirait
    // HV_EN pour toujours -- or le piloter est justement un des objets de
    // l'essai. Le regime etabli de l'etage 1 reste exige, et la commande HT
    // de l'operateur aussi (main.c) : l'essai rend HV_EN possible, pas
    // automatique.
    return (s_state == CTRL_STATE_RUN);
#else
    // Etage 2 desactive : il n'y a pas de sortie HT a mettre sous tension,
    // meme une fois l'etage 1 etabli. HV_EN reste donc interdit.
    return (s_state == CTRL_STATE_RUN) && s_s2_enabled;
#endif
}

// ---- Surveillance rapide, appelee depuis l'ISR ADC ---------------------
//
// Deux comparaisons sur les valeurs brutes, seuils pre-calcules a la
// compilation : le cout est negligeable, la difference de latence ne l'est
// pas.
//
// Ces tests ont d'abord vecu APRES la decimation du pas de regulation, donc
// executes une fois sur treize : jusqu'a 195 us d'aveuglement. A vide,
// l'inductance continue de deverser son courant dans un condensateur que
// plus rien ne decharge -- la tension depasse largement le seuil avant qu'il
// ne soit seulement lu, et c'est le drain du MOSFET qui encaisse le
// depassement. Un MOSFET a ete detruit ainsi, charge oubliee.
//
// Ils sont maintenant dans leur propre fonction, appelee a CHAQUE sequence
// ADC : la detection tombe a 15 us et, surtout, elle ne depend plus du
// contexte ou tourne la regulation.
void control_fast_check(void)
{
    int32_t v1_raw = (int32_t)adc_get_raw(ADC_CH_V1);
    int32_t vout_raw = (int32_t)adc_get_raw(ADC_CH_VOUT);
    int32_t vin_raw = (int32_t)adc_get_raw(ADC_CH_VIN);

    // Armement de la surveillance de sous-tension : une seule fois, quand
    // l'alimentation d'entree est franchement etablie. Voir calib.h.
    if (!s_vin_armed)
    {
        if (vin_raw > VIN_UV_ARM_RAW)
        {
            s_vin_armed = true;
        }
    }
    else if (vin_raw < VIN_UV_TRIP_RAW)
    {
        s_vin_uv_count++;
    }
    else
    {
        s_vin_uv_count = 0U;
    }

    // Anti-rebond de la surtension de SORTIE. Voir calib.h : le defaut
    // tombait a chaque arret normal, sur le transitoire de HV_EN et de la
    // decharge, alors que la tension reelle ne bougeait pas.
    //
    // Le comptage est fait ICI, inconditionnellement, et non dans la
    // cascade de tests plus bas : place dans le "else if", il ne serait
    // remis a zero que si aucun autre defaut n'etait vu avant, et un
    // depassement de V1 masquerait l'historique de VOUT.
    if (vout_raw > VOUT_OV_TRIP_RAW)
    {
        s_vout_ov_count++;
    }
    else
    {
        s_vout_ov_count = 0U;
    }

    if (v1_raw > V1_OV_TRIP_RAW)
    {
        s_v1_ov_count++;
    }
    else
    {
        s_v1_ov_count = 0U;
    }

    if (s_v1_ov_count >= CTRL_V1_OV_COUNTS)
    {
        s_fault = FAULT_OVERVOLTAGE_V1;
        s_trip_requested = true;
    }
    else if (s_vout_ov_count >= CTRL_VOUT_OV_COUNTS)
    {
        s_fault = FAULT_OVERVOLTAGE_VOUT;
        s_trip_requested = true;
    }
    else if (s_vin_uv_count >= CTRL_VIN_UV_COUNTS)
    {
        // Sous-tension d'entree : le convertisseur elevateur compenserait en
        // augmentant le rapport cyclique, donc le courant, jusqu'a la
        // surintensite. On coupe avant.
        s_fault = FAULT_UNDERVOLTAGE_VIN;
        s_trip_requested = true;
    }

    // La coupure n'attend pas le pas de regulation : elle est appliquee ici,
    // dans la meme sequence que la detection.
    if (s_trip_requested)
    {
        s_trip_requested = false;
        s_state = CTRL_STATE_FAULT;
    }
    if (s_state == CTRL_STATE_FAULT)
    {
        both_off();
    }
}

// ---- Pas de regulation, appele depuis l'ISR du CPU Timer 1 -------------
//
// N'etait qu'une branche decimee de l'ISR ADC. Le cout mesure au scope : une
// sequence sur treize, l'ISR ADC passait de 4,9 us a plus de 9 us et
// debordait sur la conversion suivante -- l'ADC ecrivant alors ADCRESULT
// pendant que l'ISR recopiait encore les resultats precedents.
//
// Sur son propre timer, le dt est aussi rigoureusement constant qu'avec la
// decimation (meme cadence, cf. CTRL_TICK_PERIOD_US), et l'ISR ADC -- INT1,
// donc plus prioritaire que INT13 -- la preempte sans dommage : la
// surveillance de survoltage garde sa latence de 15 us quoi qu'il arrive
// ici. La regulation reste prioritaire sur l'UART, qui est en groupe 9.
// Descend le duty d'un etage vers son plancher, un pas par tick.
//
// SYNCHRONE, et c'est tout le sujet. On ecrit CMPA, on ne touche NI a la
// porte ET NI a AQCSFRC :
//
//   - AQCSFRC coupe le MOSFET a un instant QUELCONQUE de la periode, donc
//     HORS de la fenetre de blanking ancree sur CTR = 0. Le front produit
//     alors une pointe que le comparateur voit -- defaut overI2 constate
//     au banc le 24/08 en coupant HT a 500 V.
//   - CMPA est en registre d'ombre et bascule au passage a zero : chaque
//     impulsion se termine normalement a son compare. Aucun front ne sort
//     de la fenetre.
//
// PROGRESSIF, et sur les DEUX etages. Une extinction en un pas fait
// disparaitre la charge de l'etage amont instantanement -- c'est ce qui a
// produit le defaut overV1 en coupant HT a 400 V, V1 montant de 76,3 a
// 83,5 V pour une coupure a 82.
//
// PLANCHER NON NUL. On s'arrete a quelques pour cent plutot qu'a zero :
// l'etage continue de commuter, donc reste dans un regime normal, et la
// reprise ne part pas d'un etat degenere. A ce duty la tension visee tombe
// bien sous celle du condensateur de sortie, la diode est bloquee et plus
// rien n'est transmis -- l'effet est celui de zero, sans la discontinuite.
static void coast_stage(uint16_t i, int32_t measured_raw)
{
    // L'integrateur est remis a zero : la reprise ne deroule pas une avance
    // perimee, elle repart d'un duty nul et remonte si besoin.
    s_accum[i] = 0;

    // LA RAMPE EST COLLEE A LA TENSION MESUREE. C'est la seule des trois
    // formes essayees qui tienne, et les deux autres ont echoue de facons
    // OPPOSEES -- toutes deux constatees au banc le 24/08 :
    //
    //   - remise a ZERO : la rampe repart de 0 et met 500/510 = 0,98 s a
    //     rejoindre la consigne. Elle finit par rattraper la tension reelle
    //     et l'etage 2 pousse alors dans un condensateur de 2 uF QUE PLUS
    //     RIEN NE CHARGE, HV_EN etant ouvert. -> overVout UNE SECONDE apres
    //     la coupure.
    //   - laissee EN PLACE : elle reste a 500 V. Au RUN suivant la sortie
    //     est a zero et l'erreur vaut 500 V d'emblee, duty sature, plus
    //     aucun soft-start. -> overVout des le demarrage.
    //
    // Collee au mesure, elle satisfait les deux : erreur nulle a la reprise
    // immediate, et point de depart correct pour un demarrage ulterieur.
    // C'est aussi ce que promettait deja le commentaire de main.c, "control.c
    // rampe ensuite depuis la tension mesuree" -- promesse qui n'etait en
    // fait implementee nulle part.
    s_ramp_q8[i] = measured_raw << RAMP_FRAC_BITS;

    if (s_coast_duty_q8[i] > (s_coast_floor_q8[i] + (int32_t)CTRL_COAST_STEP_Q8))
    {
        s_coast_duty_q8[i] -= (int32_t)CTRL_COAST_STEP_Q8;
    }
    else
    {
        s_coast_duty_q8[i] = s_coast_floor_q8[i];
    }

    pwm_set_duty_q8(k_stages[i], (uint32_t)s_coast_duty_q8[i]);
}

// Extinction sequencee des DEUX etages, sans inhibition materielle. A
// n'utiliser que pour sequencer une coupure de HT : ce n'est PAS un organe
// de securite -- la protection rapide reste le comparateur et la Trip Zone.
void control_coast(bool coast)
{
    s_coast = coast;
}

void control_tick(void)
{
    int32_t v1_raw = (int32_t)adc_get_raw(ADC_CH_V1);
    int32_t vout_raw = (int32_t)adc_get_raw(ADC_CH_VOUT);
    int32_t error;

    // Une demande de trip venue de la boucle principale (control_trip())
    // peut s'etre presentee entre-temps.
    if (s_trip_requested)
    {
        s_trip_requested = false;
        s_state = CTRL_STATE_FAULT;
    }

    // Un defaut est VERROUILLE. Seule exception, et une seule : la
    // sous-tension d'entree, qui decrit l'etat de la source et non une
    // avarie du convertisseur. Voir calib.h pour la justification complete
    // et pour le plafond de tentatives.
    if (s_state == CTRL_STATE_FAULT)
    {
        both_off();

#if CTRL_VIN_UV_REARM
        if (s_fault == FAULT_UNDERVOLTAGE_VIN
            && s_vin_retries < CTRL_VIN_UV_MAX_RETRIES)
        {
            // Le seuil teste est celui d'ARMEMENT (12 V), pas celui de
            // coupure (9,5 V) : les 2,5 V d'ecart sont l'hysteresis qui
            // interdit le battement.
            if ((int32_t)adc_get_raw(ADC_CH_VIN) > VIN_UV_ARM_RAW)
            {
                s_vin_rearm_count++;
            }
            else
            {
                s_vin_rearm_count = 0U; // il faut une duree CONTINUE
            }

            if (s_vin_rearm_count >= CTRL_VIN_UV_REARM_STEPS)
            {
                s_vin_rearm_count = 0U;
                s_vin_retries++;

                // Retour a IDLE et NON reprise en l'etat : le pas suivant
                // repartira de la tension mesuree avec un accumulateur nul,
                // donc par la sequence de demarrage complete. Reprendre en
                // cours de rampe reimposerait le duty d'avant la coupure sur
                // une sortie qui s'est videe entre-temps -- le courant
                // d'inductance s'emballe alors en quelques periodes (constate
                // en debogage, cf. le chemin EMUSTOP de main.c).
                s_fault = FAULT_NONE;
                s_vin_uv_count = 0U;
                s_state = CTRL_STATE_IDLE;
            }
        }
#endif
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
#if CTRL_VIN_UV_REARM
        // Le regime etabli est atteint : les tentatives de rearmement
        // consommees sont rendues. Ecrit a chaque pas plutot que sur la
        // transition -- c'est idempotent, et ca couvre du meme coup les deux
        // chemins d'entree dans cet etat (avec et sans etage 2).
        s_vin_retries = 0U;
#endif

        // Regime etabli. Les rampes restent actives : un changement de
        // consigne en marche est suivi progressivement, ce qui est
        // exactement le cas d'usage des essais par paliers.
        if (s_coast)
        {
            // EXTINCTION SEQUENCEE, demandee par main.c avant d'ouvrir
            // HV_EN. Voir HV_OFF_INHIBIT_SEQ dans calib.h.
            coast_stage(S1, v1_raw);
            coast_stage(S2, vout_raw);
            break;
        }

        ramp_toward(S1, V1_RAMP_STEP_Q8);
        (void)regulate(S1, v1_raw);

        if (s_s2_enabled)
        {
            ramp_toward(S2, VOUT_RAMP_STEP_Q8);
            (void)regulate(S2, vout_raw);
        }
        else
        {
#if STAGE2_OPENLOOP_TEST
            // ESSAI EN BOUCLE OUVERTE : duty FIXE, aucune contre-reaction.
            // Voir calib.h pour la portee et les dangers.
            //
            // Applique ICI et NULLE PART AILLEURS, pour trois raisons :
            //  - c'est le seul etat ou l'etage 1 est etabli, donc ou l'etage 2
            //    voit une tension d'entree stable ;
            //  - both_off() continue de remettre l'etage 2 a zero sur defaut
            //    et a l'arret, l'essai ne survit donc a aucun trip ;
            //  - la boucle principale n'ecrit jamais ce duty, ce qui evite le
            //    conflit d'ecriture decrit dans main.c.
            pwm_set_duty_counts(STAGE_2, s_openloop_counts);
#else
            // Duty force a zero a CHAQUE pas, pas seulement sur la
            // transition : rien ne doit pouvoir laisser une valeur
            // residuelle dans CMPA de l'etage 2.
            stage_reset(S2);

            // Et la rampe suit la tension mesuree tant qu'on ne regule pas.
            // Sans ca elle resterait figee a sa derniere valeur, et le jour
            // ou l'etage 2 est reactive l'erreur vaudrait d'emblee tout
            // l'ecart -- duty sature, aucun soft-start. C'est la meme regle
            // que dans coast_stage(), et pour la meme raison.
            s_ramp_q8[S2] = vout_raw << RAMP_FRAC_BITS;
#endif
        }
        break;
    }
}
