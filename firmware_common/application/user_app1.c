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
#define NUM_AUDIO_FRAMES 4
#define DEFAULT_SAMPLE_RATE 48000
#define MAX_SAMPLE_RATE 48000
#define MIN_SAMPLE_RATE 8000

// Target systick for buzzer frames to end on.
#define BUZZER_TGT_TICK ((U32_SYSTICK_COUNT / 10) * 2)

typedef struct {
  DmaInfo stDma;
  u16 u16NumSamples;
  s16 as16Samples[MAX_SAMPLES];
  bool bInUse;
} AudioFrame;

typedef struct {
  DmaInfo stDma;
  u16 u16ExpectedTick;
  u16 au16Samples[MAX_SAMPLES * 2];
} PwmFrame;

static AudioFrame astFrames[NUM_AUDIO_FRAMES];
static PwmFrame astPwmFrames[2];

static struct {
  AudioFrame *pstInFrame;
  AudioFrame *pstUsbOutFrame;
  PwmFrame *pstPwmFront;
  PwmFrame *pstPwmBack;
} stPipeline = {
    .pstPwmFront = &astPwmFrames[0],
    .pstPwmBack = &astPwmFrames[1],
};

typedef enum {
  SRC_LOOPBACK,
  SRC_TRI_WAVE,
  SRC_SIN_WAVE,

  SRC_COUNT,
} SourceSelect;

typedef s16 (*SamplerFunc)(s16);

static void ReportError(const char *pcMsg_);
static void UpdateDisplay(void);

static bool InitUsb(void);

static void HandleUsbEvent(UsbEventIdType eEvt_);
static void OnStandardUsbRequest(const UsbSetupPacketType *pstRequest);
static void OnDescriptorRequest(const UsbSetupPacketType *pstRequest);
static void OnUsbStringRequest(const UsbSetupPacketType *pstRequest, u8 u8StrIdx);
static void OnUsbClassRequest(const UsbSetupPacketType *pstRequest);

static void OnAudioCtrlRequest(const UsbSetupPacketType *pstRequest);
static void HandleAudioCtrlWrite(const volatile UsbRequestStatusType *pstStatus, void *);
static void GetClkSrcCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange);
static void SetClkSrcCtrl(u8 u8Ctrl, u8 u8Chan);
static void GetVolumeCtrl(u8 u8Ctrl, u8 u8Chan, bool bIsRange);
static void SetVolumeCtrl(u8 u8Ctrl, u8 u8Chan);

static void ApplySampleRate(void);

static void ResetAudioFrame(AudioFrame *pstFrame);
static AudioFrame *GetAvailableFrame(void);

static void UpdateFrameLen(void);
static AudioFrame *PrepareInFrame(void);
static AudioFrame *GetInFrame(void);
static void ClearSentFrames(void);
static void ProcessAudioPipeline(void);

static void SynthAudioFrame(AudioFrame *pstFrame, SamplerFunc pfnSampler);
static void ProcessAudioFrame(AudioFrame *pstFrame);

static bool SetupUsbRead(AudioFrame *pstFrame);
static void SendToUsb(AudioFrame *pstFrame);

static void BuzzerProcess(AudioFrame *pstFrame);
static void SendInitialBuzzerFrame(void);
static void ConvertToBuzzerFormat(AudioFrame *pstInFrame, PwmFrame *pstOutFrame, u16 u16SamplePeriod);
static void BuzzerDmaCb(DmaInfo *pstDma);

static u32 u32SampleRate;
static u32 u32InvSampleRate; // Fixed point, 1.31
static SourceSelect eSource;

static struct {
  u16 u16Base;
  u16 u16Frac;

  u16 u16Accum;
  u16 u16Curr;
} stFrameLen;

static bool bDisplayUpToDate;

extern volatile u32 G_u32BspPwmUnderruns;
static struct {
  u32 u32Overruns;

  u32 u32FrameClock;      // fixed point 24.8
  u32 u32ClocksPerSample; // fixed point 24.8
  volatile s16 s16TickErr;

  s16 s16SampleCountDiff;

  bool bEnabled;
} stBuzzer;

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

  u32SampleRate = DEFAULT_SAMPLE_RATE;
  ApplySampleRate();

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

static inline s16 SampleTriangle(s16 x_) {
  s32 y = abs(x_) - 0x4000;
  // Rather than multiply by 2, use slightly less in order to avoid undefined
  // behaviour in the case where y = 0x4000.
  return (y * 0xffff) >> 15;
}

