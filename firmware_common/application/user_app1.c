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

// NOTE: Refer to usb.org for info on appropriate IDs. This test app
// intentionally avoids checking in anything specific.

// #define USB_VID
// #define USB_PID

#define EP_IN 1
#define EP_OUT 2

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

/// @brief  Used to determine if data echo should be attempted this time slice
/// or not.
static bool bIfaceActive = FALSE;

/// @brief Keep track of total bytes echoed for debug/display purposes.
static u32 u32EchoCount = 0;

/**********************************************************************************************************************
Function Definitions
**********************************************************************************************************************/

static void SetLedStatus(LedNameType eLed_);
static void HandleDevEvt(UsbDevEvtIdType eEvt_);
static void HandleIfaceEvt(UsbIfaceEvtIdType eEvt_);
static void LcdNumber(u8 u8Address, u32 u32Num);

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
  bool bCfgOk = TRUE;

  static const UsbDeviceInfoType stDevInfo = {
      .pcManufacturerName = "Embedded in Embedded",
      .pcProductName = "Razor Dev Board",
      .pcSerialNum = "12345678",

      .pfnEventHandler = HandleDevEvt,

      .stId =
          {
              .u16Vendor = USB_VID,
              .u16Product = USB_PID,
          },

      .stDevVersion =
          {
              .u8Major = 0,
              .u8Minor = 1,
              .u8SubMinor = 0,
          },
  };

  bCfgOk = bCfgOk && UsbSetDeviceInfo(&stDevInfo);

  static const UsbConfigInfoType stCfgInfo = {
      .pcConfigName = "Usb Demo",
  };

  bCfgOk = bCfgOk && UsbAddConfig(&stCfgInfo);

  // Two endpoints for echo usage, 64 is max packet size at full speed.
  // Remember In is dev->host.

  static const UsbEndptInfoType stEpInCfg = {
      .bIsIn = TRUE,
      .u8TransferType = USB_XFER_TYPE_BULK,
      .u16PacketSize = 64,
  };

  static const UsbEndptInfoType stEpOutCfg = {
      .bIsIn = FALSE,
      .u8TransferType = USB_XFER_TYPE_BULK,
      .u16PacketSize = 64,
  };

  static const UsbIfaceInfoType stIfaceInfo = {
      .pcIfaceName = "Bulk Data Echo",
      .pfnEventHandler = HandleIfaceEvt,
      .stClass =
          {
              .u8Class = USB_CLASS_VENDOR_SPECIFIC,
          },
  };

  bCfgOk = bCfgOk && UsbAddIface(&stIfaceInfo, FALSE);
  bCfgOk = bCfgOk && UsbSetEndpointCapacity(EP_IN, 64, 2);
  bCfgOk = bCfgOk && UsbSetEndpointCapacity(EP_OUT, 64, 2);
  bCfgOk = bCfgOk && UsbUseEndpt(EP_IN, &stEpInCfg);
  bCfgOk = bCfgOk && UsbUseEndpt(EP_OUT, &stEpOutCfg);

  LedOn(LCD_RED);
  LedOn(LCD_GREEN);
  LedOn(LCD_BLUE);

  LcdCommand(LCD_HOME_CMD);
  LcdCommand(LCD_CLEAR_CMD);
  LcdCommand(LCD_DISPLAY_CMD | LCD_DISPLAY_ON);

  /* If good initialization, set state to Idle */
  if (bCfgOk) {
    SetLedStatus(U8_TOTAL_LEDS);
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
  static bool attach_started = FALSE;

  static u32 u32FrameCounter = 100;
  if (--u32FrameCounter == 0) {
    u32FrameCounter = 100;
    LcdClearChars(LINE1_START_ADDR, U8_LCD_MAX_LINE_DISPLAY_SIZE);
    LcdNumber(LINE1_START_ADDR, u32EchoCount);
  }

  if (WasButtonPressed(BUTTON0)) {
    ButtonAcknowledge(BUTTON0);

    if (!attach_started) {
      attach_started = TRUE;
      SetLedStatus(ORANGE);
      if (!UsbSetEnabled(TRUE)) {
        UserApp1_pfStateMachine = UserApp1SM_Error;
      }
    }
  }

  if (WasButtonPressed(BUTTON1)) {
    ButtonAcknowledge(BUTTON1);

    UsbSetEnabled(FALSE);
    SetLedStatus(U8_TOTAL_LEDS);
    attach_started = FALSE;
  }

  if (UsbIsPacketReady(EP_OUT) && UsbIsPacketReady(EP_IN)) {
    static u8 pkt_buf[64];

    u8 pkt_sz = UsbRead(EP_OUT, pkt_buf, 64);
    UsbWrite(EP_IN, pkt_buf, pkt_sz);

    UsbNextPacket(EP_IN);

    if (UsbGetPktOffset(EP_OUT) == UsbGetPktSize(EP_OUT)) {
      UsbNextPacket(EP_OUT);
    }

    u32EchoCount += pkt_sz;
  }
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {
  SetLedStatus(RED);
  UsbSetEnabled(FALSE);
} /* end UserApp1SM_Error() */

/* Process interface specific USB events. */
static void HandleIfaceEvt(UsbIfaceEvtIdType eEvt_) {
  switch (eEvt_) {
  case USB_IFACE_EVT_SETUP:
  case USB_IFACE_EVT_RESUME:
    bIfaceActive = TRUE;
    SetLedStatus(GREEN);
    break;

  case USB_IFACE_EVT_TEARDOWN:
  case USB_IFACE_EVT_SUSPEND:
    bIfaceActive = FALSE;
    SetLedStatus(YELLOW);
    break;

  default:
    break;
  }
}

/* Process device-level USB events. */
static void HandleDevEvt(UsbDevEvtIdType eEvt_) {
  switch (eEvt_) {
  case USB_DEV_EVT_RESET:
    SetLedStatus(YELLOW);
    break;

  default:
    break;
  }
}

/* For some simple status display, turn all leds off except for a specific one.
 */
static void SetLedStatus(LedNameType eLed_) {
  for (LedNameType i = WHITE; i < LCD_RED; i++) {
    if (i == eLed_) {
      LedOn(i);
    } else {
      LedOff(i);
    }
  }
}

/* Convenience method for displaying a positive integer on the LCD. */
static void LcdNumber(u8 u8Address, u32 u32Num) {
  if (u32Num == 0) {
    LcdMessage(u8Address, "0");
    return;
  }

  // Largest 32 bit integer is 10 digits + a null terminator.
  char acBuf[11] = {0};

  char *pcDigit = &acBuf[10];

  while (u32Num) {
    pcDigit -= 1;
    *pcDigit = '0' + u32Num % 10;
    u32Num /= 10;
  }

  LcdMessage(u8Address, pcDigit);
}

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
