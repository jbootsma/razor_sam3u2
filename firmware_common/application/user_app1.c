/*!*********************************************************************************************************************
@file user_app1.c
@brief User's tasks / applications are written here.  This description
should be replaced by something specific to the task.

----------------------------------------------------------------------------------------------------------------------
To start a new task using this user_app1 as a template:
 1. Copy both user_app1.c and user_app1.h to the Application directory
 2. Rename the files yournewtaskname.c and yournewtaskname.h
 3. Add yournewtaskname.c and yournewtaskname.h to the Application Include and Source groups in the IAR project
 4. Use ctrl-h (make sure "Match Case" is checked) to find and replace all instances of "user_app1" with
"yournewtaskname"
 5. Use ctrl-h to find and replace all instances of "UserApp1" with "YourNewTaskName"
 6. Use ctrl-h to find and replace all instances of "USER_APP1" with "YOUR_NEW_TASK_NAME"
 7. Add a call to YourNewTaskNameInitialize() in the init section of main
 8. Add a call to YourNewTaskNameRunActiveState() in the Super Loop section of
main
 9. Update yournewtaskname.h per the instructions at the top of
yournewtaskname.h
10. Delete this text (between the dashed lines) and update the Description below
to describe your task
----------------------------------------------------------------------------------------------------------------------

------------------------------------------------------------------------------------------------------------------------
GLOBALS
- NONE

CONSTANTS
- NONE

TYPES
- NONE

PUBLIC FUNCTIONS
- NONE

PROTECTED FUNCTIONS
- void UserApp1Initialize(void)
- void UserApp1RunActiveState(void)


**********************************************************************************************************************/

#include "configuration.h"

/***********************************************************************************************************************
Global variable definitions with scope across entire project.
All Global variable names shall start with "G_<type>UserApp1"
***********************************************************************************************************************/
/* New variables */
volatile u32 G_u32UserApp1Flags; /*!< @brief Global state flags */

/*--------------------------------------------------------------------------------------------------------------------*/
/* Existing variables (defined in other files -- should all contain the "extern"
 * keyword) */
extern volatile u32 G_u32SystemTime1ms;    /*!< @brief From main.c */
extern volatile u32 G_u32SystemTime1s;     /*!< @brief From main.c */
extern volatile u32 G_u32SystemFlags;      /*!< @brief From main.c */
extern volatile u32 G_u32ApplicationFlags; /*!< @brief From main.c */

/***********************************************************************************************************************
Global variable definitions with scope limited to this local application.
Variable names shall start with "UserApp1_<type>" and be declared as static.
***********************************************************************************************************************/
static fnCode_type UserApp1_pfStateMachine; /*!< @brief The state machine function pointer */
// static u32 UserApp1_u32Timeout;                           /*!< @brief Timeout counter used across states */

#define U16_SAMPLE_BUF_SIZE 256

static u16 UserApp1_au16Samples[U16_SAMPLE_BUF_SIZE];
static u16 UserApp1_au16SampleRate = 100;

/**********************************************************************************************************************
Function Definitions
**********************************************************************************************************************/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @protectedsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*!--------------------------------------------------------------------------------------------------------------------
@fn void UserApp1Initialize(void)

@brief
Initializes the State Machine and its variables.

Should only be called once in main init section.

Requires:
- NONE

Promises:
- NONE

*/
void UserApp1Initialize(void) {
  LedPWM(LCD_BLUE, LED_PWM_100);
  LedPWM(LCD_GREEN, LED_PWM_0);
  LedOff(LCD_RED);

  LcdClearChars(LINE1_START_ADDR, U8_LCD_MAX_MESSAGE_SIZE);
  LcdClearChars(LINE2_START_ADDR, U8_LCD_MAX_MESSAGE_SIZE);
  LcdCommand(LCD_DISPLAY_CMD | LCD_DISPLAY_ON);

  /* If good initialization, set state to Idle */
  if (1) {
    UserApp1_pfStateMachine = UserApp1SM_Idle;
  } else {
    /* The task isn't properly initialized, so shut it down and don't run */
    UserApp1_pfStateMachine = UserApp1SM_Error;
  }

} /* end UserApp1Initialize() */

