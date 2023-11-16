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
  PWMAudioSetFrequency(BUZZER1, 500);

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
  static const u16 aNoteFreqs[] = {
    C3, C3S, D3, D3S, E3, F3, F3S, G3, G3S, A3, A3S, B3,
    C4, C4S, D4, D4S, E4, F4, F4S, G4, G4S, A4, A4S, B4,
    C5, C5S, D5, D5S, E5, F5, F5S, G5, G5S, A5, A5S, B5,
    C6, C6S, D6, D6S, E6, F6, F6S, G6, G6S, A6, A6S, B6,
  };
  static const char acKeys[12] = "zsxdcvgbhnjm";

  static u8 u8BaseIdx = 0;
  static u16 u16NoteTimer = 0;

  static char acScanBuf[DEBUG_SCANF_BUFFER_SIZE];
  u8 charCount = DebugScanf((u8*)acScanBuf);

  if (charCount > 0) {
    for (u8 idx = 0; idx < charCount; idx++) {
      // Loop is kinda pricy, but should only be processing a char or two per tick at most.
      for (u8 key_idx = 0; key_idx < 12; key_idx++) {
        if (acKeys[key_idx] == acScanBuf[idx]) {
          u16 u16Freq = aNoteFreqs[u8BaseIdx + key_idx];
          DebugPrintf("Start note with freq ");
          DebugPrintNumber(u16Freq);
          DebugLineFeed();
          PWMAudioSetFrequency(BUZZER1, u16Freq);
          PWMAudioOn(BUZZER1);
          u16NoteTimer = 500;
          break;
        }
      }
    }
  }

  if (WasButtonPressed(BUTTON0)) {
    ButtonAcknowledge(BUTTON0);
    u8BaseIdx = 0;
  }

  if (WasButtonPressed(BUTTON1)) {
    ButtonAcknowledge(BUTTON1);
    u8BaseIdx = 12;
  }

  if (WasButtonPressed(BUTTON2)) {
    ButtonAcknowledge(BUTTON2);
    u8BaseIdx = 24;
  }

  if (WasButtonPressed(BUTTON3)) {
    ButtonAcknowledge(BUTTON3);
    u8BaseIdx = 36;
  }

  if (u16NoteTimer > 0) {
    if (--u16NoteTimer == 0) {
      PWMAudioOff(BUZZER1);
    }
  }
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
