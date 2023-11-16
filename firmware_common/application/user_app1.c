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

/* Local variables */
#define MAX_PASSWD_LEN 10
static ButtonNameType aPasswd[MAX_PASSWD_LEN] = {0, 2, 2, 1, 2, 0};
static u8 u8PasswdLen = 6;

static ButtonNameType aEntry[MAX_PASSWD_LEN];
static u8 u8EntryLen = 0;

/***********************************************************************************************************************
State Machine Declarations
***********************************************************************************************************************/
static StateMachineType sUserApp1Sm;

static void StartupState(StateMachineEventType eEvt);
static void LockedState(StateMachineEventType eEvt);
static void PasswdSetState(StateMachineEventType eEvt);
static void PasswdCheckState(StateMachineEventType eEvt);

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
  for (u8 eLed = 0; eLed < U8_TOTAL_LEDS; eLed++) {
    LedOff(eLed);
  }

  InitStateMachine(&sUserApp1Sm, StartupState);
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
  // "secret" combo to reset password. Works always.
  if (IsButtonHeld(BUTTON0, 3000) && IsButtonHeld(BUTTON3, 3000)) {
    ChangeState(&sUserApp1Sm, PasswdSetState);
  }

  RunStateMachine(&sUserApp1Sm);

} /* end UserApp1RunActiveState */

/*------------------------------------------------------------------------------------------------------------------*/
/*! @privatesection */
/*--------------------------------------------------------------------------------------------------------------------*/

static void AckAllButtons(void) {
  ButtonAcknowledge(BUTTON0);
  ButtonAcknowledge(BUTTON1);
  ButtonAcknowledge(BUTTON2);
  ButtonAcknowledge(BUTTON3);
}

/**********************************************************************************************************************
State Machine Function Definitions
**********************************************************************************************************************/

void StartupState(StateMachineEventType eEvt) {
  switch (eEvt) {
  case SM_EVT_ENTER:
    LedPWM(YELLOW, LED_PWM_20);
    SetTimeout(&sUserApp1Sm, 3000);

    AckAllButtons();
    break;

  case SM_EVT_TICK:
    if (WasButtonPressed(BUTTON3)) {
      ButtonAcknowledge(BUTTON3);
      ChangeState(&sUserApp1Sm, PasswdSetState);
    }
    break;

  case SM_EVT_TIMEOUT:
    ChangeState(&sUserApp1Sm, LockedState);
    break;

  case SM_EVT_EXIT:
    LedOff(YELLOW);
    break;

  default:
    break;
  }
}

void PasswdSetState(StateMachineEventType eEvt) {
  switch (eEvt) {
  case SM_EVT_ENTER:
    LedBlink(RED, LED_4HZ);
    LedBlink(GREEN, LED_4HZ);
    AckAllButtons();
    u8PasswdLen = 0;
    break;

  case SM_EVT_EXIT:
    LedOff(RED);
    LedOff(GREEN);
    break;

  case SM_EVT_TICK:
    if (u8PasswdLen < MAX_PASSWD_LEN) {
      if (WasButtonPressed(BUTTON0)) {
        ButtonAcknowledge(BUTTON0);
        aPasswd[u8PasswdLen++] = BUTTON0;
      } else if (WasButtonPressed(BUTTON1)) {
        ButtonAcknowledge(BUTTON1);
        aPasswd[u8PasswdLen++] = BUTTON1;
      } else if (WasButtonPressed(BUTTON2)) {
        ButtonAcknowledge(BUTTON2);
        aPasswd[u8PasswdLen++] = BUTTON2;
      }
    }

    if (WasButtonPressed(BUTTON3)) {
      ButtonAcknowledge(BUTTON3);
      ChangeState(&sUserApp1Sm, LockedState);
    }
    break;

  case SM_EVT_TIMEOUT:
    break;
  }
}

void LockedState(StateMachineEventType eEvt) {
  switch (eEvt) {
  case SM_EVT_ENTER:
    LedPWM(RED, LED_PWM_10);
    AckAllButtons();
    u8EntryLen = 0;
    break;

  case SM_EVT_EXIT:
    LedOff(RED);
    break;

  case SM_EVT_TICK:
    if (u8EntryLen < MAX_PASSWD_LEN) {
      if (WasButtonPressed(BUTTON0)) {
        ButtonAcknowledge(BUTTON0);
        aEntry[u8EntryLen++] = BUTTON0;
      } else if (WasButtonPressed(BUTTON1)) {
        ButtonAcknowledge(BUTTON1);
        aEntry[u8EntryLen++] = BUTTON1;
      } else if (WasButtonPressed(BUTTON2)) {
        ButtonAcknowledge(BUTTON2);
        aEntry[u8EntryLen++] = BUTTON2;
      }
    }

    if (WasButtonPressed(BUTTON3)) {
      ButtonAcknowledge(BUTTON3);
      ChangeState(&sUserApp1Sm, PasswdCheckState);
    }
    break;

  case SM_EVT_TIMEOUT:
    break;
  }
}

void PasswdCheckState(StateMachineEventType eEvt) {
  switch (eEvt) {
  case SM_EVT_ENTER:
    bool bMatched = TRUE;
    if (u8EntryLen != u8PasswdLen) {
      bMatched = FALSE;
    } else {
      for (u8 idx = 0; idx < u8PasswdLen; idx++) {
        if (aEntry[idx] != aPasswd[idx]) {
          bMatched = FALSE;
          break;
        }
      }
    }

    if (bMatched) {
      LedBlink(GREEN, LED_1HZ);
    } else {
      LedBlink(RED, LED_8HZ);
    }

    AckAllButtons();

    break;

  case SM_EVT_EXIT:
    LedOff(RED);
    LedOff(GREEN);
    break;

  case SM_EVT_TICK:
    if (WasButtonPressed(BUTTON0) || WasButtonPressed(BUTTON1) ||
        WasButtonPressed(BUTTON2) || WasButtonPressed(BUTTON3)) {
      ChangeState(&sUserApp1Sm, LockedState);
    }
    break;

  case SM_EVT_TIMEOUT:
    break;
  }
}

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
