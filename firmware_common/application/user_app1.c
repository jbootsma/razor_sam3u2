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

// Generated using the accompanying sintable_gen.py
#define FIXP_PI 102944

static const s16 as16SinLut[65] = {0,     804,   1607,  2410,  3211,  4010,  4807,  5600,  6391,  7177,  7960,
                                   8737,  9509,  10276, 11036, 11789, 12536, 13275, 14006, 14728, 15442, 16146,
                                   16841, 17525, 18199, 18862, 19514, 20154, 20781, 21397, 21999, 22588, 23163,
                                   23725, 24272, 24805, 25322, 25825, 26311, 26782, 27237, 27676, 28097, 28502,
                                   28890, 29260, 29613, 29948, 30264, 30563, 30843, 31105, 31347, 31571, 31776,
                                   31962, 32129, 32276, 32403, 32512, 32600, 32669, 32719, 32748, 32758};

/**********************************************************************************************************************
Private Function Prototypes
**********************************************************************************************************************/
#define MAX_SAMPLES 50

typedef struct {
  u16 u16NumSamples;
  s16 as16Samples[MAX_SAMPLES];
} AudioFrame;

static void ReportError(const char *pcMsg_);

static bool InitUsb(void);

static void HandleUsbEvent(UsbEventIdType eEvt_);
static void OnStandardUsbRequest(const UsbSetupPacketType *pstRequest);
static void OnDescriptorRequest(const UsbSetupPacketType *pstRequest);
static void OnUsbStringRequest(const UsbSetupPacketType *pstRequest, u8 u8StrIdx);
static void OnUsbClassRequest(const UsbSetupPacketType *pstRequest);

static void OnAudioCtrlRequest(const UsbSetupPacketType *pstRequest);
static void HandleAudioCtrlWrite(const volatile UsbRequestStatusType *pstStatus, void *);
static void GetClkSrcCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange);
static void GetVolumeCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange);
static void SetVolumeCtrl(u8 u8Ctrl, u8 u8Chan);
static void GetSrcSelCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange);
static void SetSrcSelCtrl(u8 u8Ctrl, u8 u8Chan);

static bool GetAudioFrame(AudioFrame *pstFrame);
static void ProcessAudioFrame(AudioFrame *pstFrame);
static void SendAudioFrame(AudioFrame *pstFrame);

static inline s16 tri_wave(s16 x_) {
  s32 y = abs(x_) - 0x4000;
  // Rather than multiply by 2, use slightly less in order to avoid undefined
  // behaviour in the case where y = 0x4000.
  return (y * 0xffff) >> 15;
}

/// @brief fixed point sin function.
///
/// Based on a 2-term taylor series expansion around points in a pre-computed lookup table.
static inline s16 fix_sin(s16 x_) {
  // Remap x from [-1.0, 1.0) into the equivalent period point within [0.0, 0.5]
  // Remember sign for later application.
  s32 s32sign = 1;
  s32 x = x_;

  // sin(-x) == -sin(x)
  if (x < 0) {
    x = -x;
    s32sign = -1;
  }

  // Reflect around 0.5
  if (x > 0x4000) {
    x = 0x8000 - x;
  }

  // At this point x is in the range 0x0000 - 0x4000;
  // Break it apart into an index into the lookup table and a delta.
  // The delta is used to compute the first term of the taylor series expansion
  // around the indexed point.
  u8 u8Idx = x >> 8;
  s32 s32Delta = x & 0xff;

  // The delta needs to be multiplied out by pi to correct for the scaling used to generate the
  // lookup table.
  s32Delta = (s32Delta * FIXP_PI) >> 15;

  // Compute terms of the taylor expansion.
  s32 s32t0 = as16SinLut[u8Idx];
  s32 s32t1 = (s32Delta * as16SinLut[64 - u8Idx]) >> 15;
  return s32sign * (s32t0 + s32t1);
}

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
  FAKE_SRC_TERM_ID,
  FAKE_SRC_TERM2_ID,
  SRC_SEL_ID,
  OUT_TERM_ID,
  CLK_SRC_ID,
  VOLUME_ID,
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

