/*!**********************************************************************************************************************
@file eief1-pcb-01.c
@brief This file provides core and GPIO functions for the eief1-pcb-01 board.

Basic board setup functions are here which are not part of the API for the system since they
are one-time configurations for the processor.

------------------------------------------------------------------------------------------------------------------------
GLOBALS
- NONE

CONSTANTS
- NONE

TYPES
- NONE

PUBLIC FUNCTIONS
-

PROTECTED FUNCTIONS
-

***********************************************************************************************************************/

#include "configuration.h"

/***********************************************************************************************************************
Global variable definitions with scope across entire project.
All Global variable names shall start with "G_xxBsp"
***********************************************************************************************************************/
/* New variables */

/*! LED locations: order must correspond to the order set in LedNameType in the header file. */
const PinConfigurationType G_asBspLedConfigurations[U8_TOTAL_LEDS] = { {PB_13_LED_WHT, PORTB, ACTIVE_HIGH},
                                                                       {PB_14_LED_PRP, PORTB, ACTIVE_HIGH},
                                                                       {PB_18_LED_BLU, PORTB, ACTIVE_HIGH},
                                                                       {PB_16_LED_CYN, PORTB, ACTIVE_HIGH},
                                                                       {PB_19_LED_GRN, PORTB, ACTIVE_HIGH},
                                                                       {PB_17_LED_YLW, PORTB, ACTIVE_HIGH},
                                                                       {PB_15_LED_ORG, PORTB, ACTIVE_HIGH},
                                                                       {PB_20_LED_RED, PORTB, ACTIVE_HIGH},
                                                                       {PB_10_LCD_BL_RED, PORTB, ACTIVE_HIGH},
                                                                       {PB_11_LCD_BL_GRN, PORTB, ACTIVE_HIGH},
                                                                       {PB_12_LCD_BL_BLU, PORTB, ACTIVE_HIGH}
                                                                      };




/*! Button locations: order must correspond to the order set in ButtonNameType in the header file. */
const PinConfigurationType G_asBspButtonConfigurations[U8_TOTAL_BUTTONS] = { {PA_17_BUTTON0, PORTA, ACTIVE_LOW},
                                                                             {PB_00_BUTTON1, PORTB, ACTIVE_LOW},
                                                                             {PB_01_BUTTON2, PORTB, ACTIVE_LOW},
                                                                             {PB_02_BUTTON3, PORTB, ACTIVE_LOW},
                                                                           };


/*--------------------------------------------------------------------------------------------------------------------*/
/* Existing variables (defined in other files -- should all contain the "extern" keyword) */
extern volatile u32 G_u32SystemTime1ms;        /*!< @brief From main.c */
extern volatile u32 G_u32SystemTime1s;         /*!< @brief From main.c */
extern volatile u32 G_u32SystemFlags;          /*!< @brief From main.c */
extern volatile u32 G_u32ApplicationFlags;     /*!< @brief From main.c */

extern volatile u32 G_u32DebugFlags;           /*!< @brief From debug.c */


/***********************************************************************************************************************
Global variable definitions with scope limited to this local application.
Variable names shall start with "Bsp_" and be declared as static.
***********************************************************************************************************************/
static u32 Bsp_u32TimingViolationsCounter = 0;


/***********************************************************************************************************************
Function Definitions
***********************************************************************************************************************/


