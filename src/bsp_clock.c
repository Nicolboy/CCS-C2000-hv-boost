#include "DSP28x_Project.h"
#include "bsp_clock.h"
#include "calib.h"

void bsp_clock_init(void)
{
    EALLOW;
    SysCtrlRegs.PLLLOCKPRD = CLK_PLLLOCKPRD;
    EDIS;

    InitSysCtrl();
}