static const UsbAudioInTermDescType stFakeSourceDesc = {
    .stHeader = USB_AUDIO_IN_TERM_DESC_HEADER,
    .u8TermId = FAKE_SRC_TERM_ID,
    .eTermType = USB_AUDIO_TERM_IN_MIC,
    .u8ClkSrcId = CLK_SRC_ID,
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioInTermDescType stFakeSource2Desc = {
    .stHeader = USB_AUDIO_IN_TERM_DESC_HEADER,
    .u8TermId = FAKE_SRC_TERM2_ID,
    .eTermType = USB_AUDIO_TERM_IN_MIC,
    .u8ClkSrcId = CLK_SRC_ID,
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioSel2DescType stSrcSelDesc = {
    .stHeader = USB_AUDIO_SEL_DESC_HEADER(2),
    .u8UnitId = SRC_SEL_ID,
    .u8NrInPins = 2,
    .au8SrcIds = {FAKE_SRC_TERM_ID, FAKE_SRC_TERM2_ID},
    .stControls =
        {
            .eSelector = USB_AUDIO_CTRL_PROP_HOST_PROG,
        },
};

static const UsbAudioMonoFeatDescType stVolumeDesc = {
    .stHeader = USB_AUDIO_MONO_FEAT_DESC_HEADER,
    .u8UnitId = VOLUME_ID,
    .u8SrcId = SRC_SEL_ID,
    .astControls = {{
        .eVolume = USB_AUDIO_CTRL_PROP_HOST_PROG,
        .eMute = USB_AUDIO_CTRL_PROP_HOST_PROG,
    }},
};

static const UsbAudioOutTermDescType stOutTermDesc = {
    .stHeader = USB_AUDIO_OUT_TERM_DESC_HEADER,
    .u8TermId = OUT_TERM_ID,
    .eTermType = USB_AUDIO_TERM_USB_STREAM,
    .u8SrcId = VOLUME_ID,
    .u8ClkSrcId = CLK_SRC_ID,
};

static const UsbDescListType stAudioCtrlList =
    MAKE_USB_DESC_LIST(&stAudioCtrlHeaderDesc, &stClkSrcDesc, &stFakeSourceDesc, &stFakeSource2Desc, &stSrcSelDesc,
                       &stVolumeDesc, &stOutTermDesc);

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
            &stFakeSourceDesc,
            &stFakeSource2Desc,
            &stSrcSelDesc,
            &stVolumeDesc,
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
  u8 u8SrcIdx;

  // Usb expects volumes in a S8.8 format with special value for -inf.
  // For fast calculation we want just a straight multiplier. So keep both around.
  // Conversion is handled when the volume is set over USB.
  s16 s16VolumeUsb;
  s16 s16VolumeRaw;

  bool bMuted;
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

  stUsb.bMuted = TRUE;
  stUsb.s16VolumeUsb = 0x8000;
  stUsb.s16VolumeRaw = 0;

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

  switch (pstRequest->stRequestType.eTgt) {
  case USB_REQ_TGT_IFACE: {
    u8 u8Iface = (u8)(pstRequest->u16Index);
    if (u8Iface == IFACE_AUDIO_CTRL) {
      OnAudioCtrlRequest(pstRequest);
    }

  } break;

  default:
    break;
  }
}

static void OnAudioCtrlRequest(const UsbSetupPacketType *pstRequest) {
  if (pstRequest->stRequestType.eDir == USB_REQ_DIR_HOST_TO_DEV) {
    UsbAcceptRequest(HandleAudioCtrlWrite, NULL, NULL);
    return;
  }

  u8 u8Entity = (u8)(pstRequest->u16Index >> 8);

  u8 u8Ctrl = (u8)(pstRequest->u16Value >> 8);
  u8 u8Chan = (u8)(pstRequest->u16Value);
  bool bIsRange = FALSE;

  switch (pstRequest->u8RequestId) {
  case USB_AUDIO_REQ_CUR:
    break;

  case USB_AUDIO_REQ_RANGE:
    bIsRange = TRUE;
    break;

  default:
    return;
  }

  switch (u8Entity) {
  case CLK_SRC_ID:
    GetClkSrcCtrl(u8Ctrl, u8Chan, bIsRange);
    break;

  case VOLUME_ID:
    GetVolumeCtrl(u8Ctrl, u8Chan, bIsRange);
    break;

  case SRC_SEL_ID:
    GetSrcSelCtrl(u8Ctrl, u8Chan, bIsRange);
    break;
  }
}

static void HandleAudioCtrlWrite(const volatile UsbRequestStatusType *pstStatus, void *) {
  const volatile UsbSetupPacketType *pstRequest = &pstStatus->stHeader;

  if (pstRequest->u8RequestId != USB_AUDIO_REQ_CUR) {
    UsbFailRequest();
    return;
  }

  u8 u8Entity = (u8)(pstRequest->u16Index >> 8);
  u8 u8Ctrl = (u8)(pstRequest->u16Value >> 8);
  u8 u8Chan = (u8)(pstRequest->u16Value);

  switch (u8Entity) {
  case VOLUME_ID:
    SetVolumeCtrl(u8Ctrl, u8Chan);
    break;

  case SRC_SEL_ID:
    SetSrcSelCtrl(u8Ctrl, u8Chan);
    break;
  }

  // If the request wasn't ended for some reason then it's failed.
  if (UsbGetCurrentRequest() != NULL) {
    UsbFailRequest();
  }
}

static void GetClkSrcCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange) {
  if (u8Ctrl != USB_AUDIO_CLK_SRC_CTRL_SAM_FREQ || u8Chan != 0) {
    return;
  }

  if (bIsRange) {
    USB_AUDIO_RANGE_BLOCK(u32, 1)
    stRanges = {
        .u16NumRanges = 1,
        .astRanges = {{44100, 44100, 0}},
    };

    UsbWrite(EPT_CTRL, &stRanges, sizeof(stRanges));
  } else {
    u32 u32SampleRate = 44100;
    UsbWrite(EPT_CTRL, &u32SampleRate, sizeof(u32SampleRate));
  }

  UsbNextPacket(EPT_CTRL);
}

