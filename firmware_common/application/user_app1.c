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

/**********************************************************************************************************************
Private Function Prototypes
**********************************************************************************************************************/

static void ReportError(const char *pcMsg_);

static bool InitUsb(void);
static void HandleUsbEvent(UsbEventIdType eEvt_);
static void OnStandardUsbRequest(const UsbSetupPacketType *pstRequest);
static void OnDescriptorRequest(const UsbSetupPacketType *pstRequest);
static void OnUsbStringRequest(const UsbSetupPacketType *pstRequest, u8 u8StrIdx);
static void OnUsbClassRequest(const UsbSetupPacketType *pstRequest);
static void OnAudioCtrlRequest(const UsbSetupPacketType *pstRequest);
static void OnAudioStreamRequest(const UsbSetupPacketType *pstRequest);
static void OnAudioEptRequest(const UsbSetupPacketType *pstRequest);

static void SendAudioFrame(void);

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
  for (u8 led = 0; led < U8_TOTAL_LEDS; led++) {
    LedOff(led);
  }
  LedOn(LCD_RED);
  LedOn(LCD_GREEN);
  LedOn(LCD_BLUE);

  LcdCommand(LCD_DISPLAY_CMD | LCD_DISPLAY_ON);
  LcdCommand(LCD_HOME_CMD);
  LcdCommand(LCD_CLEAR_CMD);
  LcdMessage(LINE1_START_ADDR, "Audio Demo");

  // Start assuming init will be good, ReportError() will override this if something bad happens.
  UserApp1_pfStateMachine = UserApp1SM_Idle;

  if (!InitUsb()) {
    ReportError("Failed to initialize USB interface");
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

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)

/**********************************************************************************************************************
USB application-specific constants and descriptors
**********************************************************************************************************************/

// Channel cluster descriptor for mono signal.
#define MONO_CHANNEL_DESC                                                                                              \
  (UsbAudioChannelClusterDesc) {                                                                                       \
    .u8NrChannels = 1, .stChannelConfig = {.bFrontCenter = 1 }                                                         \
  }

// VID/PID used according to https://github.com/obdev/v-usb/blob/master/usbdrv/USB-IDs-for-free.txt.
// Note the special rules there about devices discriminated by serial number.
#define USB_VID 0x27E0
#define USB_PID 0x16C0

// TODO: Generate based on some hardware UID or something.
#define SERIAL 80071337
#define SERIAL_STR_CONTENT "jrbootsma@gmail.com:" STRINGIFY(SERIAL)

// USB strings
enum {
  LANGIDS_STR,
  PRODUCT_STR,
  MANUFACTURE_STR,
  SERIAL_STR,
};

// Configurations
enum {
  NO_CFG,
  MAIN_CFG,

  CFG_COUNT,
};

// Interface numbers
enum {
  IFACE_AUDIO_CTRL,
  IFACE_AUDIO_OUTPUT,

  IFACE_COUNT,
};

// Interface alts for audio output.
enum {
  IFACE_AUDIO_OUT_ALT_0_BYTES,
  IFACE_AUDIO_OUT_ALT_100_BYTES, // Can handle 48 KHz 16-bit mono.

  NUM_OUT_ALTS,
};

// Endpoint numbers
enum {
  EPT_CTRL,
  EPT_AUDIO_OUT,
};

// Id's for all components in the audio chain.
enum {
  UNDEF_ID,
  SILENCE_TERM_ID,
  OUT_TERM_ID,
  CLK_SRC_ID,
};

// Descriptors
// where many descriptors are sent in response to a single request, the layout here tends to match the order they are
// sent.

static const UsbDevDescType stDevDesc = {
    .stHeader = USB_DEV_DESC_HEADER,
    .stUsbVersion = {.u8Major = 2},
    .stDevClass = USB_CLASS_IAD,
    .u8Ep0PktSize = 64,
    .u16Vid = USB_VID,
    .u16Pid = USB_PID,
    .stDevVersion =
        {
            .u8Major = 0,
            .u4Minor = 1,
        },
    .u8ManufacturerStr = MANUFACTURE_STR,
    .u8ProductStr = PRODUCT_STR,
    .u8SerialStr = SERIAL_STR,
    .u8NumCfgs = 1,
};

