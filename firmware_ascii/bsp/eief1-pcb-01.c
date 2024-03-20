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
const PinConfigurationType G_asBspLedConfigurations[U8_TOTAL_LEDS] = {
    {PB_13_LED_WHT, PORTB, ACTIVE_HIGH},    {PB_14_LED_PRP, PORTB, ACTIVE_HIGH},
    {PB_18_LED_BLU, PORTB, ACTIVE_HIGH},    {PB_16_LED_CYN, PORTB, ACTIVE_HIGH},
    {PB_19_LED_GRN, PORTB, ACTIVE_HIGH},    {PB_17_LED_YLW, PORTB, ACTIVE_HIGH},
    {PB_15_LED_ORG, PORTB, ACTIVE_HIGH},    {PB_20_LED_RED, PORTB, ACTIVE_HIGH},
    {PB_10_LCD_BL_RED, PORTB, ACTIVE_HIGH}, {PB_11_LCD_BL_GRN, PORTB, ACTIVE_HIGH},
    {PB_12_LCD_BL_BLU, PORTB, ACTIVE_HIGH}};

/*! Button locations: order must correspond to the order set in ButtonNameType in the header file. */
const PinConfigurationType G_asBspButtonConfigurations[U8_TOTAL_BUTTONS] = {
    {PA_17_BUTTON0, PORTA, ACTIVE_LOW},
    {PB_00_BUTTON1, PORTB, ACTIVE_LOW},
    {PB_01_BUTTON2, PORTB, ACTIVE_LOW},
    {PB_02_BUTTON3, PORTB, ACTIVE_LOW},
};

/*--------------------------------------------------------------------------------------------------------------------*/
/* Existing variables (defined in other files -- should all contain the "extern" keyword) */
extern volatile u32 G_u32SystemTime1ms;    /*!< @brief From main.c */
extern volatile u32 G_u32SystemTime1s;     /*!< @brief From main.c */
extern volatile u32 G_u32SystemFlags;      /*!< @brief From main.c */
extern volatile u32 G_u32ApplicationFlags; /*!< @brief From main.c */
extern volatile s32 G_s32SysTickSyncAdj;   /*!< @brief From main.c */

extern volatile u32 G_u32DebugFlags; /*!< @brief From debug.c */

/***********************************************************************************************************************
Global variable definitions with scope limited to this local application.
Variable names shall start with "Bsp_" and be declared as static.
***********************************************************************************************************************/
static u32 Bsp_u32TimingViolationsCounter = 0;

static DmaInfo *volatile Bsp_pstCurrPwm;
static DmaInfo *volatile Bsp_pstNextPwm;

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
void WatchDogSetup(void) { WDT->WDT_MR = WDT_MR_INIT; } /* end WatchDogSetup() */