static void GetSrcSelCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange) {
  if (u8Ctrl != USB_AUDIO_SEL_CTRL_SELECTOR || u8Chan != 0 || bIsRange) {
    return;
  }

  u8 u8Val = stUsb.u8SrcIdx + 1;
  UsbWrite(EPT_CTRL, &u8Val, sizeof(u8Val));
  UsbNextPacket(EPT_CTRL);
}

static void SetSrcSelCtrl(u8 u8Ctrl, u8 u8Chan) {
  if (u8Ctrl != USB_AUDIO_SEL_CTRL_SELECTOR || u8Chan != 0) {
    return;
  }

  u8 u8Val;
  if (sizeof(u8Val) != UsbRead(EPT_CTRL, &u8Val, sizeof(u8Val))) {
    return;
  }

  if (u8Val > 2 || u8Val == 0) {
    return;
  }

  stUsb.u8SrcIdx = u8Val - 1;
  UsbNextPacket(EPT_CTRL);
}

static void GetVolumeCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange) {
  if (u8Chan != 0) {
    return;
  }

  switch (u8Ctrl) {
  case USB_AUDIO_FEAT_CTRL_MUTE:
    if (bIsRange) {
      return;
    }

    u8 u8Muted = stUsb.bMuted;
    UsbWrite(EPT_CTRL, &u8Muted, sizeof(u8Muted));
    break;

  case USB_AUDIO_FEAT_CTRL_VOLUME:
    if (bIsRange) {
      // 9 was the smallest scaling factor that didn't start resulting in duplicates in the table,
      // and chose 100 steps just because.

      USB_AUDIO_RANGE_BLOCK(s16, 1)
      stRanges = {
          .u16NumRanges = 1,
          .astRanges = {{-900 + 9, 0, 9}},
      };

      UsbWrite(EPT_CTRL, &stRanges, sizeof(stRanges));

    } else {
      UsbWrite(EPT_CTRL, &stUsb.s16VolumeUsb, sizeof(stUsb.s16VolumeUsb));
    }
    break;

  default:
    return;
  }

  UsbNextPacket(EPT_CTRL);
}

