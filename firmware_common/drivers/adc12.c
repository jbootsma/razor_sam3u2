/*!**********************************************************************************************************************
@file adc12.c
@brief 12-bit ADC driver and API

Driver function to give access to the 12-bit ADC on the EiE development boards.
The ADC hardware is the same for the EiE 1 and EiE 2 development board Blade connectors.
The EiE1 board has an additional on-board potentiometer for testing purporses.

This driver currently only provides setup and single result read access from any
channel on the ADC at a time.  Any averaging or special operations should be handled by the
application using the driver.  This driver is set up as a state machine for future
feature additions.

The first sample tends to read 20-30 bits high.  If no sample is taken for a few minutes,
the next first sample will also read high.  This implies a long time constant in the hold
time, but the timing parameters that have been set all line up with the electrical
characteristics and source impedence considerations.  So this is a mystery for
now -- suggest the first sample is thrown out, or average it out with at least 16 samples
per displayed result which will reduce the error down to 1 or 2 LSBs.

------------------------------------------------------------------------------------------------------------------------
GLOBALS
- NONE

CONSTANTS
- NONE

TYPES
- Adc12ChannelType {ADC12_CH0 ... ADC12_CH7}

PUBLIC FUNCTIONS
- void Adc12AssignCallback(Adc12ChannelType eAdcChannel_, fnCode_u16_type pfUserCallback_)
- bool Adc12StartConversion(Adc12ChannelType eAdcChannel_)

PROTECTED FUNCTIONS
- void Adc12Initialize(void)
- void Adc12RunActiveState(void)
- void ADCC0_IrqHandler(void)

**********************************************************************************************************************/

#include "configuration.h"

/***********************************************************************************************************************
Global variable definitions with scope across entire project.
All Global variable names shall start with "G_<type>Adc12"
***********************************************************************************************************************/
/* New variables */
volatile u32 G_u32Adc12Flags; /*!< @brief Global state flags */

/*--------------------------------------------------------------------------------------------------------------------*/
/* Existing variables (defined in other files -- should all contain the "extern" keyword) */
extern volatile u32 G_u32SystemTime1ms;    /*!< @brief From main.c */
extern volatile u32 G_u32SystemTime1s;     /*!< @brief From main.c */
extern volatile u32 G_u32SystemFlags;      /*!< @brief From main.c */
extern volatile u32 G_u32ApplicationFlags; /*!< @brief From main.c */

/***********************************************************************************************************************
Global variable definitions with scope limited to this local application.
Variable names shall start with "Adc12_<type>" and be declared as static.
***********************************************************************************************************************/
static fnCode_type Adc12_pfnStateMachine; /*!< @brief The state machine function pointer */

static volatile Adc12ChannelType Adc12_eActiveChannel; /*!< Track which channel is being used.*/
static volatile bool Adc12_bStopRequested; /*!< @brief TRUE if the ADC should stop after the next sample is completed*/

static Adc12ChannelType Adc12_aeChannels[] =
    ADC_CHANNEL_ARRAY;                        /*!< @brief Available channels defined in configuration.h */
static fnCode_u16_type Adc12_apfCallbacks[8]; /*!< @brief ADC12 ISR callback function pointers */

/* Variables related to the continuous sampling buffer. */

static u16 *Adc12_pu16SampleBuffer;     /*!< @brief Buffer to store samples in during continuous sampling. */
static u16 Adc12_u16SampleBufSize;      /*!< @brief Size of the sample buffer in samples.*/
static volatile bool Adc12_bOverrunErr; /*!< @brief Track if any samples were discarded due to buffer overrun. */

/*
The head and tail pointers to use the sample buffer as a FIFO. If Head == Tail then the fifo is empty.
The head is only updated by the IRQ, and the tail only by application code. When accessing these pointers care must be
taken to use appropriate memory barriers around their writes/reads as otherwise the compiler/caches may re-order
accesses leading to corrupted sample data. The general guidance is:
- Use a DMB after writing/reading fifo contents and before updating the pointer.
- Use a DMB after reading a pointer that may be updated elsewhere and before accessing buffer contents.
*/
static u16 Adc12_u16Head; /*!< @brief Offset of next sample position to save in the buffer. */
static u16 Adc12_u16Tail; /*!< @brief Offset of oldest sample in the buffer. */

/**********************************************************************************************************************
Function Definitions
**********************************************************************************************************************/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*!---------------------------------------------------------------------------------------------------------------------
@brief Check if the ADC12 is available for use