/*!----------------------------------------------------------------------------------------------------------------------
@fn void UserApp1RunActiveState(void)

@brief Selects and runs one iteration of the current state in the state machine.

All state machines have a TOTAL of 1ms to execute, so on average n state
machines may take 1ms / n to execute.

Requires:
- State machine function pointer points at current state

Promises:
- Calls the function to pointed by the state machine function pointer

*/
void UserApp1RunActiveState(void) { UserApp1_pfStateMachine(); } /* end UserApp1RunActiveState */

/*------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/

static u8 LcdNumMessage(u8 u8Addr, u32 u32Num) {
  char line[11];
  line[10] = '\0';

  u32 idx = 9;
  line[idx] = '0';

  while (u32Num != 0) {
    line[idx--] = '0' + u32Num % 10;
    u32Num /= 10;
  }

  // Always show at least one character.
  if (idx != 9) {
    idx += 1;
  }

  LcdMessage(u8Addr, &line[idx]);
  return 10 - idx;
}

/**********************************************************************************************************************
State Machine Function Definitions
**********************************************************************************************************************/
/*-------------------------------------------------------------------------------------------------------------------*/
/* What does this state do? */
static void UserApp1SM_Idle(void) {
  static u8 u8DisplayedPct = 255;
  static u32 u32SamplesThisSecond = 0;
  static u16 u16SecondCounter = 1000;
  static u8 u8FrameCounter = 100;
  static u16 u16LastSample = 0;

  if (Adc12IsIdle()) {
    Adc12StartContinuousSampling(ADC12_POTENTIOMETER, UserApp1_au16SampleRate, UserApp1_au16Samples,
                                 U16_SAMPLE_BUF_SIZE);
  } else {
    if (Adc12CheckOverrun()) {
      LedOn(YELLOW);
    } else {
      LedOff(YELLOW);
    }
  }

  if (WasButtonPressed(BUTTON1)) {
    ButtonAcknowledge(BUTTON1);
    Adc12StopContinuousSampling();
    UserApp1_au16SampleRate *= 1.1;
  } else if (WasButtonPressed(BUTTON0)) {
    ButtonAcknowledge(BUTTON0);
    Adc12StopContinuousSampling();
    UserApp1_au16SampleRate *= 0.9;
  }

  u16 au16Samples[128];
  u16 u16SampleCount = Adc12GetSamples(au16Samples, 128);

  if (u16SampleCount != 0) {
    u32SamplesThisSecond += u16SampleCount;
    u16LastSample = au16Samples[u16SampleCount - 1];
  }

  if (--u8FrameCounter == 0) {
    u8FrameCounter = 100;
    u32 u32Sample = u16LastSample;               // Use 32 bits to avoid overflow during multiplication.
    u32Sample = (u32Sample * 100 + 2048) / 4096; // Convert to %

    if (u32Sample != u8DisplayedPct) {
      u8DisplayedPct = u32Sample;
      LcdClearChars(LINE1_START_ADDR, U8_LCD_MAX_LINE_DISPLAY_SIZE);
      LcdNumMessage(LINE1_START_ADDR, u8DisplayedPct);
    }

    u32Sample = (u32Sample + 2) / 5; // Convert to PWM level.

    LedPWM(LCD_BLUE, LED_PWM_100 - u32Sample);
    LedPWM(LCD_GREEN, LED_PWM_0 + u32Sample);
  }

  if (--u16SecondCounter == 0) {
    u16SecondCounter = 1000;

    LcdClearChars(LINE2_START_ADDR, U8_LCD_MAX_LINE_DISPLAY_SIZE);

    u8 numSz = LcdNumMessage(LINE2_START_ADDR, UserApp1_au16SampleRate);

    LcdNumMessage(LINE2_START_ADDR + numSz + 1, u32SamplesThisSecond);
    u32SamplesThisSecond = 0;

    Adc12ClearOverrun();
  }
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