static UsbCfgDescType stMainCfgDesc = {
    .stHeader = USB_CFG_DESC_HEADER,
    .u8NumIfaces = IFACE_COUNT,
    .u8CfgIdx = MAIN_CFG,
    .stAttrib = {._deprecated = 1},
    .u8MaxPower = 50, // 100 mA
};

static const UsbIfaceAssocDescType stAudioIad = {
    .stHeader = USB_IFACE_ASSOC_DESC_HEADER,
    .u8FirstInterface = IFACE_AUDIO_CTRL,
    .u8InterfaceCount = 2,
    .stFunctionClass = USB_AUDIO_FUNC_CLASS,
};

static const UsbIfaceDescType stAudioCtrlDesc = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_CTRL,
    .stIfaceClass = USB_AUDIO_CTRL_CLASS,
};

static UsbAudioCtrlHeaderDescType stAudioCtrlHeaderDesc = {
    .stHeader = USB_AUDIO_CTRL_HEADER_DESC_HEADER,
    .stAdcVersion = {.u8Major = 2},
    .eCategory = USB_AUDIO_CAT_IO_BOX,
};

static const UsbAudioClkSrcDescType stClkSrcDesc = {
    .stHeader = USB_AUDIO_CLK_SRC_DESC_HEADER,
    .u8ClkId = CLK_SRC_ID,
    .stAttributes = {.eClkType = USB_AUDIO_CLK_INTERNAL_FIX},
    .stControls = {.eClkFreq = USB_AUDIO_CTRL_PROP_READ_ONLY},
};

