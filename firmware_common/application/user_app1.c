/*!*********************************************************************************************************************
@file user_app1.c
@brief User's tasks / applications are written here.  This description
should be replaced by something specific to the task.

----------------------------------------------------------------------------------------------------------------------
To start a new task using this user_app1 as a template:
 1. Copy both user_app1.c and user_app1.h to the Application directory
 2. Rename the files yournewtaskname.c and yournewtaskname.h
 3. Add yournewtaskname.c and yournewtaskname.h to the Application Include and
Source groups in the IAR project
 4. Use ctrl-h (make sure "Match Case" is checked) to find and replace all
instances of "user_app1" with "yournewtaskname"
 5. Use ctrl-h to find and replace all instances of "UserApp1" with
"YourNewTaskName"
 6. Use ctrl-h to find and replace all instances of "USER_APP1" with
"YOUR_NEW_TASK_NAME"
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
static fnCode_type
    UserApp1_pfStateMachine; /*!< @brief The state machine function pointer */
// static u32 UserApp1_u32Timeout;                           /*!< @brief Timeout
// counter used across states */

#define LED_STEP_TIME_MS 40
#define LCD_COLOR_STEPS_PER_BAND 30

static LedRateType aCurrentRates[U8_TOTAL_LEDS];
static LedRateType aTargetRates[U8_TOTAL_LEDS];

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
  for (LedNameType eLed = 0; eLed < U8_TOTAL_LEDS; eLed++) {
    LedPWM(eLed, LED_0HZ);
    aCurrentRates[eLed] = LED_PWM_0;
    aTargetRates[eLed] = LED_PWM_0;
  }

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
void UserApp1RunActiveState(void) {
  UserApp1_pfStateMachine();

} /* end UserApp1RunActiveState */

/*------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/

/**********************************************************************************************************************
State Machine Function Definitions
**********************************************************************************************************************/
/*-------------------------------------------------------------------------------------------------------------------*/
/* What does this state do? */
static void UserApp1SM_Idle(void) {
  static const LedNameType aLedFadeSteps[][2] = {
      // in-to-out
      {CYAN, GREEN},
      {BLUE, YELLOW},
      {PURPLE, ORANGE},
      {WHITE, RED},
      // left ping-pong
      {PURPLE, RED},
      {BLUE, RED},
      {CYAN, RED},
      {GREEN, RED},
      {YELLOW, RED},
      {ORANGE, RED},
      {YELLOW, RED},
      {GREEN, RED},
      {CYAN, RED},
      {BLUE, RED},
      {PURPLE, RED},
      {WHITE, RED},
      // right ping-pong
      {WHITE, ORANGE},
      {WHITE, YELLOW},
      {WHITE, GREEN},
      {WHITE, CYAN},
      {WHITE, BLUE},
      {WHITE, PURPLE},
      {WHITE, BLUE},
      {WHITE, CYAN},
      {WHITE, GREEN},
      {WHITE, YELLOW},
      {WHITE, ORANGE},
      // out-to-in
      {WHITE, RED},
      {PURPLE, ORANGE},
      {BLUE, YELLOW},
  };
  static const u8 u8NumFades = sizeof(aLedFadeSteps) / sizeof(aLedFadeSteps[0]);

  static const LedRateType aLcdColors[][3] = {
      {LED_PWM_100, LED_PWM_0, LED_PWM_0},    // R
      {LED_PWM_100, LED_PWM_50, LED_PWM_0},   // O
      {LED_PWM_100, LED_PWM_100, LED_PWM_0},  // Y
      {LED_PWM_0, LED_PWM_100, LED_PWM_0},    // G
      {LED_PWM_0, LED_PWM_0, LED_PWM_100},    // B
      {LED_PWM_0, LED_PWM_50, LED_PWM_100},   // I
      {LED_PWM_30, LED_PWM_0, LED_PWM_100},   // V
  };
  static const u8 u8NumColors = sizeof(aLcdColors) / sizeof(aLcdColors[0]);

  static u8 u8StepTimer = LED_STEP_TIME_MS;
  static u8 u8ColorTimer = LCD_COLOR_STEPS_PER_BAND;
  static u8 u8FadeIdx = u8NumFades - 1;
  static u8 u8ColorIdx = u8NumColors - 1;

  if (--u8StepTimer == 0) {
    u8StepTimer = LED_STEP_TIME_MS;
    bool bDoneFade = TRUE;
    for (LedNameType eLed = WHITE; eLed <= RED; eLed++) {
      if (aCurrentRates[eLed] < aTargetRates[eLed]) {
        bDoneFade = FALSE;
        aCurrentRates[eLed] += 1;
        LedPWM(eLed, aCurrentRates[eLed]);
      } else if (aCurrentRates[eLed] > aTargetRates[eLed]) {
        bDoneFade = FALSE;
        aCurrentRates[eLed] -= 1;
        LedPWM(eLed, aCurrentRates[eLed]);
      }
    }

    for (LedNameType eLed = LCD_RED; eLed <= LCD_BLUE; eLed++) {
      if (aCurrentRates[eLed] < aTargetRates[eLed]) {
        aCurrentRates[eLed] += 1;
        LedPWM(eLed, aCurrentRates[eLed]);
      } else if (aCurrentRates[eLed] > aTargetRates[eLed]) {
        aCurrentRates[eLed] -= 1;
        LedPWM(eLed, aCurrentRates[eLed]);
      }
    }

    if (bDoneFade) {
      if (++u8FadeIdx == u8NumFades) {
        u8FadeIdx = 0;
      }

      for (LedNameType eLed = WHITE; eLed <= RED; eLed++) {
        if (eLed == aLedFadeSteps[u8FadeIdx][0] ||
            eLed == aLedFadeSteps[u8FadeIdx][1]) {
          aTargetRates[eLed] = LED_PWM_35;
        } else {
          aTargetRates[eLed] = LED_PWM_0;
        }
      }
    }

    if (--u8ColorTimer == 0) {
      u8ColorTimer = LCD_COLOR_STEPS_PER_BAND;

      if (++u8ColorIdx == u8NumColors) {
        u8ColorIdx = 0;
      }

      aTargetRates[LCD_RED] = aLcdColors[u8ColorIdx][0];
      aTargetRates[LCD_GREEN] = aLcdColors[u8ColorIdx][1];
      aTargetRates[LCD_BLUE] = aLcdColors[u8ColorIdx][2];
    }
  }

} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