/*--------------------------------------------------------------------------------------------------------------------*/
/*! @protectedsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*!---------------------------------------------------------------------------------------------------------------------
@fn void WatchDogSetup(void)

@brief Configures the watchdog timer.

The dog runs at 32kHz from the slow built-in RC clock source which varies
over operating conditions from 20kHz to 44kHz.

Since the main loop time / sleep time should be 1 ms most of the time, choosing a value
of 5 seconds should be plenty to avoid watchdog resets.

Note: the processor allows the WDMR register to be written just once after a reset.

Requires:
- SLCK is active at about 32kHz

Promises:
- Watchdog is set for 5 second timeout

*/
void WatchDogSetup(void)
{
  WDT->WDT_MR = WDT_MR_INIT;
} /* end WatchDogSetup() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void ClockSetup(void)

@brief Loads all registers required to set up the processor clocks.

Requires:
- NONE

Promises:
- EFC is set up with proper flash access wait states based on 48MHz system clock
- PMC is set up with proper oscillators and clock sources

*/
void ClockSetup(void)
{
  /* Enable the master clock on the PKC0 clock out pin (PA_27_CLOCK_OUT) */
  PMC->PMC_PCK[0] = PMC_PCK_CSS_MCK | PMC_PCK_PRES(1);
  PMC->PMC_SCER = PMC_SCER_PCK0;
  while ( !(PMC->PMC_SR & PMC_SR_PCKRDY0) );

  /* Turn on the main oscillator and wait for it to start up */
  PMC->CKGR_MOR = PMC_MOR_INIT;
  while ( !(PMC->PMC_SR & PMC_SR_MOSCXTS) );

  /* Assign main clock as crystal and wait for switch */
  PMC->CKGR_MOR |= (CKGR_MOR_MOSCSEL | CKGR_MOR_KEY_PASSWD);
  while ( !(PMC->PMC_SR & PMC_SR_MOSCSELS) );

  /* Set flash wait states to allow 48 MHz system clock (2 wait states required) */
  /* Note: There is an errata where the flash controller can't operate at sub-5 Mhz with
     wait states programmed, so this must be done after the clock is bumped to 12 Mhz*/
  EFC0->EEFC_FMR = EEFC_FMR_FWS(2);

  /* Initialize PLLA and wait for lock */
  PMC->CKGR_PLLAR = PMC_PLAAR_INIT;
  while ( !(PMC->PMC_SR & PMC_SR_LOCKA) );

  /* Assign the PLLA as the main system clock with prescaler active using the sequence suggested in the user guide */
  PMC->PMC_MCKR = PMC_MCKR_INIT;
  while ( !(PMC->PMC_SR & PMC_SR_MCKRDY) );
  PMC->PMC_MCKR = PMC_MCKR_PLLA;
  while ( !(PMC->PMC_SR & PMC_SR_MCKRDY) );

  /* Initialize UTMI for USB usage */
  PMC->CKGR_UCKR |= CKGR_UCKR_UPLLCOUNT(3) | CKGR_UCKR_UPLLEN;
  while ( !(PMC->PMC_SR & PMC_SR_LOCKU) );

  /* Activate the peripheral clocks needed for the system, must be done
     last to ensure all clock sources are valid before clocking periphs. */
  PMC->PMC_PCER0 = PMC_PCER_INIT;

} /* end ClockSetup */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void GpioSetup(void)

@brief Loads registers required to set up GPIO on the processor.

Requires:
- All configurations must match connected hardware.

Promises:
- Output pin for PA31_HEARTBEAT is configured