@returns TRUE if the ADC12 is currently idle and can be used to start new sampling.
*/
bool Adc12IsIdle(void) { return Adc12_eActiveChannel == _ADC12_CH_INVLD; } /* Adc12IsIdle */

/*!---------------------------------------------------------------------------------------------------------------------
@fn void Adc12AssignCallback(Adc12ChannelType eAdcChannel_, fnCode_u16_type pfUserCallback_)

@brief Assigns callback for the client application.

This is how the ADC result for any channel is accessed.  The callback function
must have one u16 parameter where the result is passed.  Define the function that
will be used for the callback, then assign this during user task initialization.

Different callbacks may be assigned for each channel.

*** To mitigate the chance of indefinitely holding control of
the ADC resource, new conversions shall not be started in this callback. ***

Example:

void UserApp_AdcCallback(u16 u16Result_);

void UserApp1Initialize(void)
{
 Adc12AssignCallback(ADC12_BLADE_AN0, UserApp_AdcCallback);
}


Requires:
@param eAdcChannel_ is the channel to which the callback will be assigned
@param pfUserCallback_ is the function address (name) for the user's callback

Promises:
- Adc12_fpCallbackCh<eAdcChannel_> ADC global value loaded with pfUserCallback_

*/
void Adc12AssignCallback(Adc12ChannelType eAdcChannel_, fnCode_u16_type pfUserCallback_) {
  bool bChannelValid = FALSE;

  /* Check to ensure the requested channel exists */
  for (u8 i = 0; i < (sizeof(Adc12_aeChannels) / sizeof(Adc12ChannelType)); i++) {
    if (Adc12_aeChannels[i] == eAdcChannel_) {
      bChannelValid = TRUE;
    }
  }

  /* If the channel is valid, then assign the new callback function */
  if (bChannelValid) {
    Adc12_apfCallbacks[eAdcChannel_] = pfUserCallback_;
  } else {
    DebugPrintf("Invalid channel\n\r");
  }

} /* end Adc12AssignCallback() */

/*!---------------------------------------------------------------------------------------------------------------------
@fn bool Adc12StartConversion(Adc12ChannelType eAdcChannel_)

@brief Checks if the ADC is available and starts the conversion on the selected channel.

Returns TRUE if the conversion is started; returns FALSE if the ADC is not available.

Example:

bool bConversionStarted = FALSE;

bConversionStarted = Adc12StartConversion(ADC12_CH2);


Requires:
- Adc12IsIdle indicates if the ADC is available for a conversion

@param eAdcChannel_ the ADC12 channel to disable

Promises:

If Adc12IsIdle is TRUE:
- Adc12IsIdle changed to FALSE
- ADC12B_CHER bit for eAdcChannel_ is set
- ADC12B_IER bit for eAdcChannel_is set
- Returns TRUE

If Adc12IsIdle is FALSE:
- Returns FALSE

*/
bool Adc12StartConversion(Adc12ChannelType eAdcChannel_) {
  if (Adc12IsIdle()) {
    /* Take the semaphore so we have the ADC resource.  Since this is a binary semaphore
    that is only cleared in the ISR, it is safe to do this with interrupts enabled */
    Adc12_eActiveChannel = eAdcChannel_;

    /* Enable the channel and its interrupt */
    AT91C_BASE_ADC12B->ADC12B_CHER = (1 << eAdcChannel_);
    AT91C_BASE_ADC12B->ADC12B_IER = (1 << eAdcChannel_);

    /* Start the conversion and exit */
    Adc12_bStopRequested = TRUE; // One-shot, stop right away.
    AT91C_BASE_ADC12B->ADC12B_CR |= AT91C_ADC12B_CR_START;
    return TRUE;
  }

  /* The ADC is not available */
  return FALSE;

} /* end Adc12StartConversion() */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Start sampling a channel at a specific sample rate

@param eAdcChannel_ The channel to start sampling.
@param u16SampleRateHz_ The number of times per second to sample the channel.
@param sSampleBuf_ The buffer that will be used to hold sample data as it is received.

Once sampling is started samples will be recorded into the provided buffer. They can be extracted
using Adc12GetSamples(). If the application fails to extract samples fast enough then the buffer
will be filled up and an overrun error will occur. Any samples received while the buffer is full
will be discarded.

Sampling continues until Adc12StopContinuousSampling() is called.

@requires That the ADC is currently idle.
@requires That the provided buffer has more than 0 samples.

@return TRUE if sampling was successfully initiated.