static const UsbAudioInTermDescType stSilenceDesc = {
    .stHeader = USB_AUDIO_IN_TERM_DESC_HEADER,
    .u8TermId = SILENCE_TERM_ID,
    .eTermType = USB_AUDIO_TERM_IN_MIC,
    .u8ClkSrcId = CLK_SRC_ID,
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioOutTermDescType stOutTermDesc = {
    .stHeader = USB_AUDIO_OUT_TERM_DESC_HEADER,
    .u8TermId = OUT_TERM_ID,
    .eTermType = USB_AUDIO_TERM_USB_STREAM,
    .u8SrcId = SILENCE_TERM_ID,
    .u8ClkSrcId = CLK_SRC_ID,
};

static const UsbDescListType stAudioCtrlList =
    MAKE_USB_DESC_LIST(&stAudioCtrlHeaderDesc, &stClkSrcDesc, &stSilenceDesc, &stOutTermDesc);

static const UsbIfaceDescType stAudioUsbOutIfaceDesc_0Bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_OUTPUT,
    .u8AltIdx = IFACE_AUDIO_OUT_ALT_0_BYTES,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbIfaceDescType stAudioUsbOutIfaceDesc_100Bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_OUTPUT,
    .u8AltIdx = IFACE_AUDIO_OUT_ALT_100_BYTES,
    .u8NumEpts = 1,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbAudioStreamIfaceDescType stAudioStreamDesc = {
    .stHeader = USB_AUDIO_STREAM_IFACE_DESC_HEADER,
    .u8TerminalLink = OUT_TERM_ID,
    .eFormatType = USB_AUDIO_FORMAT_TYPE_I,
    .uFormats = {.stTypeI = {.bPcm = 1}},
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioTypeIFormatDescType stAudioFmtDesc = {
    .stHeader = USB_AUDIO_FMT_TYPE_I_DESC_HEADER,
    .eFormatType = USB_AUDIO_FORMAT_TYPE_I,
    .u8SubslotSize = 2,
    .u8BitResolution = 16,
};

static const UsbEptDescType stAudioUsbOutEptDesc_100Bytes = {
    .stHeader = USB_EPT_DESC_HEADER,
    .stAddress =
        {
            .u4EptNum = EPT_AUDIO_OUT,
            .eDir = USB_EPT_DIR_TO_HOST,
        },
    .stAttrib =
        {
            .eXfer = USB_XFER_ISO,
            .eSync = USB_SYNC_ASYNC,
            .eUsage = USB_EPT_USAGE_DATA,
        },
    .stMaxPacketSize =
        {
            .u11PacketSize = 100,
        },
    .u8Interval = 1, // 2^(1-1) == Every 1 frame.
};

static const UsbAudioIsoEptDescType stAudioEptDesc = {
    .stHeader = USB_AUDIO_ISO_EPT_DESC_HEADER,
};

// Use some explicit formatting to show the hierarchial structure of the descriptors.
// clang-format off
static const UsbDescListType stMainCfgList =
    MAKE_USB_DESC_LIST(
    &stMainCfgDesc,
      &stAudioIad,
        &stAudioCtrlDesc,
          &stAudioCtrlHeaderDesc,
            &stClkSrcDesc,
            &stSilenceDesc,
            &stOutTermDesc,
          &stAudioUsbOutIfaceDesc_0Bytes,
          &stAudioUsbOutIfaceDesc_100Bytes,
            &stAudioStreamDesc,
            &stAudioFmtDesc,
            &stAudioUsbOutEptDesc_100Bytes,
              &stAudioEptDesc);
// clang-format on

// Driver configuration
static const UsbDriverConfigType stUsbDriverCfg = {
    .pfnEventHandler = HandleUsbEvent,
    .stFullSpeedEp0Cfg =
        {
            .u16MaxPacketSize = 64,
            .u8NumPackets = 1,
            .eXferType = USB_XFER_CTRL,
        },
    .bHighSpeedEnabled = FALSE,
};

static const UsbEndpointConfigType astMainEpts[] = {{
    .u16MaxPacketSize = 128,
    .u8NumPackets = 2,
    .eXferType = USB_XFER_ISO,
    .eDir = USB_EPT_DIR_TO_HOST,
}};

// USB state variables.
struct {
  u8 u8ActiveCfg;
  u8 u8OutAlt;
} stUsb;

/**********************************************************************************************************************
Private Functions
**********************************************************************************************************************/

static void ReportError(const char *pcMsg_) {
  DebugPrintf((u8 *)pcMsg_);
  DebugLineFeed();
  UserApp1_pfStateMachine = UserApp1SM_Error;
}

static bool InitUsb(void) {
  stMainCfgDesc.u16TotLen = UsbDescListByteLen(stMainCfgList);
  stAudioCtrlHeaderDesc.u16TotalLength = UsbDescListByteLen(stAudioCtrlList);

  stUsb.u8ActiveCfg = NO_CFG;
  stUsb.u8OutAlt = IFACE_AUDIO_OUT_ALT_0_BYTES;

  bool bOk = UsbSetDriverConfig(&stUsbDriverCfg);

  return bOk;
}

static void HandleUsbEvent(UsbEventIdType eEvt_) {
  switch (eEvt_) {
  case USB_EVT_RESET:
    // TODO: factor out once there's more to do than clear numbers.
    stUsb.u8ActiveCfg = NO_CFG;
    stUsb.u8OutAlt = IFACE_AUDIO_OUT_ALT_0_BYTES;

    if (!UsbSetEndpointsConfig(0, NULL)) {
      ReportError("Could not clear endpoints");
    }
    break;

  case USB_EVT_REQUEST: {
    const UsbSetupPacketType *pstRequest = UsbGetCurrentRequest();
    switch (pstRequest->stRequestType.eType) {
    case USB_REQ_TYPE_STANDARD:
      OnStandardUsbRequest(pstRequest);
      break;
    case USB_REQ_TYPE_CLASS:
      OnUsbClassRequest(pstRequest);
      break;

    default:
      UsbFailRequest();
      break;
    }
  } break;

  case USB_EVT_SUSPEND:
  case USB_EVT_RESUME:
    ReportError("USB suspend not supported");
    break;
  }
}

static void OnStandardUsbRequest(const UsbSetupPacketType *pstRequest) {
  switch (pstRequest->u8RequestId) {
  case USB_REQ_GET_CFG:
    UsbWrite(EPT_CTRL, &stUsb.u8ActiveCfg, sizeof(stUsb.u8ActiveCfg));
    UsbNextPacket(EPT_CTRL);
    break;

  case USB_REQ_GET_DESCRIPTOR:
    OnDescriptorRequest(pstRequest);
    break;

  case USB_REQ_GET_IFACE:
    if (stUsb.u8ActiveCfg == MAIN_CFG) {
      bool bSend = FALSE;
      u8 u8Alt = 0;

      switch (pstRequest->u16Index) {
      case IFACE_AUDIO_CTRL:
        bSend = TRUE;
        break;

      case IFACE_AUDIO_OUTPUT:
        bSend = TRUE;
        u8Alt = stUsb.u8OutAlt;
        break;

      default:
        break;
      }

      if (bSend) {
        UsbWrite(EPT_CTRL, &u8Alt, sizeof(u8Alt));
        UsbNextPacket(EPT_CTRL);
      }
    }
    break;

  case USB_REQ_SET_CFG:
    if (pstRequest->u16Value < CFG_COUNT) {
      stUsb.u8ActiveCfg = pstRequest->u16Value;
      stUsb.u8OutAlt = IFACE_AUDIO_OUT_ALT_0_BYTES;

      DebugPrintf("Switching to CFG ");
      DebugPrintNumber(pstRequest->u16Value);
      DebugLineFeed();

      switch (stUsb.u8ActiveCfg) {
      case MAIN_CFG:
        if (!UsbSetEndpointsConfig(sizeof(astMainEpts) / sizeof(astMainEpts[0]), astMainEpts)) {
          ReportError("Could not configure endpoints");
        }
        break;

      default:
        if (!UsbSetEndpointsConfig(0, NULL)) {
          ReportError("Could not clear endpoints");
        }
        break;
      }

      UsbNextPacket(EPT_CTRL);
    } else {
      UsbFailRequest();
    }
    break;

  case USB_REQ_SET_IFACE:
    if (stUsb.u8ActiveCfg == MAIN_CFG) {
      if (pstRequest->u16Index == IFACE_AUDIO_OUTPUT) {
        if (pstRequest->u16Value < NUM_OUT_ALTS) {
          DebugPrintf("Changing out iface to alt ");
          DebugPrintNumber(pstRequest->u16Value);
          DebugLineFeed();

          stUsb.u8OutAlt = (u8)pstRequest->u16Value;
          UsbNextPacket(EPT_CTRL);
        }
      }
    }
    break;
  }
}

static void OnDescriptorRequest(const UsbSetupPacketType *pstRequest) {
  UsbDescType eType = (UsbDescType)(pstRequest->u16Value >> 8);
  u8 u8Idx = (u8)(pstRequest->u16Value);

  switch (eType) {
  case USB_DESC_TYPE_DEV:
    UsbAcceptRequest(UsbSendDesc, NULL, (void *)&stDevDesc);
    break;

  case USB_DESC_TYPE_CFG:
    UsbAcceptRequest(UsbSendDescList, NULL, (void *)&stMainCfgList);
    break;

  case USB_DESC_TYPE_STRING:
    OnUsbStringRequest(pstRequest, u8Idx);
    break;

  default:
    UsbFailRequest();
    break;
  }
}

static void OnUsbStringRequest(const UsbSetupPacketType *pstRequest, u8 u8StrIdx) {
  UsbDescHeaderType *pstDesc = NULL;

  if (u8StrIdx != LANGIDS_STR && pstRequest->u16Index != USB_LANG_ID_EN_US) {
    UsbFailRequest();
    return;
  }

  switch (u8StrIdx) {
  case LANGIDS_STR: {
    u16 u16Lang = USB_LANG_ID_EN_US;
    pstDesc = UsbCreateLangIds(1, &u16Lang);
  } break;

  case PRODUCT_STR: {
    pstDesc = UsbCreateStringDesc("Audio Demo Board");
  } break;

  case MANUFACTURE_STR: {
    pstDesc = UsbCreateStringDesc("James Bootsma");
  } break;

  case SERIAL_STR: {
    pstDesc = UsbCreateStringDesc(SERIAL_STR_CONTENT);
  } break;

  default:
    break;
  }

  if (pstDesc) {
    UsbAcceptRequest(UsbSendDesc, free, pstDesc);
  } else {
    UsbFailRequest();
  }
}

static void OnUsbClassRequest(const UsbSetupPacketType *pstRequest) {
  if (stUsb.u8ActiveCfg != MAIN_CFG) {
    return;
  }

  // TODO: more flexibility.
  if (pstRequest->stRequestType.eDir != USB_REQ_DIR_DEV_TO_HOST) {
    return;
  }

  if (pstRequest->stRequestType.eTgt != USB_REQ_TGT_IFACE) {
    return;
  }

  u8 u8IFace = (u8)(pstRequest->u16Index);
  u8 u8Entity = (u8)(pstRequest->u16Index >> 8);

  if (u8IFace != IFACE_AUDIO_CTRL || u8Entity != CLK_SRC_ID) {
    return;
  }

  UsbAudioClkSrcCtrlType eCtrl = (UsbAudioClkSrcCtrlType)(u8)(pstRequest->u16Value >> 8);
  u8 u8Chan = (u8)pstRequest->u16Value;

  if (eCtrl != USB_AUDIO_CLK_SRC_CTRL_SAM_FREQ || u8Chan != 0) {
    return;
  }

  switch (pstRequest->u8RequestId) {
  case USB_AUDIO_REQ_CUR: {
    static const u32 u32Freq = 44100;
    UsbWrite(EPT_CTRL, &u32Freq, sizeof(u32Freq));
    UsbNextPacket(EPT_CTRL);
  } break;

  case USB_AUDIO_REQ_RANGE: {
    static const struct __attribute__((packed)) {
      u16 u16NumRanges;
      u32 u32Min;
      u32 u32Max;
      u32 u32Res;
    } stRanges = {
        .u16NumRanges = 1,
        .u32Min = 44100,
        .u32Max = 44100,
        .u32Res = 1,
    };

    UsbWrite(EPT_CTRL, &stRanges, sizeof(stRanges));
    UsbNextPacket(EPT_CTRL);
  } break;

  default:
    break;
  }
}

static void SendAudioFrame(void) {
  static u8 u8SampleFrac = 0;

  if (stUsb.u8ActiveCfg != MAIN_CFG || stUsb.u8OutAlt != IFACE_AUDIO_OUT_ALT_100_BYTES) {
    return;
  }

  u8 u8Len = 44;
  if (++u8SampleFrac == 10) {
    u8SampleFrac = 0;
    u8Len += 1;
  }

  static const s16 as16Zeros[45] = {0};
  UsbWrite(EPT_AUDIO_OUT, &as16Zeros, u8Len * sizeof(s16));
  UsbNextPacket(EPT_AUDIO_OUT);
}

/**********************************************************************************************************************
State Machine Function Definitions
**********************************************************************************************************************/
/*-------------------------------------------------------------------------------------------------------------------*/
/* What does this state do? */
static void UserApp1SM_Idle(void) {
  if (WasButtonPressed(BUTTON0)) {
    ButtonAcknowledge(BUTTON0);

    bool bEnable = !UsbIsEnabled();
    if (bEnable) {
      DebugPrintf("Enabling USB\r\n");
    } else {
      DebugPrintf("Disabling USB\r\n");
    }

    if (!UsbSetEnabled(bEnable)) {
      ReportError("Unable to change USB peripheral state");
    }
  }

  SendAudioFrame();
} /* end UserApp1SM_Idle() */

/*-------------------------------------------------------------------------------------------------------------------*/
/* Handle an error */
static void UserApp1SM_Error(void) {
  static bool bSignaled = FALSE;

  if (!bSignaled) {
    bSignaled = TRUE;

    UsbSetEnabled(FALSE);

    for (u8 u8Led = WHITE; u8Led <= RED; u8Led++) {
      LedOff(u8Led);
    }

    LedBlink(RED, LED_8HZ);

    LcdCommand(LCD_HOME_CMD);
    LcdCommand(LCD_CLEAR_CMD);
    LcdMessage(LINE1_START_ADDR, "ERROR");
  }
} /* end UserApp1SM_Error() */

/*--------------------------------------------------------------------------------------------------------------------*/
/* End of File */
/*--------------------------------------------------------------------------------------------------------------------*/