*/
void GpioSetup(void)
{
  /* Set all of the pin function registers in port A */
  PIOA->PIO_PER    =  PIOA_PER_INIT;
  PIOA->PIO_PDR    = ~PIOA_PER_INIT;
  PIOA->PIO_OER    =  PIOA_OER_INIT;
  PIOA->PIO_ODR    = ~PIOA_OER_INIT;
  PIOA->PIO_IFER   =  PIOA_IFER_INIT;
  PIOA->PIO_IFDR   = ~PIOA_IFER_INIT;
  PIOA->PIO_MDER   =  PIOA_MDER_INIT;
  PIOA->PIO_MDDR   = ~PIOA_MDER_INIT;
  PIOA->PIO_PUER   =  PIOA_PPUER_INIT;
  PIOA->PIO_PUDR   = ~PIOA_PPUER_INIT;
  PIOA->PIO_OWER   =  PIOA_OWER_INIT;
  PIOA->PIO_OWDR   = ~PIOA_OWER_INIT;

  PIOA->PIO_SODR   = PIOA_SODR_INIT;
  PIOA->PIO_CODR   = PIOA_CODR_INIT;
  PIOA->PIO_ABSR   = PIOA_ABSR_INIT;
  PIOA->PIO_SCIFSR = PIOA_SCIFSR_INIT;
  PIOA->PIO_DIFSR  = PIOA_DIFSR_INIT;
  PIOA->PIO_SCDR   = PIOA_SCDR_INIT;

  /* Set all of the pin function registers in port B */
  PIOB->PIO_PER    =  PIOB_PER_INIT;
  PIOB->PIO_PDR    = ~PIOB_PER_INIT;
  PIOB->PIO_OER    =  PIOB_OER_INIT;
  PIOB->PIO_ODR    = ~PIOB_OER_INIT;
  PIOB->PIO_IFER   =  PIOB_IFER_INIT;
  PIOB->PIO_IFDR   = ~PIOB_IFER_INIT;
  PIOB->PIO_MDER   =  PIOB_MDER_INIT;
  PIOB->PIO_MDDR   = ~PIOB_MDER_INIT;
  PIOB->PIO_PUER   =  PIOB_PPUER_INIT;
  PIOB->PIO_PUDR   = ~PIOB_PPUER_INIT;
  PIOB->PIO_OWER   =  PIOB_OWER_INIT;
  PIOB->PIO_OWDR   = ~PIOB_OWER_INIT;

  PIOB->PIO_SODR   = PIOB_SODR_INIT;
  PIOB->PIO_CODR   = PIOB_CODR_INIT;
  PIOB->PIO_ABSR   = PIOB_ABSR_INIT;
  PIOB->PIO_SCIFSR = PIOB_SCIFSR_INIT;
  PIOB->PIO_DIFSR  = PIOB_DIFSR_INIT;
  PIOB->PIO_SCDR   = PIOB_SCDR_INIT;

} /* end GpioSetup() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn  void SysTickSetup(void)

@brief Initializes the 1ms and 1s System Ticks off the core timer.

Requires:
- NVIC is setup and SysTick handler is installed

Promises:
- Both global system timers are reset and the SysTick core timer is configured for 1ms intervals

*/
void SysTickSetup(void)
{
  G_u32SystemTime1ms = 0;
  G_u32SystemTime1s  = 0;

  /* Load the SysTick Counter Value */
  SysTick->LOAD = U32_SYSTICK_COUNT - 1;
  SysTick->VAL = (0x00);
  SysTick->CTRL = SYSTICK_CTRL_INIT;

} /* end SysTickSetup() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void SystemTimeCheck(void)

@brief Checks for violations of the 1ms system loop time.

Requires:
- Should be called only once in the main system loop

Promises:
@Bsp_u32TimingViolationsCounter is incremented if G_u32SystemTime1ms has
increased by more than one since this function was last called

*/
void SystemTimeCheck(void)
{
   static u32 u32PreviousSystemTick = 0;

  /* Check system timing */
  if( (G_u32SystemTime1ms - u32PreviousSystemTick) != 1)
  {
    /* Flag, count and optionally display warning */
    Bsp_u32TimingViolationsCounter++;
    G_u32SystemFlags |= _SYSTEM_TIME_WARNING;

    if(G_u32DebugFlags & _DEBUG_TIME_WARNING_ENABLE)
    {
      DebugPrintf("\n\r*** 1ms timing violation: ");
      DebugPrintNumber(Bsp_u32TimingViolationsCounter);
      DebugLineFeed();
    }
  }

  u32PreviousSystemTick = G_u32SystemTime1ms;

} /* end SystemTimeCheck() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void SystemSleep(void)

@brief Puts the system into sleep mode.

_SYSTEM_SLEEPING is set here so if the system wakes up because of a non-Systick
interrupt, it can go back to sleep.

Deep sleep mode is currently disabled, so maximum processor power savings are
not yet realized.  To enable deep sleep, there are certain considerations for
waking up that would need to be taken care of.

Requires:
- SysTick is running with interrupt enabled for wake from Sleep LPM

Promises:
- Configures processor for sleep while still allowing any required
  interrupt to wake it up.
- G_u32SystemFlags _SYSTEM_SLEEPING is set

*/
void SystemSleep(void)
{
  /* Set the system control register for Sleep (but not Deep Sleep) */
  PMC->PMC_FSMR &= ~PMC_FSMR_LPM;
  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

  /* Set the sleep flag (cleared only in SysTick ISR */
  G_u32SystemFlags |= _SYSTEM_SLEEPING;

  /* Now enter the selected LPM */
  while(G_u32SystemFlags & _SYSTEM_SLEEPING)
  {
    __WFI();
  }

} /* end SystemSleep(void) */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void PWMSetupAudio(void)

@brief Configures the PWM peripheral for audio operation on H0 and H1 channels.

Requires:
- Peripheral resources not used for any other function.

Promises:
- PWM is configured for PWM mode and currently off.

*/
void PWMSetupAudio(void)
{
  /* Set all intialization values */
  PWM->PWM_CLK = PWM_CLK_INIT;

  PWM->PWM_CH_NUM[0].PWM_CMR = PWM_CMR0_INIT;
  PWM->PWM_CH_NUM[0].PWM_CPRD    = PWM_CPRD0_INIT; /* Set current frequency */
  PWM->PWM_CH_NUM[0].PWM_CPRDUPD = PWM_CPRD0_INIT; /* Latch CPRD values */
  PWM->PWM_CH_NUM[0].PWM_CDTY    = PWM_CDTY0_INIT; /* Set 50% duty */
  PWM->PWM_CH_NUM[0].PWM_CDTYUPD = PWM_CDTY0_INIT; /* Latch CDTY values */

  PWM->PWM_CH_NUM[1].PWM_CMR = PWM_CMR1_INIT;
  PWM->PWM_CH_NUM[1].PWM_CPRD    = PWM_CPRD1_INIT; /* Set current frequency  */
  PWM->PWM_CH_NUM[1].PWM_CPRDUPD = PWM_CPRD1_INIT; /* Latch CPRD values */
  PWM->PWM_CH_NUM[1].PWM_CDTY    = PWM_CDTY1_INIT; /* Set 50% duty */
  PWM->PWM_CH_NUM[1].PWM_CDTYUPD = PWM_CDTY1_INIT; /* Latch CDTY values */


} /* end PWMSetupAudio() */