/*!---------------------------------------------------------------------------------------------------------------------
@fn void ClockSetup(void)

@brief Loads all registers required to set up the processor clocks.

Requires:
- NONE

Promises:
- EFC is set up with proper flash access wait states based on 48MHz system clock
- PMC is set up with proper oscillators and clock sources

*/
void ClockSetup(void) {
  /* Enable the master clock on the PKC0 clock out pin (PA_27_CLOCK_OUT) */
  PMC->PMC_PCK[0] = PMC_PCK_CSS_MCK | PMC_PCK_PRES(1);
  PMC->PMC_SCER = PMC_SCER_PCK0;
  while (!(PMC->PMC_SR & PMC_SR_PCKRDY0))
    ;

  /* Turn on the main oscillator and wait for it to start up */
  PMC->CKGR_MOR = PMC_MOR_INIT;
  while (!(PMC->PMC_SR & PMC_SR_MOSCXTS))
    ;

  /* Assign main clock as crystal and wait for switch */
  PMC->CKGR_MOR |= (CKGR_MOR_MOSCSEL | CKGR_MOR_KEY_PASSWD);
  while (!(PMC->PMC_SR & PMC_SR_MOSCSELS))
    ;

  /* Set flash wait states to allow 48 MHz system clock (2 wait states required) */
  /* Note: There is an errata where the flash controller can't operate at sub-5 Mhz with
     wait states programmed, so this must be done after the clock is bumped to 12 Mhz*/
  EFC0->EEFC_FMR = EEFC_FMR_FWS(2);

  /* Initialize PLLA and wait for lock */
  PMC->CKGR_PLLAR = PMC_PLAAR_INIT;
  while (!(PMC->PMC_SR & PMC_SR_LOCKA))
    ;

  /* Assign the PLLA as the main system clock with prescaler active using the sequence suggested in the user guide */
  PMC->PMC_MCKR = PMC_MCKR_INIT;
  while (!(PMC->PMC_SR & PMC_SR_MCKRDY))
    ;
  PMC->PMC_MCKR = PMC_MCKR_PLLA;
  while (!(PMC->PMC_SR & PMC_SR_MCKRDY))
    ;

  /* Initialize UTMI for USB usage */
  PMC->CKGR_UCKR |= CKGR_UCKR_UPLLCOUNT(3) | CKGR_UCKR_UPLLEN;
  while (!(PMC->PMC_SR & PMC_SR_LOCKU))
    ;

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
void GpioSetup(void) {
  /* Set all of the pin function registers in port A */
  PIOA->PIO_PER = PIOA_PER_INIT;
  PIOA->PIO_PDR = ~PIOA_PER_INIT;
  PIOA->PIO_OER = PIOA_OER_INIT;
  PIOA->PIO_ODR = ~PIOA_OER_INIT;
  PIOA->PIO_IFER = PIOA_IFER_INIT;
  PIOA->PIO_IFDR = ~PIOA_IFER_INIT;
  PIOA->PIO_MDER = PIOA_MDER_INIT;
  PIOA->PIO_MDDR = ~PIOA_MDER_INIT;
  PIOA->PIO_PUER = PIOA_PPUER_INIT;
  PIOA->PIO_PUDR = ~PIOA_PPUER_INIT;
  PIOA->PIO_OWER = PIOA_OWER_INIT;
  PIOA->PIO_OWDR = ~PIOA_OWER_INIT;

  PIOA->PIO_SODR = PIOA_SODR_INIT;
  PIOA->PIO_CODR = PIOA_CODR_INIT;
  PIOA->PIO_ABSR = PIOA_ABSR_INIT;
  PIOA->PIO_SCIFSR = PIOA_SCIFSR_INIT;
  PIOA->PIO_DIFSR = PIOA_DIFSR_INIT;
  PIOA->PIO_SCDR = PIOA_SCDR_INIT;

  /* Set all of the pin function registers in port B */
  PIOB->PIO_PER = PIOB_PER_INIT;
  PIOB->PIO_PDR = ~PIOB_PER_INIT;
  PIOB->PIO_OER = PIOB_OER_INIT;
  PIOB->PIO_ODR = ~PIOB_OER_INIT;
  PIOB->PIO_IFER = PIOB_IFER_INIT;
  PIOB->PIO_IFDR = ~PIOB_IFER_INIT;
  PIOB->PIO_MDER = PIOB_MDER_INIT;
  PIOB->PIO_MDDR = ~PIOB_MDER_INIT;
  PIOB->PIO_PUER = PIOB_PPUER_INIT;
  PIOB->PIO_PUDR = ~PIOB_PPUER_INIT;
  PIOB->PIO_OWER = PIOB_OWER_INIT;
  PIOB->PIO_OWDR = ~PIOB_OWER_INIT;

  PIOB->PIO_SODR = PIOB_SODR_INIT;
  PIOB->PIO_CODR = PIOB_CODR_INIT;
  PIOB->PIO_ABSR = PIOB_ABSR_INIT;
  PIOB->PIO_SCIFSR = PIOB_SCIFSR_INIT;
  PIOB->PIO_DIFSR = PIOB_DIFSR_INIT;
  PIOB->PIO_SCDR = PIOB_SCDR_INIT;

} /* end GpioSetup() */

/*!---------------------------------------------------------------------------------------------------------------------
@fn  void SysTickSetup(void)

@brief Initializes the 1ms and 1s System Ticks off the core timer.

Requires:
- NVIC is setup and SysTick handler is installed

Promises:
- Both global system timers are reset and the SysTick core timer is configured for 1ms intervals

*/
void SysTickSetup(void) {
  G_u32SystemTime1ms = 0;
  G_u32SystemTime1s = 0;

  /* Load the SysTick Counter Value */
  SysTick->LOAD = U32_SYSTICK_COUNT - 1;
  SysTick->VAL = (0x00);
  SysTick->CTRL = SYSTICK_CTRL_INIT;

} /* end SysTickSetup() */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Attempt to synchronize the 1ms timer to a specific event.

This should be called regularly in order to achieve actual synchronization. Each call will adjust
the next systick period by up to 5% in order to move the 1ms tick and the sync event closer to
being in sync (it is assumed here that the sync event occurs on multiples of 1ms).

This function allows a target diff to be applied too in case the 1ms timer should lag or lead the
specified event.

Requires:
- Systick has been setup and is providing a 1ms periodic interrupt.

Promises:
- The periodic interrupt time will be adjusted by up to 5% in order to synchronize to the event.

@parameter u32Measured_ The measured value of the systick counter when the event occurred.
@parameter s32TgtDiff_ The amount of desired counter ticks between the event occuring and the 1ms
                       tick occuring (ie. If positive the goal is to have the tick slightly after
                       the sync event.)
*/
void SysTickSyncEvt(u32 u32Measured_, s32 s32TgtDiff_) {
  // Signed math is okay here because systick values are all 24 bits, and it makes the delta
  // calculations much easier.

  // In order to account for the fact that the actual reload value is being adjusted on the fly,
  // it's best to frame the problem as "what should the measured value be", and handle the cases
  // of the event occuring before or after wraparound separately.

  // If diff is positive the sync event should happen before the systick. Since it's a downcounter
  // that means we add the diff to the target. (higher numbers are earlier).
  s32 s32Tgt = U32_SYSTICK_COUNT + s32TgtDiff_;
  s32 s32Actual = u32Measured_;

  // Move the target for the case where the measurement was before the reload.
  if (s32Actual < (U32_SYSTICK_COUNT / 2)) {
    s32Tgt -= U32_SYSTICK_COUNT;
  }

  s32 s32Err = s32Tgt - s32Actual;

  // Reduce error by a factor of 10 to smooth out changes. This is basically a P only PID
  // controller. The rounding here is away from 0 to ensure there's always some signal unless error
  // really is 0. This is relying on C99's specification that integer division truncates towards 0.
  if (s32Err > 0) {
    s32Err = (s32Err + 9) / 10;
  } else {
    s32Err = (s32Err - 9) / 10;
  }

  // err < 0 means the event happened earlier than desired. So we want a shorter period, which means
  // an adjustment < 0 as well, so no need to flip signs.

  // Cap the adjustment to 2% of the period, to avoid suprises if most of the CPU time is being used.
  static const s32 s32MaxAdjust = (U32_SYSTICK_COUNT * 2) / 100;

  if (s32Err > s32MaxAdjust) {
    s32Err = s32MaxAdjust;
  } else if (s32Err < -s32MaxAdjust) {
    s32Err = -s32MaxAdjust;
  }

  // No need for __disable_irq() here, a single write is already atomic.
  G_s32SysTickSyncAdj = s32Err;
}

/*!---------------------------------------------------------------------------------------------------------------------
@fn void SystemTimeCheck(void)

@brief Checks for violations of the 1ms system loop time.

Requires:
- Should be called only once in the main system loop

Promises:
@Bsp_u32TimingViolationsCounter is incremented if G_u32SystemTime1ms has
increased by more than one since this function was last called

*/
void SystemTimeCheck(void) {
  static u32 u32PreviousSystemTick = 0;

  /* Check system timing */
  if ((G_u32SystemTime1ms - u32PreviousSystemTick) != 1) {
    /* Flag, count and optionally display warning */
    Bsp_u32TimingViolationsCounter++;
    G_u32SystemFlags |= _SYSTEM_TIME_WARNING;

    if (G_u32DebugFlags & _DEBUG_TIME_WARNING_ENABLE) {
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
void SystemSleep(void) {
  /* Set the system control register for Sleep (but not Deep Sleep) */
  PMC->PMC_FSMR &= ~PMC_FSMR_LPM;
  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

  /* Set the sleep flag (cleared only in SysTick ISR */
  G_u32SystemFlags |= _SYSTEM_SLEEPING;

  /* Now enter the selected LPM */
  while (G_u32SystemFlags & _SYSTEM_SLEEPING) {
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
void PWMSetupAudio(void) {
  /* Set all initialization values */
  PWM->PWM_CLK = PWM_CLK_INIT;
  PWM->PWM_IER2 = PWM_IER2_ENDTX;
  PWM->PWM_SCM = PWM_SCM_INIT;

  PWM->PWM_CH_NUM[0].PWM_CMR = PWM_CMR0_INIT;
  PWM->PWM_CH_NUM[0].PWM_CPRD = PWM_CPRD0_INIT;    /* Set current frequency */
  PWM->PWM_CH_NUM[0].PWM_CPRDUPD = PWM_CPRD0_INIT; /* Latch CPRD values */
  PWM->PWM_CH_NUM[0].PWM_CDTY = PWM_CDTY0_INIT;    /* Set 0% duty */
  PWM->PWM_CH_NUM[0].PWM_CDTYUPD = PWM_CDTY0_INIT; /* Latch CDTY values */

  PWM->PWM_CH_NUM[1].PWM_CMR = PWM_CMR0_INIT;
  PWM->PWM_CH_NUM[1].PWM_CPRD = PWM_CPRD0_INIT;    /* Set current frequency */
  PWM->PWM_CH_NUM[1].PWM_CPRDUPD = PWM_CPRD0_INIT; /* Latch CPRD values */
  PWM->PWM_CH_NUM[1].PWM_CDTY = PWM_CDTY0_INIT;    /* Set 0% duty */
  PWM->PWM_CH_NUM[1].PWM_CDTYUPD = PWM_CDTY0_INIT; /* Latch CDTY values */

  // Allow the PDC to be used for transfers.
  PWM->PWM_PTCR = PERIPH_PTCR_TXTEN;

  NVIC_ClearPendingIRQ(PWM_IRQn);
} /* end PWMSetupAudio() */

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

typedef enum {
  NO_DMA_ACTION,
  FRAME_QUEUED,
  FRAME_COMPLETED,
  UNDERRUN,
  FRAMES_ABORTED,
} DmaAction;

static struct {
  DmaAction eAction;
  u32 u32SysTime;
  u32 u32SysTick;
} astDmaLog[64];

static u8 u8LogIdx;

static inline void LogDma(DmaAction eAction) {
  u32 prim = __get_PRIMASK();
  __disable_irq();
  u8 u8Idx = u8LogIdx++ & 63;
  astDmaLog[u8Idx].u32SysTick = SysTick->VAL;
  astDmaLog[u8Idx].eAction = eAction;
  astDmaLog[u8Idx].u32SysTime = G_u32SystemTime1ms;
  __set_PRIMASK(prim);
}

/**
 * @brief Queue a chunk of samples to play with the buzzer
 *
 * @param pstDma_ A DMA buffer holding the sample values to play. It should be a series of 16-bit unsigned integers,
 * one per sample. (They are used directly as the duty-cycle value by the peripheral). These are relative to the
 * sample period. This means that a sample with value >= u32SamplePeriod is effectively 100%.
 *
 * @param u32SamplePeriod The time of one sample period in system clock ticks.
 */
bool PWMAudioSendFrame(DmaInfo *pstDma_, u16 u16SamplePeriod) {
  if (u16SamplePeriod == 0 || !pstDma_ || !pstDma_->pvBuffer || (pstDma_->u16XferLen & 0x1)) {
    return FALSE;
  }

  if (Bsp_pstNextPwm) {
    return FALSE;
  }

  pstDma_->eStatus = DMA_ACTIVE;

  __disable_irq();
  LogDma(FRAME_QUEUED);
  PWM->PWM_CH_NUM[0].PWM_CPRDUPD = PWM_CPRDUPD_CPRDUPD(u16SamplePeriod);
  // Learned the hard way: Cannot trust the PWM_SR to be up to date. Enable/disable takes some time.
  // However it will always honor the most recent request, so all we need to do here is say to keep
  // going and fill in the missing steps the IRQ would have normally done.
  if (Bsp_pstCurrPwm == NULL) {
    Bsp_pstCurrPwm = pstDma_;
    // Replicate everything that would normally be handled by the interrupt.
    PWM->PWM_TPR = (u32)pstDma_->pvBuffer;
    PWM->PWM_TCR = PWM_TCR_TXCTR(pstDma_->u16XferLen >> 1);

    PWM->PWM_SCUC = PWM_SCUC_UPDULOCK;

    // Start at the right period if the channel was fully disabled. We might miss one period worth
    // in really rare cases, but that's probably okay.
    if (!(PWM->PWM_SR & PWM_SR_CHID0)) {
      PWM->PWM_CH_NUM[0].PWM_CPRD = PWM_CPRD_CPRD(u16SamplePeriod);
    }

    NVIC_ClearPendingIRQ(PWM_IRQn);
    NVIC_EnableIRQ(PWM_IRQn);
    PWM->PWM_ENA = PWM_ENA_CHID0;
  } else {
    Bsp_pstNextPwm = pstDma_;
  }
  __enable_irq();

  return TRUE;
} /* end PWMAudioSendWave */

void PWMAbortAudio(void) {
  DmaInfo *pstCurr;
  DmaInfo *pstNext;

  __disable_irq();
  LogDma(FRAMES_ABORTED);
  PWM->PWM_DIS = PWM_DIS_CHID0;
  PWM->PWM_TCR = 0;
  NVIC_DisableIRQ(PWM_IRQn);

  pstCurr = Bsp_pstCurrPwm;
  Bsp_pstCurrPwm = NULL;
  pstNext = Bsp_pstNextPwm;
  Bsp_pstNextPwm = NULL;
  __enable_irq();

  if (pstCurr) {
    pstCurr->eStatus = DMA_ABORTED;
    if (pstCurr->OnCompleteCb) {
      pstCurr->OnCompleteCb(pstCurr);
    }
  }

  if (pstNext) {
    pstNext->eStatus = DMA_ABORTED;
    if (pstNext->OnCompleteCb) {
      pstNext->OnCompleteCb(pstNext);
    }
  }
} /* end PWMAbortAudio */

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/
volatile u32 G_u32BspPwmUnderruns;

void PWM_IrqHandler(void) {
  // Do a single read because some events/interrupts are cleared on read.
  u32 u32Status = PWM->PWM_ISR2;

  if (u32Status & PWM_ISR2_ENDTX) {
    DmaInfo *pstDma = Bsp_pstCurrPwm;
    Bsp_pstCurrPwm = Bsp_pstNextPwm;
    Bsp_pstNextPwm = NULL;

    // Latch in the new period for the next block, or stop any output if a new frame was not ready.
    if (Bsp_pstCurrPwm) {
      PWM->PWM_SCUC = PWM_SCUC_UPDULOCK;
      PWM->PWM_TPR = (u32)Bsp_pstCurrPwm->pvBuffer;
      PWM->PWM_TCR = PWM_TCR_TXCTR(Bsp_pstCurrPwm->u16XferLen >> 1);
    } else {
      PWM->PWM_DIS = PWM_DIS_CHID0;
      NVIC_DisableIRQ(PWM_IRQn);
      LogDma(UNDERRUN);
      G_u32BspPwmUnderruns += 1;
    }

    if (pstDma) {
      LogDma(FRAME_COMPLETED);
      pstDma->eStatus = DMA_COMPLETE;
      if (pstDma->OnCompleteCb) {
        pstDma->OnCompleteCb(pstDma);
      }
    }
  }
}

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/