@promises The ADC will be sampling the specified channel at the specified rate.
*/
bool Adc12StartContinuousSampling(Adc12ChannelType eAdcChannel_, u16 u16SampleRateHz_, u16 *pu16SampleBuf_,
                                  u16 u16SampleBufLen_) {
  // Argument validation.
  // Sample buf must be at least 2 samples, as at most N-1 samples can be in the buffer at any time due to how the
  // head/tail pointers are used.
  // The sample rate can't be slower than 6 Hz either, as the timer used doesn't have enough bits to
  // count that out at 48MHz. (Could use SCLK, but that's an entire other branch of math).
  if (u16SampleRateHz_ < 6 || pu16SampleBuf_ == NULL || u16SampleBufLen_ < 2) {
    return FALSE;
  }

  // State validation.
  if (!Adc12IsIdle()) {
    return FALSE;
  }

  // Everything is good, okay to setup sampling.
  Adc12_eActiveChannel = eAdcChannel_;

  Adc12_pu16SampleBuffer = pu16SampleBuf_;
  Adc12_u16SampleBufSize = u16SampleBufLen_;
  Adc12_u16Head = 0;
  Adc12_u16Tail = 0;
  Adc12_bOverrunErr = FALSE;

  Adc12AssignCallback(eAdcChannel_, Adc12ContinuousCallback);

  // First need the T2 timer setup and running. Should go through a timer API but haven't had time
  // to expand the driver interface yet. This depends on the timer defaults.

  u32 u32MclksPerSample = (MCK + u16SampleRateHz_ / 2) / u16SampleRateHz_;

  // Pick fastest clock with the range we need.
  u32 u32TicksPerSample = u32MclksPerSample / 2;
  u32 clk = AT91C_TC_CLKS_TIMER_DIV1_CLOCK;

  while (u32TicksPerSample >= 0x10000) {
    u32TicksPerSample /= 4;
    clk += (AT91C_TC_CLKS_TIMER_DIV2_CLOCK - AT91C_TC_CLKS_TIMER_DIV1_CLOCK);
  }

  // The halfway point will be the actual leading edge for samples.
  u32 u32HalfCount = u32TicksPerSample / 2;

  AT91C_BASE_TC2->TC_CMR &= ~AT91C_TC_CLKS;
  AT91C_BASE_TC2->TC_CMR |= clk;
  AT91C_BASE_TC2->TC_RA = (u16)u32HalfCount;
  AT91C_BASE_TC2->TC_RC = (u16)u32TicksPerSample;

  // Allow the ADC to be triggered by the timer.
  AT91C_BASE_ADC12B->ADC12B_MR |= AT91C_ADC12B_MR_TRGEN;

  // Enable the channel and it's data ready interrupt.
  AT91C_BASE_ADC12B->ADC12B_CHER = (1 << eAdcChannel_);
  AT91C_BASE_ADC12B->ADC12B_IER = (1 << eAdcChannel_);

  // Now that everything setup we can start the timer.
  AT91C_BASE_TC2->TC_CCR = AT91C_TC_CLKEN | AT91C_TC_SWTRG;

  return TRUE;
} /* end Adc12StartContinuousSampling */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Stop the continuous sampling process

Stops any ongoing continuous sampling process. This is an asynchronous operation, the application
must use Adc12IsIdle() to determine when the sampling has actually stopped, though it is safe to
assume the sample buffer is no longer used after this function returns.

@promises The Adc will eventually be idle.
@promises The previously supplied sample buffer will no longer be written to.
*/
void Adc12StopContinuousSampling(void) {
  if (Adc12_pu16SampleBuffer == NULL) {
    // No sampling going on.
    return;
  }

  // Disabling the interrupt here ensures that the sample recording buffer can be safely reclaimed.
  NVIC_DisableIRQ(IRQn_ADCC0);
  Adc12AssignCallback(Adc12_eActiveChannel, Adc12DefaultCallback);
  Adc12_bStopRequested = TRUE;
  NVIC_EnableIRQ(IRQn_ADCC0);

  // The remainder of cleanup occurs in the IRQ.
} /* end Adc12StopContinuousSampling */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Check if the overrun condition has occured

@returns True if at least one sample has been discarded due to overrun since the last time the
condition was cleared.

The overrun condition is automatically cleared any time continuous sampling is started. The condition
can also be cleared manually using Adc12ClearOverrun().
*/
bool Adc12CheckOverrun(void) { return Adc12_bOverrunErr; } /* end Adc12CheckOverrun */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Clear the overrun condition

Clears the overrun condition flag. If a new overrun is detected the flag will be set again.
*/
void Adc12ClearOverrun(void) { Adc12_bOverrunErr = FALSE; } /* end Adc12ClearOverrun */