/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*!---------------------------------------------------------------------------------------------------------------------
@fn void PWMAudioSetFrequency(BuzzerChannelType eChannel_, u16 u16Frequency_)

@brief Configures the PWM peripheral with the desired frequency on the specified channel.

If the buzzer is already on, it will change frequency (essentially) immediately.
If it is not on, the new frequency will be audible next time PWMAudioOn() is called.

Example:

PWMAudioSetFrequency(BUZZER1, 1000);

Requires:
- The PWM peripheral is correctly configured for the current processor clock speed.
- CPRE_CLCK is the clock frequency for the PWM peripheral

@param eChannel_ is the channel of interest and corresponds to the channel bit
position of the buzzer in the PWM peripheral
@param u16Frequency_ is in Hertz and should be in the range 100 - 20,000 since
       that is the audible range.  Higher and lower frequencies are allowed, though.

Promises:
- The frequency and duty cycle values for the requested channel are calculated
  and then latched to their respective update registers (CPRDUPDR, CDTYUPDR)
- If the channel is not valid, nothing happens

*/
void PWMAudioSetFrequency(BuzzerChannelType eChannel_, u16 u16Frequency_)
{
  u32 u32ChannelPeriod;
  PwmCh_num* psChannelAddress;

  /* Get the base address of the channel */
  switch (eChannel_)
  {
    case BUZZER1:
    {
      psChannelAddress = &PWM->PWM_CH_NUM[0];
      break;
    }

    case BUZZER2:
    {
      psChannelAddress = &PWM->PWM_CH_NUM[1];
      break;
    }

    default:
    {
      /* Invalid channel */
      return;
    }
  }

  /* Calculate the period based on the requested frequency.
  The duty cycle is this value divided by 2 (right shift 1) */
  u32ChannelPeriod = CPRE_CLCK / u16Frequency_;

  /* Set different registers depending on if PWM is already running */
  if (PWM->PWM_SR & eChannel_)
  {
    /* Beeper is already running, so use update registers */
    psChannelAddress->PWM_CPRDUPD = u32ChannelPeriod;
    psChannelAddress->PWM_CDTYUPD = u32ChannelPeriod >> 1;
  }
  else
  {
    /* Beeper is off, so use direct registers */
    psChannelAddress->PWM_CPRD = u32ChannelPeriod;
    psChannelAddress->PWM_CDTY = u32ChannelPeriod >> 1;
  }

} /* end PWMAudioSetFrequency() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void PWMAudioOn(BuzzerChannelType eBuzzerChannel_)

@brief Enables a PWM channel so the buzzer is on.

Example:

PWMAudioOn(BUZZER2);

Requires:
- All peripheral values should be configured
- Frequency of the desired channel should already be set

@param eBuzzerChannel_ is a valid BuzzerChannelType (BUZZER1 or BUZZER2)

Promises:
- PWM for the selected channel is enabled

*/
void PWMAudioOn(BuzzerChannelType eBuzzerChannel_)
{
  /* Enable the channel to turn the buzzer on*/
  PWM->PWM_ENA = (u32)eBuzzerChannel_;

} /* end PWMAudioOn() */


/*!---------------------------------------------------------------------------------------------------------------------
@fn void PWMAudioOff(BuzzerChannelType eBuzzerChannel_)

@brief Disables a PWM channel so the buzzer is off.

Example:

PWMAudioOff(BUZZER2);


Requires:
@param eBuzzerChannel_ is a valid BuzzerChannelType (BUZZER1 or BUZZER2)

Promises:
- PWM for the selected channel is disabled

*/
void PWMAudioOff(BuzzerChannelType eBuzzerChannel_)
{
  /* Disable the channel to turn the buzzer off */
  PWM->PWM_DIS = (u32)eBuzzerChannel_;

} /* end PWMAudioOff() */


/*--------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/