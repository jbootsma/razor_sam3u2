// clang-format off
/*!**********************************************************************************************************************
@file main.c
@brief Main system file for the EiE firmware.
***********************************************************************************************************************/

#include "configuration.h"

extern void kill_x_cycles(u32);

/***********************************************************************************************************************
Global variable definitions with scope across entire project.
All Global variable names shall start with "G_"
***********************************************************************************************************************/
/* New variables */
volatile u32 G_u32SystemTime1ms = 0;    /*!< @brief Global system time incremented
                                           every ms, max 2^32 (~49 days) */
volatile u32 G_u32SystemTime1s = 0;     /*!< @brief Global system time incremented
                                           every second, max 2^32 (~136 years) */
volatile u32 G_u32SystemFlags = 0;      /*!< @brief Global system flags */
volatile u32 G_u32ApplicationFlags = 0; /*!< @brief Global system application flags: set when application is
                                           successfully initialized */
volatile s32 G_s32SysTickSyncAdj = 0;   /*!< @brief Global adjustment to the length of the next 1ms systick
                                           period. Set by SysTickSyncEvt() in order to achieve event
                                           synchronization. The value is reset to 0 after the adjustment is
                                           applied for a single tick. */

/* Task short names corresponding to G_u32ApplicationFlags in main.h */
#ifdef EIE_ASCII
const u8 G_aau8AppShortNames[NUMBER_APPLICATIONS][MAX_TASK_NAME_SIZE] = {"LED", "BUTTON", "DEBUG", "TIMER",
                                                                         "LCD", "ADC",    "ANT"};
#endif /* EIE_ASCII */

#ifdef EIE_DOTMATRIX
const u8 G_aau8AppShortNames[NUMBER_APPLICATIONS][MAX_TASK_NAME_SIZE] = {"LED", "BUTTON", "DEBUG", "TIMER",
                                                                         "LCD", "ADC",    "ANT",   "CAPTOUCH"};
#endif /* EIE_DOTMATRIX */

/*--------------------------------------------------------------------------------------------------------------------*/
/* External global variables defined in other files (must indicate which file
 * they are defined in) */

extern u32 G_u32DebugFlags; // debug.c

/***********************************************************************************************************************
Global variable definitions with scope limited to this local application.
Variable names shall start with "Main_" and be declared as static.
***********************************************************************************************************************/

/*!---------------------------------------------------------------------------------------------------------------------
@fn int main(void)
@brief Main program where all tasks are initialized and executed.

Requires:
- NONE

Promises:
- NONE

*/
int main(void) {
  G_u32SystemFlags |= _SYSTEM_INITIALIZING;

  /* Low level initialization */
  WatchDogSetup();
  ClockSetup();
  GpioSetup();
  PWMSetupAudio();
  InterruptSetup();
  SysTickSetup();

  /* Driver initialization */
  MessagingInitialize();
  UartInitialize();
  UsbInitialize();
  DebugInitialize();

  /* Debug messages through DebugPrintf() are available from here */
  ButtonInitialize();
  TimerInitialize();
  SpiInitialize();
  SspInitialize();
  TwiInitialize();

  Adc12Initialize();
  LcdInitialize();
  LedInitialize();
  AntInitialize();
  AntApiInitialize();

#ifdef EIE_ASCII
#endif /* EIE_ASCII */

#ifdef EIE_DOTMATRIX
  CapTouchInitialize();
#endif /* EIE_DOTMATRIX */

  /* Application initialization */
  UserApp1Initialize();
  UserApp2Initialize();
  UserApp3Initialize();

  /* Exit initialization */
  SystemStatusReport();
  G_u32SystemFlags &= ~_SYSTEM_INITIALIZING;

  /* Super loop */
  while (1) {
    static u32 u32DriverProfCtr = 0;
    static u32 u32AppProfCtr = 0;
    static u16 u16ProfFrameCtr = 1000;

    WATCHDOG_BONE();
    SystemTimeCheck();

    u32 u32StartTick = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;

    /* Drivers */
    MessagingRunActiveState();
    UartRunActiveState();
    UsbRunActiveState();
    DebugRunActiveState();

    ButtonRunActiveState();
    TimerRunActiveState();
    SpiRunActiveState();
    SspRunActiveState();
    TwiRunActiveState();

    Adc12RunActiveState();
    LcdRunActiveState();
    LedRunActiveState();
    AntRunActiveState();
    AntApiRunActiveState();

    u32 u32DriverDoneTick = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;

#ifdef EIE_ASCII
#endif /* EIE_ASCII */

#ifdef EIE_DOTMATRIX
    CapTouchRunActiveState();
#endif /* EIE_DOTMATRIX */


    /* Applications */
    UserApp1RunActiveState();
    UserApp2RunActiveState();
    UserApp3RunActiveState();

    u32 u32AppDoneTick = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;

    if (G_u32DebugFlags & _DEBUG_PROFILE_ENABLED) {
      // These look backwards, it's because the systick timer is a down counter.
      u32DriverProfCtr += u32StartTick - u32DriverDoneTick;
      u32AppProfCtr += u32DriverDoneTick - u32AppDoneTick;

      if (--u16ProfFrameCtr == 0) {
        u16ProfFrameCtr = 1000;

        // Divide by a factor of 10 instead of 1000, as it cancels a later multiply by 100.
        u32DriverProfCtr = (u32DriverProfCtr + 5) / 10;
        u32AppProfCtr = (u32AppProfCtr + 5) / 10;
        DebugPrintf("Profile drivers=");
        DebugPrintNumber((u32DriverProfCtr +  U32_SYSTICK_COUNT / 2) / U32_SYSTICK_COUNT);
        DebugPrintf("%, apps=");
        DebugPrintNumber((u32AppProfCtr + U32_SYSTICK_COUNT / 2) / U32_SYSTICK_COUNT);
        DebugPrintf("%\r\n");

        u32DriverProfCtr = 0;
        u32AppProfCtr = 0;
      }
    }

    /* System sleep */
    HEARTBEAT_OFF();
    SystemSleep();
    HEARTBEAT_ON();

  } /* end while(1) main super loop */

} /* end main() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