static void SetVolumeCtrl(u8 u8Ctrl, u8 u8Chan) {
  if (u8Chan != 0) {
    return;
  }

  switch (u8Ctrl) {
  case USB_AUDIO_FEAT_CTRL_MUTE: {
    u8 u8Mute;
    if (sizeof(u8Mute) != UsbRead(EPT_CTRL, &u8Mute, sizeof(u8Mute))) {
      return;
    }

    stUsb.bMuted = !!u8Mute;
  } break;

  case USB_AUDIO_FEAT_CTRL_VOLUME: {
    if (sizeof(stUsb.s16VolumeUsb) != UsbRead(EPT_CTRL, &stUsb.s16VolumeUsb, sizeof(stUsb.s16VolumeUsb))) {
      return;
    }

    if (stUsb.s16VolumeUsb == INT16_MIN) {
      stUsb.s16VolumeRaw = 0;
    } else {
      s16 s16Idx = stUsb.s16VolumeUsb / 9;
      if (s16Idx > 0) {
        s16Idx = 0;
      }
      if (s16Idx < -99) {
        s16Idx = -99;
      }

      // stUsb.s16VolumeUsb = s16Idx * 9;
      s16Idx = -s16Idx;

      // This table was generated using the python expression
      // [int(10 ** ((-idx * 9) / 256) * (2**15 - 1)) for idx in range(100)]

      static const s16 as16VolMap[100] = {
          32767, 30219, 27869, 25702, 23703, 21860, 20160, 18592, 17146, 15813, 14583, 13449, 12403, 11439, 10549,
          9729,  8972,  8275,  7631,  7038,  6491,  5986,  5520,  5091,  4695,  4330,  3993,  3683,  3396,  3132,
          2889,  2664,  2457,  2266,  2089,  1927,  1777,  1639,  1511,  1394,  1285,  1185,  1093,  1008,  930,
          857,   791,   729,   672,   620,   572,   527,   486,   448,   413,   381,   352,   324,   299,   276,
          254,   234,   216,   199,   184,   169,   156,   144,   133,   122,   113,   104,   96,    88,    82,
          75,    69,    64,    59,    54,    50,    46,    42,    39,    36,    33,    31,    28,    26,    24,
          22,    20,    19,    17,    16,    14,    13,    12,    11,    10};

      stUsb.s16VolumeRaw = as16VolMap[s16Idx];
    }
  } break;

  default:
    return;
  }

  UsbNextPacket(EPT_CTRL);
}

static bool GetAudioFrame(AudioFrame *pstFrame) {
  static u8 u8FrameIdx = 0;
  static s16 s16Period = 0;

  pstFrame->u16NumSamples = 44;
  if (++u8FrameIdx == 10) {
    u8FrameIdx = 0;
    pstFrame->u16NumSamples += 1;
  }

  for (u8 u8Idx = 0; u8Idx < pstFrame->u16NumSamples; u8Idx++) {
    s16 s16Sample;

    if (stUsb.u8SrcIdx == 0) {
      s16Sample = tri_wave(s16Period);
    } else {
      s16Sample = fix_sin(s16Period);
    }

    pstFrame->as16Samples[u8Idx] = s16Sample / 2;

    // 440 Hz, sampled @ 44.1 Khz.
    s16Period += 654;
  }

  return TRUE;
}

static void ProcessAudioFrame(AudioFrame *pstFrame) {
  // TODO
}

static void SendAudioFrame(AudioFrame *pstFrame) {
  if (stUsb.u8OutAlt != IFACE_AUDIO_OUT_ALT_100_BYTES) {
    return;
  }

  for (u8 u8Idx = 0; u8Idx < pstFrame->u16NumSamples; u8Idx++) {
    s32 s32Sample = pstFrame->as16Samples[u8Idx];
    if (stUsb.bMuted) {
      s32Sample = 0;
    } else {
      s32Sample = (s32Sample * stUsb.s16VolumeRaw) / INT16_MAX;
    }
    pstFrame->as16Samples[u8Idx] = s32Sample;
  }

  UsbWrite(EPT_AUDIO_OUT, pstFrame->as16Samples, pstFrame->u16NumSamples * sizeof(pstFrame->as16Samples[0]));
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

  AudioFrame stFrame;

  if (GetAudioFrame(&stFrame)) {
    ProcessAudioFrame(&stFrame);
    SendAudioFrame(&stFrame);
  }
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
