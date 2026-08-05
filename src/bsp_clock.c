#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "calib.h"

void bsp_clock_init(void)
{
    EALLOW;
    SysCtrlRegs.PLLLOCKPRD = CLK_PLLLOCKPRD;
    EDIS;

    InitSysCtrl();

#ifdef _FLASH
    // Etats d'attente de la flash. Au reset ils valent leur MAXIMUM
    // (FBANKWAIT = 0x0F0F, soit RANDWAIT = PAGEWAIT = 15) : chaque acces
    // coute alors 16 cycles au lieu des 3 necessaires a 60 MHz, et TOUT le
    // code resident en flash tourne environ 4 fois trop lentement.
    //
    // Mesure avant/apres sur l'ISR ADC : c'etait le facteur dominant, bien
    // devant l'optimisation du compilateur.
    //
    // InitFlash() DOIT s'executer depuis la RAM (elle reconfigure la flash
    // sous ses propres pieds) : c'est tout l'objet du memcpy des ramfuncs
    // fait en tete de main(), qui existait deja mais ne servait a rien
    // puisque la fonction n'etait jamais appelee.
    InitFlash();
#endif
}
