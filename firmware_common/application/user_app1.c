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
  DebugSetPassthrough();

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
  static u8 au8Inbuf[DEBUG_RX_BUFFER_SIZE];
  static u8 u8NameCount = 0;

  static const char sName[] = "james";
  static u8 u8MatchIdx = 0;

  if (WasButtonPressed(BUTTON3)) {
    ButtonAcknowledge(BUTTON3);
    u8NameCount *= 10;
  }

  u8 u8NewChars = DebugScanf(au8Inbuf);

  for (u8 idx = 0; idx < u8NewChars; idx++) {
    // Need to allow repeated chars, so try matching against the next if
    // possible, but also allow if the previous char is matched.
    u8 c = au8Inbuf[idx];

    if (c == sName[u8MatchIdx]) {
      u8MatchIdx += 1;
    } else if (u8MatchIdx > 0 && c == sName[u8MatchIdx - 1]) {
      // Do nothing, it's a repeat.
    } else {
      // reset the match algorith. Take special care of the edge case where the
      // new character is the start of the name again.
      if (c == sName[0]) {
        u8MatchIdx = 1;
      } else {
        u8MatchIdx = 0;
      }
    }

    // Now check if the match is complete.
    if (u8MatchIdx == sizeof(sName) - 1) {
      u8NameCount += 1;

      u8 u8CountDigits = 0;
      if (u8NameCount >= 100) {
        u8CountDigits = 3;
      } else if (u8NameCount >= 10) {
        u8CountDigits = 2;
      } else {
        u8CountDigits = 1;
      }

      u8 u8BoxTBPad = (u8NameCount + 9) / 10;
      u8 u8BoxLRPad = (u8NameCount + 1) / 2;
      u8 u8LineLen = u8BoxLRPad * 2 + u8CountDigits;

      u8 au8Line[256]; // Build strings then print only one to not overload the debug API.

      DebugLineFeed();
      for (u8 i = 0; i < u8LineLen; i++) {
        au8Line[i] = '*';
      }
      au8Line[u8LineLen++] = '\r';
      au8Line[u8LineLen++] = '\n';
      au8Line[u8LineLen] = 0;

      for (u8 p = 0; p < u8BoxTBPad; p++) {
        DebugPrintf(au8Line);
      }

      // Hacky: Temporarily truncate the line for the shorter segments that go with the count.
      // Will restore later.
      au8Line[u8BoxLRPad] = 0;

      DebugPrintf(au8Line);
      DebugPrintNumber(u8NameCount);
      DebugPrintf(au8Line);
      DebugLineFeed();

      au8Line[u8BoxLRPad] = '*';

      for (u8 p = 0; p < u8BoxTBPad; p++) {
        DebugPrintf(au8Line);
      }
    }
  }
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