/*!---------------------------------------------------------------------------------------------------------------------
@brief Copy available samples from the continuous sampling buffer

@param sDestBuffer_ The buffer that samples will be copied into.

If there are available samples in the continuous sampling buffer this will copy them into the provided
destination buffer. This must be called regularly to ensure there is room in the sample buffer for
new samples.

@returns The number of samples actually copied.

@promises The number of samples copied will be the lesser of the number of samples in the internal
          buffer, or the number that will fit in the destination buffer. (This means that if the
          return is less than the dest buffer size all available samples have been copied).
*/
u16 Adc12GetSamples(u16 *pu16Dest_, u16 u16NumSamples_) {
  // Capture local copies of the pointers to determine how many samples are definitely valid (it's
  // possible for more to be recorded while this method runs).
  u16 u16Tail = Adc12_u16Tail;
  u16 u16Head = Adc12_u16Head;
  u16 u16Copied = 0;

  __DMB();

  // Quit if we run out of room.
  while (u16NumSamples_ > 0) {
    u16 u16ChunkStart = u16Tail;
    u16 u16ChunkEnd = u16Head;

    // If valid area wraps around, just deal with the part before wraparound.
    if (u16ChunkEnd < u16ChunkStart) {
      u16ChunkEnd = Adc12_u16SampleBufSize;
    }

    u16 u16Len = u16ChunkEnd - u16ChunkStart;
    if (u16Len == 0) {
      // There's no more samples available.
      break;
    }

    if (u16Len > u16NumSamples_) {
      u16Len = u16NumSamples_; // Don't overrun the destination.
    }

    memcpy(pu16Dest_, &Adc12_pu16SampleBuffer[u16ChunkStart], sizeof(u16) * u16Len);
    pu16Dest_ += u16Len;
    u16NumSamples_ -= u16Len;
    u16Copied += u16Len;

    u16Tail += u16Len;
    if (u16Tail == Adc12_u16SampleBufSize) {
      u16Tail = 0;
    }

    __DMB();
    // Make sure the actual tail is updated, to allow more samples to be recorded.
    Adc12_u16Tail = u16Tail;

    // If we consumed all samples there's no more work to do.
    if (u16Tail == u16Head) {
      break;
    }
  }

  return u16Copied;
} /* end AdcGetSamples */

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @protectedsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*!--------------------------------------------------------------------------------------------------------------------
@fn void Adc12Initialize(void)

@brief Runs required initialization for the task.

Should only be called once in main init section.

Requires:
- NONE

Promises:
- The ADC-12 peripheral is configured
- ADC interrupt is enabled
- Adc12_pfnStateMachine set to Idle

*/
void Adc12Initialize(void) {
  u8 au8Adc12Started[] = "ADC12 task initialized\n\r";

  /* Initialize peripheral registers. ADC starts totally disabled. */
  AT91C_BASE_ADC12B->ADC12B_MR = ADC12B_MR_INIT;
  AT91C_BASE_ADC12B->ADC12B_CHDR = ADC12B_CHDR_INIT;
  AT91C_BASE_ADC12B->ADC12B_ACR = ADC12B_ACR_INIT;
  AT91C_BASE_ADC12B->ADC12B_EMR = ADC12B_EMR_INIT;
  AT91C_BASE_ADC12B->ADC12B_IDR = ADC12B_IDR_INIT;

  /* Set all the callbacks to default */
  for (u8 i = 0; i < (sizeof(Adc12_apfCallbacks) / sizeof(fnCode_u16_type)); i++) {
    Adc12_apfCallbacks[i] = Adc12DefaultCallback;
  }

  /* Mark the ADC semaphore as available */
  Adc12_eActiveChannel = _ADC12_CH_INVLD;

  /* Check initialization and set first state */
  if (1) {
    /* Enable required interrupts */
    NVIC_ClearPendingIRQ(IRQn_ADCC0);
    NVIC_EnableIRQ(IRQn_ADCC0);

    /* Write message, set "good" flag and select Idle state */
    DebugPrintf(au8Adc12Started);
    G_u32ApplicationFlags |= _APPLICATION_FLAGS_ADC;
    Adc12_pfnStateMachine = Adc12SM_Idle;
  } else {
    /* The task isn't properly initialized, so shut it down and don't run */
    Adc12_pfnStateMachine = Adc12SM_Error;
  }

} /* end Adc12Initialize() */