/// @brief fixed point sin function.
///
/// Based on a 2-term taylor series expansion around points in a pre-computed lookup table.
static inline s16 SampleSin(s16 x_) {
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

static inline void InitDma(AudioFrame *pstFrame) {
  pstFrame->stDma = (DmaInfo){
      .pvBuffer = pstFrame->as16Samples,
      .u16XferLen = pstFrame->u16NumSamples * sizeof(u16),
  };
}

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
  IFACE_AUDIO_INPUT,

  IFACE_COUNT,
};

// Interface alts for audio output.
enum {
  IFACE_AUDIO_OUT_ALT_0_BYTES,
  IFACE_AUDIO_OUT_ALT_200_BYTES, // Can handle 48 KHz 16-bit mono.

  NUM_OUT_ALTS,
};

// Interface alts for audio input.
enum {
  IFACE_AUDIO_IN_ALT_0_BYTES,
  IFACE_AUDIO_IN_ALT_200_BYTES, // Can handle 48 KHz 16-bit mono.

  NUM_IN_ALTS,
};

// Endpoint numbers
enum {
  EPT_CTRL,
  EPT_AUDIO_OUT,
  EPT_AUDIO_IN,
};

// Id's for all components in the audio chain.
enum {
  UNDEF_ID,
  IN_TERM_ID,
  OUT_TERM_ID,
  CLK_SRC_ID,
  VOLUME_ID,

  // For accepting audio from the USB host.
  LOOP_IN_TERM_ID,
  LOOP_OUT_TERM_ID,
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
    .u8InterfaceCount = 3,
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
    .stControls = {.eClkFreq = USB_AUDIO_CTRL_PROP_HOST_PROG},
};

