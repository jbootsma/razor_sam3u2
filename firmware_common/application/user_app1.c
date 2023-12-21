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
 8. Add a call to YourNewTaskNameRunActiveState() in the Super Loop section of main
 9. Update yournewtaskname.h per the instructions at the top of yournewtaskname.h
10. Delete this text (between the dashed lines) and update the Description below to describe your task
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
/* Existing variables (defined in other files -- should all contain the "extern" keyword) */
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

static volatile u16 UserApp1_u16LastSample;

/**********************************************************************************************************************
Function Definitions
**********************************************************************************************************************/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @protectedsection */
/*--------------------------------------------------------------------------------------------------------------------*/

static void UpdateSample(u16 u16SampleVal) { UserApp1_u16LastSample = u16SampleVal; }

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
  Adc12AssignCallback(ADC12_POTENTIOMETER, UpdateSample);

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

All state machines have a TOTAL of 1ms to execute, so on average n state machines
may take 1ms / n to execute.

Requires:
- State machine function pointer points at current state

Promises:
- Calls the function to pointed by the state machine function pointer

*/
void UserApp1RunActiveState(void) { UserApp1_pfStateMachine(); } /* end UserApp1RunActiveState */

/*------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/

/**********************************************************************************************************************
State Machine Function Definitions
**********************************************************************************************************************/
/*-------------------------------------------------------------------------------------------------------------------*/
/* What does this state do? */
static void UserApp1SM_Idle(void) {
  static bool first_tick = TRUE; // Give ADC12 time to startup.

  static u8 u8Framecount = 100;
  static u8 u8DisplayedPct = 255;

  if (--u8Framecount == 0) {
    u8Framecount = 100; // 10 Hz.

    u32 sample = UserApp1_u16LastSample;
    sample = (sample * 100 + 2048) / 4096; // Convert to %

    if (sample != u8DisplayedPct) {
      u8DisplayedPct = sample;
      char line[4];

      u8 tmp = u8DisplayedPct;
      u8 idx = 0;

      line[idx++] = '0' + tmp / 100;
      tmp -= (tmp / 100) * 100;

      line[idx++] = '0' + tmp / 10;
      tmp -= (tmp / 10) * 10;

      line[idx++] = '0' + tmp;
      line[idx] = '\0';

      LcdMessage(LINE1_START_ADDR, line);
    }

    sample = (sample + 2) / 5; // Convert to PWM level.

    LedPWM(LCD_BLUE, LED_PWM_100 - sample);
    LedPWM(LCD_GREEN, LED_PWM_0 + sample);
  }

  if (!first_tick) {
    Adc12StartConversion(ADC12_POTENTIOMETER);
  }

  first_tick = FALSE;
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File                                                                                                        */
/*--------------------------------------------------------------------------------------------------------------------*/