/*!----------------------------------------------------------------------------------------------------------------------
@fn void Adc12RunActiveState(void)

@brief Selects and runs one iteration of the current state in the state machine.

All state machines have a TOTAL of 1ms to execute, so on average n state machines
may take 1ms / n to execute.

Requires:
- State machine function pointer points at current state

Promises:
- Calls the function to pointed by the state machine function pointer

*/
void Adc12RunActiveState(void) { Adc12_pfnStateMachine(); } /* end Adc12RunActiveState */

/*!----------------------------------------------------------------------------------------------------------------------
@fn void ADCC0_IrqHandler(void)

@brief Parses the ADC12 interrupts and handles them appropriately.

Note that all ADC12 interrupts are ORed and will trigger this handler, therefore
any expected interrupt that is enabled must be parsed out and handled.  There is
no obviously available explanation for why this handler is called ADCC0_IrqHandler
instead of ADC12B_IrqHandler

Requires:
- Only one channel can be converting at a time, so only one interrupt flag
  will be set.

*/
void ADCC0_IrqHandler(void) {
  u16 u16Adc12Result;

  /* WARNING: if you step through this handler with the ADC12B registers
  debugging, the debugger reads ADC12B_SR and clears the EOC flag bits */

  // Since only 1 channel is enabled at a time we can just check it directly.
  if (AT91C_BASE_ADC12B->ADC12B_SR & (1 << Adc12_eActiveChannel)) {
    /* Read the channel's result register (clears EOC bit / interrupt) and send to callback */
    u16Adc12Result = AT91C_BASE_ADC12B->ADC12B_CDR[Adc12_eActiveChannel];
    Adc12_apfCallbacks[Adc12_eActiveChannel](u16Adc12Result);

    if (Adc12_bStopRequested) {
      // Time to cleanup, either this was a 1-shot sample or continuous sampling has been aborted.
      Adc12_bStopRequested = FALSE;

      // No more channel sample/interrupts.
      AT91C_BASE_ADC12B->ADC12B_CHDR = (1 << Adc12_eActiveChannel);
      AT91C_BASE_ADC12B->ADC12B_IDR = (1 << Adc12_eActiveChannel);

      // Stop the hardware trigger.
      AT91C_BASE_TC2->TC_CCR = AT91C_TC_CLKDIS;
      AT91C_BASE_ADC12B->ADC12B_MR &= ~AT91C_ADC12B_MR_TRGEN;

      /* Give the Semaphore back, clear the ADC pending flag and exit */
      Adc12_eActiveChannel = _ADC12_CH_INVLD;
      NVIC_ClearPendingIRQ(IRQn_ADCC0);
    }
  }
} /* end ADCC0_IrqHandler() */

/*----------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*----------------------------------------------------------------------------------------------------------------------*/

/*!----------------------------------------------------------------------------------------------------------------------
@fn void Adc12DefaultCallback(u16 u16Result_)

@brief An empty function that the unset Adc Callbacks point to.

Expected that the user will set their own.

Requires:
@param u16Result_ Required for the ISR to pass the result to but not used

Promises:
- NONE

*/
void Adc12DefaultCallback(u16 u16Result_) { /* This is an empty function */
} /* end Adc12DefaultCallback() */

/*!----------------------------------------------------------------------------------------------------------------------
@brief Callback used to save a sample into the sample buffer.

Used by Adc12StartContinuousSampling() to record data into the configured sample buffer.

@requires A valid sample buffer has been set up.
@promises The new sample is in the sample buffer, or the overflow flag is set.
*/
void Adc12ContinuousCallback(u16 u16Result_) {
  u16 u16Tail = Adc12_u16Tail;
  u16 u16Head = Adc12_u16Head;
  __DMB();

  Adc12_pu16SampleBuffer[u16Head] = u16Result_;
  u16Head += 1;

  if (u16Head == Adc12_u16SampleBufSize) {
    u16Head = 0;
  }

  if (u16Head == u16Tail) {
    Adc12_bOverrunErr = TRUE;
    // Don't update the head in this case as that would drop all the data received so far.
  } else {
    __DMB();
    Adc12_u16Head = u16Head;
  }
}

/***********************************************************************************************************************
State Machine Declarations
***********************************************************************************************************************/

/*!-------------------------------------------------------------------------------------------------------------------
@fn static void Adc12SM_Idle(void)

@brief Wait for a message to be queued
*/
static void Adc12SM_Idle(void) {} /* end Adc12SM_Idle() */

/*!-------------------------------------------------------------------------------------------------------------------
@fn static void Adc12SM_Error(void)

@brief Handle an error
*/
static void Adc12SM_Error(void) {} /* end Adc12SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File                                                                                                        */
/*--------------------------------------------------------------------------------------------------------------------*/