static const UsbAudioInTermDescType stSourceDesc = {
    .stHeader = USB_AUDIO_IN_TERM_DESC_HEADER,
    .u8TermId = IN_TERM_ID,
    .eTermType = USB_AUDIO_TERM_EXT_GEN_ANALOG,
    .u8ClkSrcId = CLK_SRC_ID,
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioMonoFeatDescType stVolumeDesc = {
    .stHeader = USB_AUDIO_MONO_FEAT_DESC_HEADER,
    .u8UnitId = VOLUME_ID,
    .u8SrcId = IN_TERM_ID,
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

static const UsbAudioInTermDescType stLoopInDesc = {
    .stHeader = USB_AUDIO_IN_TERM_DESC_HEADER,
    .u8TermId = LOOP_IN_TERM_ID,
    .eTermType = USB_AUDIO_TERM_USB_STREAM,
    .u8ClkSrcId = CLK_SRC_ID,
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioOutTermDescType stLoopOutDesc = {
    .stHeader = USB_AUDIO_OUT_TERM_DESC_HEADER,
    .u8TermId = LOOP_OUT_TERM_ID,
    .eTermType = USB_AUDIO_TERM_OUT_SPEAKER,
    .u8SrcId = LOOP_IN_TERM_ID,
    .u8ClkSrcId = CLK_SRC_ID,
};

static const UsbDescListType stAudioCtrlList = MAKE_USB_DESC_LIST(
    &stAudioCtrlHeaderDesc, &stClkSrcDesc, &stSourceDesc, &stVolumeDesc, &stOutTermDesc, &stLoopInDesc, &stLoopOutDesc);

static const UsbIfaceDescType stAudioUsbOutIfaceDesc_0Bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_OUTPUT,
    .u8AltIdx = IFACE_AUDIO_OUT_ALT_0_BYTES,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbIfaceDescType stAudioUsbOutIfaceDesc_200Bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_OUTPUT,
    .u8AltIdx = IFACE_AUDIO_OUT_ALT_200_BYTES,
    .u8NumEpts = 1,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbAudioStreamIfaceDescType stAudioStreamDesc = {
    .stHeader = USB_AUDIO_STREAM_IFACE_DESC_HEADER,
    .u8TerminalLink = OUT_TERM_ID,
    .eFormatType = USB_AUDIO_FORMAT_TYPE_I,
    .uFormats = {.stTypeI = {.bPcm = TRUE}},
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbAudioTypeIFormatDescType stAudioFmtDesc = {
    .stHeader = USB_AUDIO_FMT_TYPE_I_DESC_HEADER,
    .eFormatType = USB_AUDIO_FORMAT_TYPE_I,
    .u8SubslotSize = 2,
    .u8BitResolution = 16,
};

static const UsbEptDescType stAudioUsbOutEptDesc_200Bytes = {
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
            .u11PacketSize = 200,
        },
    .u8Interval = 1, // 2^(1-1) == Every 1 frame.
};

static const UsbAudioIsoEptDescType stAudioEptDesc = {
    .stHeader = USB_AUDIO_ISO_EPT_DESC_HEADER,
};

static const UsbIfaceDescType stAudioUsbInIfaceDesc_0bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_INPUT,
    .u8AltIdx = IFACE_AUDIO_IN_ALT_0_BYTES,
    .u8NumEpts = 0,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbIfaceDescType stAudioUsbInIfaceDesc_200bytes = {
    .stHeader = USB_IFACE_DESC_HEADER,
    .u8IfaceIdx = IFACE_AUDIO_INPUT,
    .u8AltIdx = IFACE_AUDIO_IN_ALT_200_BYTES,
    .u8NumEpts = 1,
    .stIfaceClass = USB_AUDIO_STREAM_CLASS,
};

static const UsbAudioStreamIfaceDescType stAudioUsbInStreamDesc = {
    .stHeader = USB_AUDIO_STREAM_IFACE_DESC_HEADER,
    .u8TerminalLink = LOOP_IN_TERM_ID,
    .eFormatType = USB_AUDIO_FORMAT_TYPE_I,
    .uFormats.stTypeI = {.bPcm = TRUE},
    .stChannels = MONO_CHANNEL_DESC,
};

static const UsbEptDescType stAudioUsbInEptDesc_200bytes = {
    .stHeader = USB_EPT_DESC_HEADER,
    .stAddress =
        {
            .u4EptNum = EPT_AUDIO_IN,
            .eDir = USB_EPT_DIR_FROM_HOST,
        },
    .stAttrib =
        {
            .eXfer = USB_XFER_ISO,
            .eSync = USB_SYNC_SYNC,
            .eUsage = USB_EPT_USAGE_DATA,
        },
    .stMaxPacketSize =
        {
            .u11PacketSize = 200,
        },
    .u8Interval = 1,
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
            &stSourceDesc,
            &stVolumeDesc,
            &stOutTermDesc,
            &stLoopInDesc,
            &stLoopOutDesc,
        &stAudioUsbOutIfaceDesc_0Bytes,
          &stAudioUsbOutIfaceDesc_200Bytes,
            &stAudioStreamDesc,
            &stAudioFmtDesc,
            &stAudioUsbOutEptDesc_200Bytes,
              &stAudioEptDesc,
        &stAudioUsbInIfaceDesc_0bytes,
          &stAudioUsbInIfaceDesc_200bytes,
            &stAudioUsbInStreamDesc,
            &stAudioFmtDesc,
            &stAudioUsbInEptDesc_200bytes,
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

static const UsbEndpointConfigType astMainEpts[] = {
    {
        .u16MaxPacketSize = 256,
        .u8NumPackets = 2,
        .eXferType = USB_XFER_ISO,
        .eDir = USB_EPT_DIR_TO_HOST,
        .bUseDma = TRUE,
    },
    {
        .u16MaxPacketSize = 256,
        .u8NumPackets = 2,
        .eXferType = USB_XFER_ISO,
        .eDir = USB_EPT_DIR_FROM_HOST,
        .bUseDma = TRUE,
    },
};

// USB state variables.
struct {
  // Usb expects volumes in a S8.8 format with special value for -inf.
  // For fast calculation we want just a straight multiplier. So keep both around.
  // Conversion is handled when the volume is set over USB.
  s16 s16VolumeUsb;
  s16 s16VolumeRaw;

  u8 u8ActiveCfg;
  u8 u8OutAlt;
  u8 u8InAlt;

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

static void UpdateDisplay(void) {
  static u8 u8KeepOut = 0; // Timer to keep from updating the display too fast.

  if (bDisplayUpToDate) {
    return;
  }

  if (u8KeepOut > 0) {
    u8KeepOut--;
    return;
  }

  u8KeepOut = 200;
  bDisplayUpToDate = TRUE;

  LcdCommand(LCD_HOME_CMD);
  LcdCommand(LCD_CLEAR_CMD);

  if (!UsbIsEnabled()) {
    LcdMessage(LINE1_START_ADDR, "USB is disabled");
    LcdMessage(LINE2_START_ADDR, "Button 1 to enable");
    return;
  }

  char acLine[U8_LCD_MAX_LINE_DISPLAY_SIZE + 1];

  char cOutState = stUsb.u8OutAlt != IFACE_AUDIO_OUT_ALT_0_BYTES ? 'O' : '-';
  char cInState = stUsb.u8InAlt != IFACE_AUDIO_IN_ALT_0_BYTES ? 'I' : '-';
  snprintf(acLine, sizeof(acLine), "USB %c%c %luHz %s", cInState, cOutState, u32SampleRate,
           stBuzzer.bEnabled ? "BZ" : "--");
  LcdMessage(LINE1_START_ADDR, acLine);

  const char *pcSrc = NULL;
  switch (eSource) {
  case SRC_LOOPBACK:
    pcSrc = "LOOP";
    break;

  case SRC_TRI_WAVE:
    pcSrc = "TRI";
    break;

  case SRC_SIN_WAVE:
    pcSrc = "SIN";
    break;

  default:
    pcSrc = "???";
    break;
  }

  const char *pcVol;
  if (stUsb.bMuted) {
    pcVol = "M";
  } else {
    if (stUsb.s16VolumeUsb == USB_AUDIO_VOLUME_SILENT) {
      pcVol = "-inf dB";
    } else {
      // newlib-nano doesn't support %f in printf, so manually break down
      // the value.

      // Convert from S8.8 into multiples of 0.01.
      s16 s16MilliDbs = (stUsb.s16VolumeUsb * 100 + 128) / 256;
      const char *pcSign = "";
      if (s16MilliDbs < 0) {
        pcSign = "-";
        s16MilliDbs = -s16MilliDbs;
      }

      static char acVolStr[11];
      snprintf(acVolStr, sizeof(acVolStr), "%s%d.%02d dB", pcSign, s16MilliDbs / 100, s16MilliDbs % 100);
      pcVol = acVolStr;
    }
  }

  snprintf(acLine, sizeof(acLine), "S:%s, V:%s", pcSrc, pcVol);
  LcdMessage(LINE2_START_ADDR, acLine);
}

static bool InitUsb(void) {
  stMainCfgDesc.u16TotLen = UsbDescListByteLen(stMainCfgList);
  stAudioCtrlHeaderDesc.u16TotalLength = UsbDescListByteLen(stAudioCtrlList);

  stUsb.u8ActiveCfg = NO_CFG;
  stUsb.u8OutAlt = IFACE_AUDIO_OUT_ALT_0_BYTES;
  stUsb.u8InAlt = IFACE_AUDIO_IN_ALT_0_BYTES;

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
    stUsb.u8InAlt = IFACE_AUDIO_IN_ALT_0_BYTES;
    bDisplayUpToDate = FALSE;

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

      case IFACE_AUDIO_INPUT:
        bSend = TRUE;
        u8Alt = stUsb.u8InAlt;
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
      bDisplayUpToDate = FALSE;

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
      switch (pstRequest->u16Index) {
      case IFACE_AUDIO_OUTPUT:
        if (pstRequest->u16Value < NUM_OUT_ALTS) {
          stUsb.u8OutAlt = (u8)pstRequest->u16Value;
          bDisplayUpToDate = FALSE;
          UsbNextPacket(EPT_CTRL);
          UsbCancelDma(EPT_AUDIO_OUT);
        }
        break;

      case IFACE_AUDIO_INPUT:
        if (pstRequest->u16Value < NUM_IN_ALTS) {
          stUsb.u8InAlt = (u8)pstRequest->u16Value;
          bDisplayUpToDate = FALSE;
          UsbNextPacket(EPT_CTRL);
          UsbCancelDma(EPT_AUDIO_IN);
        }
        break;
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
  case CLK_SRC_ID:
    SetClkSrcCtrl(u8Ctrl, u8Chan);
    break;

  case VOLUME_ID:
    SetVolumeCtrl(u8Ctrl, u8Chan);
    break;
  }

  bDisplayUpToDate = FALSE;

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
        .astRanges = {{MIN_SAMPLE_RATE, MAX_SAMPLE_RATE, 1}},
    };

    UsbWrite(EPT_CTRL, &stRanges, sizeof(stRanges));
  } else {
    UsbWrite(EPT_CTRL, &u32SampleRate, sizeof(u32SampleRate));
  }

  UsbNextPacket(EPT_CTRL);
}

static void SetClkSrcCtrl(u8 u8Ctrl, u8 u8Chan) {
  if (u8Ctrl != USB_AUDIO_CLK_SRC_CTRL_SAM_FREQ || u8Chan != 0) {
    return;
  }

  u32 u32NewRate;
  if (sizeof(u32NewRate) != UsbRead(EPT_CTRL, &u32NewRate, sizeof(u32NewRate))) {
    return;
  }

  if (u32NewRate < MIN_SAMPLE_RATE || u32NewRate > MAX_SAMPLE_RATE) {
    return;
  }

  u32SampleRate = u32NewRate;
  ApplySampleRate();
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

    if (stUsb.s16VolumeUsb == USB_AUDIO_VOLUME_SILENT) {
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

static void ApplySampleRate(void) {
  // Precalc various derived values from the sample rate.

  // Get the inverse in as precise a format as we can. Used for period advancement when synthesizing
  // wave forms.
  u32InvSampleRate = ((1 << 31) + u32SampleRate / 2) / u32SampleRate;

  // Setup the framelength accumulator. Used by various systems to regularly insert extra samples so
  // that the overall sample rate is as exact as possible.
  stFrameLen.u16Base = u32SampleRate / 1000;
  stFrameLen.u16Frac = u32SampleRate % 1000;
  stFrameLen.u16Accum = 0;
  stFrameLen.u16Curr = stFrameLen.u16Base;

  // Changing sample rate on the fly tends to make the PWM sample period feedback loop unstable.
  // Clear it out and re-seed with a frame of silence.
  PWMAbortAudio();

  // Setup some things used by the buzzer to set it's per-sample period.
  stBuzzer.u32ClocksPerSample = (CCLK_VALUE / u32SampleRate) << 8;
  // Make sure to get the fractional part to.
  u32 u32Rem = CCLK_VALUE - (stBuzzer.u32ClocksPerSample >> 8) * u32SampleRate;
  stBuzzer.u32ClocksPerSample += ((u32Rem << 8) + u32SampleRate / 2) / u32SampleRate;

  stBuzzer.s16SampleCountDiff = 0;
}

static void ResetAudioFrame(AudioFrame *pstFrame) {
  pstFrame->bInUse = FALSE;
  pstFrame->u16NumSamples = 0;
  pstFrame->stDma = (DmaInfo){0};
}

static AudioFrame *GetAvailableFrame(void) {
  for (u8 i = 0; i < NUM_AUDIO_FRAMES; i++) {
    if (!astFrames[i].bInUse) {
      astFrames[i].bInUse = TRUE;
      return &astFrames[i];
    }
  }

  return NULL;
}

static void UpdateFrameLen(void) {
  stFrameLen.u16Accum += stFrameLen.u16Frac;
  stFrameLen.u16Curr = stFrameLen.u16Base;

  if (stFrameLen.u16Accum >= 1000) {
    stFrameLen.u16Accum -= 1000;
    stFrameLen.u16Curr += 1;
  }
}

static AudioFrame *PrepareInFrame(void) {
  AudioFrame *pstFrame = GetAvailableFrame();
  if (!pstFrame) {
    return NULL;
  }

  bool bSynthed = FALSE;

  switch (eSource) {
  case SRC_LOOPBACK:
    if (!SetupUsbRead(pstFrame)) {
      ResetAudioFrame(pstFrame);
      pstFrame = NULL;
    }
    break;

  case SRC_TRI_WAVE:
    bSynthed = TRUE;
    SynthAudioFrame(pstFrame, SampleTriangle);
    break;

  case SRC_SIN_WAVE:
    bSynthed = TRUE;
    SynthAudioFrame(pstFrame, SampleSin);
    break;

  default:
    ResetAudioFrame(pstFrame);
    pstFrame = NULL;
    break;
  }

  if (bSynthed) {
    // No actual DMA, so simulate it.
    InitDma(pstFrame);
    pstFrame->stDma.eStatus = DMA_COMPLETE;
  }

  return pstFrame;
}

static AudioFrame *GetInFrame(void) {
  if (!stPipeline.pstInFrame) {
    return NULL;
  }

  if (stPipeline.pstInFrame->stDma.eStatus == DMA_ACTIVE) {
    return NULL;
  }

  AudioFrame *pstFrame = stPipeline.pstInFrame;
  stPipeline.pstInFrame = NULL;

  if (pstFrame->stDma.eStatus == DMA_COMPLETE) {
    pstFrame->u16NumSamples = pstFrame->stDma.u16XferLen / sizeof(u16);
  } else {
    ResetAudioFrame(pstFrame);
    pstFrame = NULL;
  }

  return pstFrame;
}

static void ClearSentFrames(void) {
  if (stPipeline.pstUsbOutFrame) {
    if (stPipeline.pstUsbOutFrame->stDma.eStatus != DMA_ACTIVE) {
      ResetAudioFrame(stPipeline.pstUsbOutFrame);
      stPipeline.pstUsbOutFrame = NULL;
    }
  }

  if (stPipeline.pstPwmFront->stDma.eStatus != DMA_ACTIVE) {
    PwmFrame *pstTmp = stPipeline.pstPwmFront;
    stPipeline.pstPwmFront = stPipeline.pstPwmBack;
    stPipeline.pstPwmBack = pstTmp;
  }
}

static void ProcessAudioPipeline(void) {
  ClearSentFrames();
  UpdateFrameLen();

  AudioFrame *pstFrame = GetInFrame();
  if (pstFrame) {
    ProcessAudioFrame(pstFrame);
    BuzzerProcess(pstFrame);
    SendToUsb(pstFrame);
  }

  if (!stPipeline.pstInFrame) {
    stPipeline.pstInFrame = PrepareInFrame();
  }
}

static void SynthAudioFrame(AudioFrame *pstFrame, SamplerFunc pfnSampler) {
  static u16 u16FrameErrAccum = 0;
  static s16 s16Period = 0;

  // clang-format off
  static const u16 au16Notes[] = {
     D3, A3, D4,
     F4, D4, A3,
     E4, F4, D4,
     F4, D4, A3,

     E3, A3, C4,
     E4, C4, A3,
     A4, E4, C4,
     E4, C4, A3,

     D3, A3, D4,
     F4, D4, A3,
     E4, F4, D4,
     F4, D4, A3,

     F3, A3, D4, F4, D4, A3,
     D3S, G3, C4, D4S, C4, G3,
  };
  // clang-format on
  static const u32 u32Notecount = sizeof(au16Notes) / sizeof(au16Notes[0]);
  static u32 u32NoteIdx = 0;
  static u8 u8FrameCtr = 250;

  if (--u8FrameCtr == 0) {
    u8FrameCtr = 250;
    if (++u32NoteIdx == u32Notecount) {
      u32NoteIdx = 0;
    }
  }
  u16 u16Note = au16Notes[u32NoteIdx];

  pstFrame->u16NumSamples = stFrameLen.u16Curr;

  if (u16FrameErrAccum >= 1000) {
    pstFrame->u16NumSamples += 1;
    u16FrameErrAccum -= 1000;
  }

  for (u8 u8Idx = 0; u8Idx < pstFrame->u16NumSamples; u8Idx++) {
    s16 s16Sample = pfnSampler(s16Period);

    pstFrame->as16Samples[u8Idx] = s16Sample;

    // NOTE: There's a multiply by 2 folded into this.
    // This is due to the range of a period being from -1.0 to 1.0.
    // The shift is based on period being 2^15, inv rate being 2^31, and wanting to multiply by a power of 2.
    s16Period += (u16Note * u32InvSampleRate + 0x2000) >> 15;
  }
}

static void ProcessAudioFrame(AudioFrame *pstFrame) {
  static u16 au16Energies[16];
  static const u8 u8NumEnergies = sizeof(au16Energies) / sizeof(au16Energies[0]);
  static u8 u8Idx;

  u32 u32Energy = 0;
  s16 s16VolumeMul = stUsb.bMuted ? 0 : stUsb.s16VolumeRaw;

  for (int i = 0; i < pstFrame->u16NumSamples; i++) {
    s32 s = pstFrame->as16Samples[i];
    s = (s * s16VolumeMul) >> 15;
    pstFrame->as16Samples[i] = s;

    u32Energy += abs(s);
  }

  au16Energies[u8Idx++] = u32Energy / pstFrame->u16NumSamples;
  if (u8Idx == u8NumEnergies) {
    u8Idx = 0;
  }

  u32Energy = 0;
  for (u8 i = 0; i < u8NumEnergies; i++) {
    u32Energy += au16Energies[i];
  }
  u32Energy /= u8NumEnergies;

  // clang-format off
  static const struct {
    LedNameType eLed;
    u16 u16Threshold;
  } astThresholds[] = {
      {WHITE,     0x6000},
      {PURPLE,    0x34f2},
      {BLUE,      0x1d33},
      {CYAN,      0x101a},
      {GREEN,     0x08e1},
      {YELLOW,    0x04e6},
      {ORANGE,    0x02b3},
      {RED,       0x017d},
      {LCD_BLUE,  0x00d2},
      {LCD_GREEN, 0x0074},
  };
  // clang-format on

  for (u8 i = 0; i < (sizeof(astThresholds) / sizeof(astThresholds[0])); i++) {
    if (u32Energy > astThresholds[i].u16Threshold) {
      LedOn(astThresholds[i].eLed);
    } else {
      LedOff(astThresholds[i].eLed);
    }
  }
}

static bool SetupUsbRead(AudioFrame *pstFrame) {
  if (stUsb.u8InAlt != IFACE_AUDIO_IN_ALT_200_BYTES) {
    // If the host isn't actively feeding in data treat it the same as getting silence.
    pstFrame->u16NumSamples = stFrameLen.u16Curr;
    memset(pstFrame->as16Samples, 0, sizeof(u16) * pstFrame->u16NumSamples);

    InitDma(pstFrame);
    pstFrame->stDma.eStatus = DMA_COMPLETE;
    return TRUE;
  }

  pstFrame->u16NumSamples = MAX_SAMPLES;
  InitDma(pstFrame);
  return UsbDmaRead(EPT_AUDIO_IN, &pstFrame->stDma);
}

static void SendToUsb(AudioFrame *pstFrame) {
  if (stUsb.u8OutAlt != IFACE_AUDIO_OUT_ALT_200_BYTES || stPipeline.pstUsbOutFrame) {
    ResetAudioFrame(pstFrame);
    return;
  }

  InitDma(pstFrame);
  if (!UsbDmaWrite(EPT_AUDIO_OUT, &pstFrame->stDma)) {
    ResetAudioFrame(pstFrame);
    return;
  }

  stPipeline.pstUsbOutFrame = pstFrame;
}

static void BuzzerProcess(AudioFrame *pstFrame) {
  if (!stBuzzer.bEnabled) {
    return;
  }

  if (stPipeline.pstPwmFront->stDma.eStatus != DMA_ACTIVE) {
    SendInitialBuzzerFrame();
  }

  if (stPipeline.pstPwmBack->stDma.eStatus == DMA_ACTIVE) {
    stBuzzer.u32Overruns += 1;
    return;
  }

  // Track difference in expected and actual samples.
  // This can drift by up to 2, but any more than that and we will assume a missed/extra sample
  // from the source. In that case the feedback loop will stretch/shrink sample time to accomodate.
  stBuzzer.s16SampleCountDiff += stFrameLen.u16Curr - pstFrame->u16NumSamples;

  // Track the target time for the sample to end at. Remember that the systick counter counts down!
  stBuzzer.u32FrameClock -= stBuzzer.u32ClocksPerSample * pstFrame->u16NumSamples;
  if (stBuzzer.s16SampleCountDiff < -2) {
    stBuzzer.s16SampleCountDiff += 1;
    stBuzzer.u32FrameClock -= stBuzzer.u32ClocksPerSample;
  } else if (stBuzzer.s16SampleCountDiff > 2) {
    stBuzzer.s16SampleCountDiff -= 1;
    stBuzzer.u32FrameClock += stBuzzer.u32ClocksPerSample;
  }
  // Bias by a frame time every, well, frame.
  stBuzzer.u32FrameClock += (U32_SYSTICK_COUNT * SYSTICK_DIVIDER) << 8;

  u16 u16SamplePeriod = stBuzzer.u32ClocksPerSample >> 8;
  // P-only PID control loop. There's a bit of a delay in the system due to the frame pipelining,
  // so using a fairly small p term of 0.1 to avoid oscillation problems.
  s16 s16Err = stBuzzer.s16TickErr; // In ticks.
  s16Err *= SYSTICK_DIVIDER;        // In clocks.
  // Combine division factors.
  s16 s16DivFactor = stFrameLen.u16Curr * 10;

  // Round up in the following division.
  if (s16Err > 0) {
    s16Err += s16DivFactor - 1;
  } else {
    s16Err -= s16DivFactor - 1;
  }
  s16Err /= s16DivFactor; // In clocks/per sample, scaled by P.

  // Positive error means measured was earlier than target, so need longer period to compensate.
  u16SamplePeriod += s16Err;

  static u32 u32Ctr = 500;
  if (--u32Ctr == 0) {
    u32Ctr = 500;
    DebugPrintf("P: ");
    DebugPrintNumber(u16SamplePeriod);
    if (stBuzzer.s16TickErr < 0) {
      DebugPrintf(" TE: -");
      DebugPrintNumber(-stBuzzer.s16TickErr);
    } else {
      DebugPrintf(" TE: ");
      DebugPrintNumber(stBuzzer.s16TickErr);
    }
    DebugLineFeed();
  }

  // Prepare and send the frame.
  ConvertToBuzzerFormat(pstFrame, stPipeline.pstPwmBack, u16SamplePeriod);
  stPipeline.pstPwmBack->u16ExpectedTick = ((stBuzzer.u32FrameClock >> 8) + (SYSTICK_DIVIDER / 2)) / SYSTICK_DIVIDER;

  PWMAudioSendFrame(&stPipeline.pstPwmBack->stDma, u16SamplePeriod);
}

static void SendInitialBuzzerFrame(void) {
  memset(stPipeline.pstPwmFront->au16Samples, 0, sizeof(stPipeline.pstPwmFront->au16Samples));
  stPipeline.pstPwmFront->stDma = (DmaInfo){
      .pvBuffer = stPipeline.pstPwmFront->au16Samples,
      .OnCompleteCb = BuzzerDmaCb,
  };

  stBuzzer.u32FrameClock = (BUZZER_TGT_TICK * SYSTICK_DIVIDER) << 8;
  stPipeline.pstPwmFront->u16ExpectedTick = BUZZER_TGT_TICK;

  // Do tick calculation last just to reduce initial error.
  u16 u16Tick = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;
  u16 u16Samples = (u16Tick - BUZZER_TGT_TICK) * SYSTICK_DIVIDER / (stBuzzer.u32ClocksPerSample >> 8);
  stPipeline.pstPwmFront->stDma.u16XferLen = u16Samples * 4; // 4 bytes, 2 channels.
  PWMAudioSendFrame(&stPipeline.pstPwmFront->stDma, stBuzzer.u32ClocksPerSample >> 8);
}

static void ConvertToBuzzerFormat(AudioFrame *pstInFrame, PwmFrame *pstOutFrame, u16 u16SamplePeriod) {
  for (u16 i = 0; i < pstInFrame->u16NumSamples; i++) {
    s32 s = pstInFrame->as16Samples[i];

    // Transform from a value in the range [-1.0, 1.0] in fixed point to a value in the range
    // [0, period].
    s = (s * u16SamplePeriod) >> 16;
    s += u16SamplePeriod >> 1;

    // Double it up for "stereo" (really because we are feeding two PWM channels).
    pstOutFrame->au16Samples[i * 2] = s;
    pstOutFrame->au16Samples[i * 2 + 1] = s;
  }

  pstOutFrame->stDma = (DmaInfo){
      .pvBuffer = &(pstOutFrame->au16Samples),
      .u16XferLen = pstInFrame->stDma.u16XferLen * 2,
      .OnCompleteCb = BuzzerDmaCb,
  };
}

static void BuzzerDmaCb(DmaInfo *pstDma) {
  if (pstDma->eStatus != DMA_COMPLETE) {
    return;
  }

  PwmFrame *pstFrame = (PwmFrame *)pstDma;
  u16 u16tick = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;
  stBuzzer.s16TickErr = u16tick - pstFrame->u16ExpectedTick;
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

    bDisplayUpToDate = FALSE;
  }

  if (WasButtonPressed(BUTTON1)) {
    ButtonAcknowledge(BUTTON1);

    eSource++;
    if (eSource >= SRC_COUNT) {
      eSource = 0;
    }

    bDisplayUpToDate = FALSE;
  }

  if (WasButtonPressed(BUTTON2)) {
    ButtonAcknowledge(BUTTON2);
    stBuzzer.bEnabled = !stBuzzer.bEnabled;
    bDisplayUpToDate = FALSE;
  }

  ProcessAudioPipeline();
  UpdateDisplay();

  static u32 u32Ctr = 200;
  if (--u32Ctr == 0) {
    u32Ctr = 200;

    if (G_u32BspPwmUnderruns) {
      DebugPrintNumber(G_u32BspPwmUnderruns);
      DebugPrintf(" underruns ");
    }

    if (stBuzzer.u32Overruns) {
      DebugPrintNumber(stBuzzer.u32Overruns);
      DebugPrintf(" overruns ");
    }

    if (G_u32BspPwmUnderruns || stBuzzer.u32Overruns) {
      DebugLineFeed();
      G_u32BspPwmUnderruns = 0;
      stBuzzer.u32Overruns = 0;
    }
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
