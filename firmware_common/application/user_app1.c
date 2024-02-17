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

#define EP_WRITE 1
#define EP_READ 2

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
static void HandleUsbEvent(UsbEventIdType eEvt_);
static void LcdNumber(u8 u8Address, u32 u32Num);

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @publicsection */
/*--------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------*/
/*! @protectedsection */
/*--------------------------------------------------------------------------------------------------------------------*/

#define NTDDI_WIN8 0x06020000

#define MS_VENDOR_CODE 0x01

typedef enum {
  STRING_MFG = 1,
  STRING_PROD,
  STRING_SERIAL,
} UsbStringType;

static const UsbDriverConfigType stDriverCfg = {
    .pfnEventHandler = HandleUsbEvent,
    .stFullSpeedEp0Cfg =
        {
            .u16MaxPacketSize = 64,
            .u8NumPackets = 1,
        },
};

static const UsbDevDescType stDevDesc = {
    .stHeader = USB_DEV_DESC_HEADER,
    .stUsbVersion =
        {
            .u8Major = 2,
            .u4Patch = 1,
        },
    .stDevClass = {.u8Class = USB_CLASS_VENDOR_SPECIFIC},
    .u8Ep0PktSize = 64,
    .u16Vid = USB_VID,
    .u16Pid = USB_PID,
    .stDevVersion =
        {
            .u4Minor = 1,
        },
    .u8ManufacturerStr = STRING_MFG,
    .u8ProductStr = STRING_PROD,
    .u8SerialStr = STRING_SERIAL,
    .u8NumCfgs = 1,
};

static UsbCfgDescType stMainCfg = {
    .stHeader = USB_CFG_DESC_HEADER,
    .u8NumIfaces = 1,
    .u8CfgIdx = 1,
    .u8MaxPower = 50,
    .stAttrib = {._deprecated = 1},
};

static const UsbIfaceDescType stIfaceDesc = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = 0,
    .u8AltIdx = 0,
    .u8NumEpts = 2,
    .stIfaceClass = {.u8Class = USB_CLASS_VENDOR_SPECIFIC},
};

static const UsbEptDescType stWriteEpt = {
    .stHeader = USB_EPT_DESC_HEADER,
    .stAddress = {.u4EptNum = EP_WRITE, .eDir = USB_EPT_DIR_IN},
    .stAttrib = {.eXfer = USB_XFER_BULK},
    .u16MaxPktSize = 64,
};

static const UsbEptDescType stReadEpt = {
    .stHeader = USB_EPT_DESC_HEADER,
    .stAddress = {.u4EptNum = EP_READ, .eDir = USB_EPT_DIR_OUT},
    .stAttrib = {.eXfer = USB_XFER_BULK},
    .u16MaxPktSize = 64,
};

static const UsbDescListType stMainCfgDescs =
    MAKE_USB_DESC_LIST(&stMainCfg, &stIfaceDesc, &stWriteEpt, &stReadEpt);

static const UsbEndpointConfigType astEptCfgs[] = {
    {
        .eDir = USB_EPT_DIR_TO_HOST,
        .eXferType = USB_XFER_BULK,
        .u16MaxPacketSize = 64,
        .u8NumPackets = 2,
    },
    {
        .eDir = USB_EPT_DIR_FROM_HOST,
        .eXferType = USB_XFER_BULK,
        .u16MaxPacketSize = 64,
        .u8NumPackets = 2,
    }};

static UsbBosDescType stBos = {
    .stHeader = USB_BOS_DESC_HEADER,
    .u8NumDeviceCaps = 1,
};

DECL_USB_MS_PLAT_CAPABILITY(stMsCap, {
                                         .u32NtddiWinVersion = NTDDI_WIN8,
                                         .u8VendorCode = MS_VENDOR_CODE,
                                     });

static const UsbDescListType stBosDescs = MAKE_USB_DESC_LIST(&stBos, &stMsCap);

static UsbMsOs20DescSetHeaderType stMsDescHeader = {
    .stHeader = USB_MS_OS_20_SET_HEADER_DESCRIPTOR_HEADER,
    .u32NtddiWindowsVersion = NTDDI_WIN8,
};

static const UsbMsOs20CompatitbleIdType stCompatId = {
    .stHeader = USB_MS_OS_20_FEATURE_COMPATBLE_ID_HEADER,
    .stCompatId = "WINUSB",
};

static const UsbMsOs20DescListType stMsDescs =
    MAKE_USB_MS_OS_20_DESC_LIST(&stMsDescHeader, &stCompatId);

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
  LedOn(LCD_RED);
  LedOn(LCD_GREEN);
  LedOn(LCD_BLUE);

  LcdCommand(LCD_HOME_CMD);
  LcdCommand(LCD_CLEAR_CMD);
  LcdCommand(LCD_DISPLAY_CMD | LCD_DISPLAY_ON);
  LcdNumber(LINE1_START_ADDR, 0);

  stMainCfg.u16TotLen = UsbDescListByteLen(stMainCfgDescs);
  stBos.u16TotalLength = UsbDescListByteLen(stBosDescs);
  stMsDescHeader.u16TotalLength = UsbMsOs20DescListByteLen(stMsDescs);
  stMsCap.astDescSets[0].u16DescSetLength = stMsDescHeader.u16TotalLength;

  bool bCfgOk = UsbSetDriverConfig(&stDriverCfg);

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

  static u32 u32Displayed = 0;
  if (u32Displayed != u32EchoCount) {
    LcdClearChars(LINE1_START_ADDR, U8_LCD_MAX_LINE_DISPLAY_SIZE);
    LcdNumber(LINE1_START_ADDR, u32EchoCount);
    u32Displayed = u32EchoCount;
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

  if (UsbIsPacketReady(EP_READ) && UsbIsPacketReady(EP_WRITE)) {
    static u8 pkt_buf[64];

    u8 pkt_sz = UsbRead(EP_READ, pkt_buf, 64);
    UsbWrite(EP_WRITE, pkt_buf, pkt_sz);

    UsbNextPacket(EP_WRITE);

    if (UsbGetPktOffset(EP_READ) == UsbGetPktSize(EP_READ)) {
      UsbNextPacket(EP_READ);
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

static void HandleStringRequest(UsbStringType eString) {
  const char *pcStr = NULL;

  switch (eString) {
  case STRING_MFG:
    pcStr = "James Bootsma";
    break;

  case STRING_PROD:
    pcStr = "Razor Dev Board";
    break;

  case STRING_SERIAL:
    pcStr = "1234576890";
    break;
  }

  if (pcStr) {
    void *desc = UsbCreateStringDesc(pcStr);
    if (desc) {
      UsbAcceptRequest(UsbSendDesc, free, desc);
    }
  }
}

static void HandleStandardRequest(const UsbSetupPacketType *pstRequest) {
  UsbStandardRequestIdType eId = pstRequest->u8RequestId;

  switch (eId) {
  case USB_REQ_GET_DESCRIPTOR: {
    UsbDescType eType = (u8)(pstRequest->u16Value >> 8);
    u8 u8Idx = (u8)pstRequest->u16Value;

    switch (eType) {
    case USB_DESC_TYPE_DEV:
      if (u8Idx == 0) {
        UsbAcceptRequest(UsbSendDesc, NULL, (void *)&stDevDesc);
      }
      break;

    case USB_DESC_TYPE_CFG:
      if (u8Idx == 0) {
        UsbAcceptRequest(UsbSendDescList, NULL, (void *)&stMainCfgDescs);
      }
      break;

    case USB_DESC_TYPE_STRING: {
      if (u8Idx == 0) {
        u16 u16Lang = USB_LANG_ID_EN_US;
        UsbDescHeaderType *pstDesc = UsbCreateLangIds(1, &u16Lang);
        if (pstDesc) {
          UsbAcceptRequest(UsbSendDesc, free, pstDesc);
        }
      } else {
        HandleStringRequest(u8Idx);
      }
    } break;

    case USB_DESC_TYPE_BOS:
      if (u8Idx == 0) {
        UsbAcceptRequest(UsbSendDescList, NULL, (void *)&stBosDescs);
      }
      break;

    default:
      break;
    }

  } break;

  case USB_REQ_GET_CFG: {
    u8 u8Cfg = 0;
    if (bIfaceActive) {
      u8Cfg = 1;
    }
    UsbWrite(USB_DEF_CTRL_EP, &u8Cfg, 1);
    UsbNextPacket(USB_DEF_CTRL_EP);
  } break;

  case USB_REQ_SET_CFG: {
    u8 u8Cfg = (u8)pstRequest->u16Value;
    bool bSuccess = FALSE;

    switch (u8Cfg) {
    case 0:
      bSuccess = UsbSetEndpointsConfig(0, NULL);
      if (bSuccess) {
        SetLedStatus(YELLOW);
        bIfaceActive = FALSE;
      }
      break;

    case 1:
      bSuccess = UsbSetEndpointsConfig(2, astEptCfgs);
      if (bSuccess) {
        SetLedStatus(GREEN);
        bIfaceActive = TRUE;
      }
      break;
    }

    if (bSuccess) {
      UsbNextPacket(USB_DEF_CTRL_EP);
    } else {
      UsbFailRequest();
    }
  } break;

  case USB_REQ_GET_IFACE: {
    u8 u8Iface = 0;
    UsbWrite(USB_DEF_CTRL_EP, &u8Iface, 1);
    UsbNextPacket(USB_DEF_CTRL_EP);
  } break;

  case USB_REQ_SET_IFACE: {
    if (pstRequest->u16Value == 0 && pstRequest->u16Index == 0) {
      UsbNextPacket(USB_DEF_CTRL_EP);
    } else {
      UsbFailRequest();
    }
  } break;

  default:
    break;
  }
}

static void HandleMsRequest(const UsbSetupPacketType *pstRequest) {
  switch (pstRequest->u16Index) {
  case USB_MS_OS_20_DESCRIPTOR_INDEX:
    UsbAcceptRequest(UsbSendMsOs20DescSet, NULL, (void *)&stMsDescs);
    break;
  }
}

/* Process device-level USB events. */
static void HandleUsbEvent(UsbEventIdType eEvt_) {
  switch (eEvt_) {
  case USB_EVT_RESET:
    SetLedStatus(YELLOW);
    break;

  case USB_EVT_REQUEST: {
    const UsbSetupPacketType *pstRequest = UsbGetCurrentRequest();
    switch (pstRequest->stRequestType.eType) {
    case USB_REQ_TYPE_STANDARD:
      HandleStandardRequest(pstRequest);
      break;

    case USB_REQ_TYPE_VENDOR:
      if (pstRequest->u8RequestId == MS_VENDOR_CODE) {
        HandleMsRequest(pstRequest);
      }
      break;

    default:
      break;
    }
  } break;

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
