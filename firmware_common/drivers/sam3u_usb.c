// A full-speed USB driver for the SAM3U usb peripheral.
// High-speed USB is not currently supported.

// Notes on FIFO experimentation:
// - It's a true Fifo, random access isn't possible.
// - You can move single bytes at a time, but the address used must be aligned
//   ie. The last two bits of the offset in the FIFO must match the last two
//   bits of the address used to access the FIFO.
//
// The easiest way to manage the alignment requirement is to treat it as if each
// endpoint's buffer memory was mapped at the base of it's 64KiB window, but the
// driver must also ensure that data is only read/written in order.

//------------------------------------------------------------------------------
//         Headers
//------------------------------------------------------------------------------

#include "configuration.h"

//------------------------------------------------------------------------------
//         Literal Constants
//------------------------------------------------------------------------------

// Hardware supports 1 control pipe + 6 arbitrary endpoints.
#define USB_NUM_EPS 7

// From the data sheet, peripheral has a 4KiB FIFO shared among all endpoints.
#define FIFO_SIZE (1024 * 4)

// From the data sheet, USB FIFO memory window begins at this address.
#define FIFO_MAP_BASE 0x20180000

// Each endpoint gets 64KiB of memory for it's FIFO access port.
#define FIFO_CHANNEL_SZ (64 * 1024)

//------------------------------------------------------------------------------
//         Types
//------------------------------------------------------------------------------

/// @brief State tracked individualy for each endpoint.
typedef struct {
  /// @brief Applied configuration for the endpoint.
  UsbEndpointConfigType stConfig;

  /// @brief DMA packet in progress, or NULL if no transfer active.
  DmaInfo *pstDma;

  /// @brief Offset into the active packet (if there is one).
  u16 u16Offset;

  /// @brief Actual size of the active packet (if there is one).
  u16 u16PktSize;

  /// @brief If true there is an active packet and the endpoint is ready for
  /// reading/writing.
  bool bIsReady;
} EndptStateType;

typedef struct {
  u16 u16PacketSize;
  u8 u8NumPackets;
} EndptLimitsType;

//------------------------------------------------------------------------------
//         Memory Constants
//------------------------------------------------------------------------------

/// @brief The maximum possible sizes that can be used with each endpoint
/// according to the data sheet.
static const EndptLimitsType astLimits[USB_NUM_EPS] = {
    {.u16PacketSize = 64, .u8NumPackets = 1},   {.u16PacketSize = 512, .u8NumPackets = 2},
    {.u16PacketSize = 512, .u8NumPackets = 2},  {.u16PacketSize = 64, .u8NumPackets = 3},
    {.u16PacketSize = 64, .u8NumPackets = 3},   {.u16PacketSize = 1024, .u8NumPackets = 3},
    {.u16PacketSize = 1024, .u8NumPackets = 3},
};

//------------------------------------------------------------------------------
//         Interrupt accessable variables
//------------------------------------------------------------------------------

/// @brief This flag is set by the IRQ whenever a USB reset occurs, and is
/// cleared in thread context when handled.
///
/// Note that the hardware does something a bit strange: When a USB reset occurs
/// the peripheral's interrupt enable mask is forced to only having the reset
/// IRQ set.
static volatile bool bResetEvt;

//------------------------------------------------------------------------------
//         Standard Variables
//------------------------------------------------------------------------------

/// @brief States the driver can be in.
enum {
  /// @brief Some unrecoverable error occured. The driver will no longer
  /// function.
  DRIVER_STATE_ERR = -1,

  /// @brief The driver has not been initialized yet.
  DRIVER_STATE_NOT_INIT = 0,

  /// @brief Driver is not active, but no further setup is allowed (everything
  /// is locked in).
  DRIVER_STATE_DISABLED,

  /// @brief The driver is active, and USB data transfer is (likely) possible.
  DRIVER_STATE_ENABLED,
} eDriverState;

/// @brief The device configuration provided during setup, or NULL if that
/// hasn't happened yet.
static const UsbDriverConfigType *pstDriverCfg;

static struct {
  UsbRequestStatusType stStatus;
  UsbRequestHandlerCb fnHandler;
  UsbRequestCleanupCb fnCleanup;
  void *pvUserData;
  bool bActive;
} stRequest;

/// @brief Runtime state for each endpoint the hardware supports.
static EndptStateType astEndpts[USB_NUM_EPS];

/// @brief If true an address change is pending.
static bool bAddressPending;

/// @brief The new address to use when a pending address change is applied.
static u8 u8NewAddress;

//------------------------------------------------------------------------------
//         Inline Functions
//------------------------------------------------------------------------------

/// @brief Get the start of the memory window used to access an endpoint's FIFO
/// memory.
static inline void *GetFifoPtr(u8 u8Endpt_) { return (void *)(FIFO_MAP_BASE + FIFO_CHANNEL_SZ * u8Endpt_); }

static inline u16 RegFieldToSize(u8 u8Field_) { return 8u << u8Field_; }

static inline u8 SizeToRegField(u16 u16Size_) {
  u8 field = 0;

  while (RegFieldToSize(field) < u16Size_) {
    field++;
  }

  return field;
}

//------------------------------------------------------------------------------
//         Function Prototypes
//------------------------------------------------------------------------------

static void ResetAllCfg(void);
static void TerminateRequest(void);
static void ResetAllEpts(void);

static void ActivateController(void);
static bool HandleUsbReset(void);

static void ConfigEpt(u8 u8Endpt_, const UsbEndpointConfigType *pstCfg_);
static void ResetEpt(u8 u8Endpt_);
static void UpdateReadyState(u8 u8Endpt_);
static void CompleteDma(u8 u8Endpt_, DmaStatus eStatus);

static void ServiceControlPipe(void);
static void StartNewRequest(void);
static void HandleStandardRequest(UsbSetupPacketType *pstHeader_);
static void HandleSetAddress(UsbSetupPacketType *pstHeader_);
static void HandleGetStatus(UsbSetupPacketType *pstHeader_);
static void HandleSetFeature(UsbSetupPacketType *pstHeader_);
static void HandleClearFeature(UsbSetupPacketType *pstHeader_);

//------------------------------------------------------------------------------
//         Public Function Impls
//------------------------------------------------------------------------------

void UsbInitialize(void) {
  if (eDriverState != DRIVER_STATE_NOT_INIT) {
    return;
  }

  ResetAllCfg();
  eDriverState = DRIVER_STATE_DISABLED;
}

void UsbRunActiveState(void) {
  if (eDriverState != DRIVER_STATE_ENABLED) {
    return;
  }

  // A reset event always has priority and is handled the same no matter what.
  if (bResetEvt) {
    bResetEvt = FALSE;

    if (!HandleUsbReset()) {
      eDriverState = DRIVER_STATE_ERR;
    }
  } else {
    ServiceControlPipe();

    for (u8 u8Endpt = 1; u8Endpt < USB_NUM_EPS; u8Endpt++) {
      UpdateReadyState(u8Endpt);
    }
  }
}

bool UsbSetDriverConfig(const UsbDriverConfigType *pstConfig_) {
  if (pstConfig_ == NULL) {
    DebugPrintf("Null USB driver config ignored\r\n");
    return FALSE;
  }

  const char *pcErr = NULL;
  if (!UsbValidateEndpoints(&pstConfig_->stFullSpeedEp0Cfg, 0, NULL, &pcErr) ||
      (pstConfig_->bHighSpeedEnabled && !UsbValidateEndpoints(&pstConfig_->stHighSpeedEp0Cfg, 0, NULL, &pcErr))) {

    DebugPrintf("Ignoring USB driver config with invalid EP0: ");
    DebugPrintf((u8 *)pcErr);
    DebugLineFeed();
    return FALSE;
  }

  if (eDriverState == DRIVER_STATE_ENABLED) {
    UsbSetEnabled(FALSE);
  }
  pstDriverCfg = pstConfig_;
  ResetAllCfg();

  return TRUE;
}

bool UsbValidateEndpoints(const UsbEndpointConfigType *pstEp0Cfg_, u8 u8NumExtraEndpoints_,
                          const UsbEndpointConfigType *astConfigs_, const char **ppcErrorStrOut_) {
  // Avoid needing to do null checks at every error point.
  const char *dummy;
  if (ppcErrorStrOut_ == NULL) {
    ppcErrorStrOut_ = &dummy;
  }

  if (pstEp0Cfg_ == NULL) {
    *ppcErrorStrOut_ = "No EP0 config provided";
    return FALSE;
  }

  if (u8NumExtraEndpoints_ > (USB_NUM_EPS - 1)) {
    *ppcErrorStrOut_ = "Too many endpoints";
    return FALSE;
  }

  size_t szFifoMem = 0;

  for (u8 u8Idx = 0; u8Idx <= u8NumExtraEndpoints_; u8Idx++) {
    const UsbEndpointConfigType *pstCfg;
    if (u8Idx == 0) {
      pstCfg = pstEp0Cfg_;
    } else {
      pstCfg = &astConfigs_[u8Idx - 1];
    }

    const EndptLimitsType *pstLimits = &astLimits[u8Idx];

    if (u8Idx == 0) {
      if (pstCfg->eXferType != USB_XFER_CTRL) {
        *ppcErrorStrOut_ = "EP0 must be a control endpoint";
        return FALSE;
      }

      if (pstCfg->u16MaxPacketSize == 0 || pstCfg->u8NumPackets == 0) {
        *ppcErrorStrOut_ = "EP0 cannot be 0-sized";
        return FALSE;
      }

      if (pstCfg->bUseDma) {
        *ppcErrorStrOut_ = "EP0 cannot use DMA";
        return FALSE;
      }
    } else {
      if (pstCfg->eXferType == USB_XFER_CTRL) {
        *ppcErrorStrOut_ = "Non-default control pipe not supported";
        return FALSE;
      }
    }

    if (pstCfg->u16MaxPacketSize > pstLimits->u16PacketSize) {
      *ppcErrorStrOut_ = "Endpoint max packet size is too big";
      return FALSE;
    }

    if (pstCfg->u8NumPackets > pstLimits->u8NumPackets) {
      *ppcErrorStrOut_ = "Endpoint packet count is too big";
      return FALSE;
    }

    // Roundtrip to the register setting to reflect the actual amount of mem
    // used.
    szFifoMem += RegFieldToSize(SizeToRegField(pstCfg->u16MaxPacketSize));

    if (szFifoMem > FIFO_SIZE) {
      *ppcErrorStrOut_ = "Not enough memory in USB Fifo";
      return FALSE;
    }
  }

  *ppcErrorStrOut_ = "";
  return TRUE;
}

bool UsbSetEndpointsConfig(u8 u8NumExtraEndpoints_, const UsbEndpointConfigType *astConfigs_) {
  const char *pcErr = NULL;

  if (!UsbIsEnabled()) {
    DebugPrintf("Can't configure endpoints while not enabled\r\n");
    return FALSE;
  }

  const UsbEndpointConfigType *pstEp0 =
      UsbIsHighSpeed() ? &pstDriverCfg->stHighSpeedEp0Cfg : &pstDriverCfg->stFullSpeedEp0Cfg;

  if (!UsbValidateEndpoints(pstEp0, u8NumExtraEndpoints_, astConfigs_, &pcErr)) {
    DebugPrintf("Unable to apply endpoint config: ");
    DebugPrintf((u8 *)pcErr);
    DebugLineFeed();
    return FALSE;
  }

  for (u8 u8Idx = USB_NUM_EPS - 1; u8Idx > 0; u8Idx -= 1) {
    ResetEpt(u8Idx);
  }

  for (u8 u8Idx = 0; u8Idx < u8NumExtraEndpoints_; u8Idx++) {
    ConfigEpt(u8Idx + 1, &astConfigs_[u8Idx]);
  }

  return TRUE;
}

bool UsbSetEnabled(bool bIsEnabled_) {
  switch (eDriverState) {
  case DRIVER_STATE_ENABLED:
    if (!bIsEnabled_) {
      ResetAllCfg();
      eDriverState = DRIVER_STATE_DISABLED;
    }
    return TRUE;

  case DRIVER_STATE_DISABLED:
    if (bIsEnabled_) {
      if (pstDriverCfg == NULL) {
        DebugPrintf("Cannot enable USB without configuration\r\n");
        return FALSE;
      }

      eDriverState = DRIVER_STATE_ENABLED;
      ActivateController();
    }
    return TRUE;

  default:
    break;
  }

  return FALSE;
}

bool UsbIsEnabled(void) { return eDriverState == DRIVER_STATE_ENABLED; }

bool UsbIsSuspended(void) {
  // TODO
  return FALSE;
}

bool UsbIsHighSpeed(void) {
  if (!UsbIsEnabled()) {
    return FALSE;
  }

  return !!(UDPHS->UDPHS_INTSTA & UDPHS_INTSTA_SPEED);
}

u16 UsbGetFrame(void) {
  if (!UsbIsEnabled() || UsbIsSuspended()) {
    return 0;
  }

  return (UDPHS->UDPHS_FNUM & UDPHS_FNUM_FRAME_NUMBER_Msk) >> UDPHS_FNUM_FRAME_NUMBER_Pos;
}

bool UsbSetStall(u8 u8Endpt_, bool bIsStalling_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    return FALSE; // Use UsbFailRequest() instead.
  }

  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];

  if (!(pstEpt->UDPHS_EPTCTL & UDPHS_EPTCTL_EPT_ENABL)) {
    return FALSE;
  }

  if (UsbIsStalling(u8Endpt_) != bIsStalling_) {
    if (bIsStalling_) {
      pstEpt->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_FRCESTALL;
    } else {
      UDPHS->UDPHS_EPTRST = 1u << u8Endpt_;
      pstEpt->UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_FRCESTALL;
    }
  }

  return TRUE;
}

bool UsbIsStalling(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    return FALSE;
  }

  return !!(UDPHS->UDPHS_EPT[u8Endpt_].UDPHS_EPTSTA & UDPHS_EPTSTA_FRCESTALL);
}

u16 UsbWrite(u8 u8Endpt_, const void *pvSrc_, u16 u16MaxLen_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return 0;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];

  if (pstEpt->stConfig.bUseDma) {
    return 0;
  }

  if (!pstEpt->bIsReady || (pstEpt->stConfig.eDir != USB_EPT_DIR_TO_HOST)) {
    return 0;
  }

  u16 u16ChunkSz = pstEpt->u16PktSize - pstEpt->u16Offset;
  if (u16ChunkSz > u16MaxLen_) {
    u16ChunkSz = u16MaxLen_;
  }

  memcpy(GetFifoPtr(u8Endpt_) + pstEpt->u16Offset, pvSrc_, u16ChunkSz);
  pstEpt->u16Offset += u16ChunkSz;

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    stRequest.stStatus.u16RequestOffset += u16ChunkSz;
  }

  return u16ChunkSz;
}

bool UsbDmaWrite(u8 u8Endpt_, DmaInfo *pstDma_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (pstDma_ == NULL) {
    return FALSE;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];

  if (!pstEpt->stConfig.bUseDma) {
    return FALSE;
  }

  if (pstEpt->stConfig.eDir != USB_EPT_DIR_TO_HOST) {
    return FALSE;
  }

  // Need to atomically claim the DMA transfer, to make this function re-entrant safe.
  __disable_irq();
  if (pstEpt->pstDma) {
    __enable_irq();
    return FALSE;
  }
  pstEpt->pstDma = pstDma_;
  __enable_irq();

  pstEpt->pstDma = pstDma_;
  UdphsDma *pstDmaRegs = &UDPHS->UDPHS_DMA[u8Endpt_];

  pstDma_->eStatus = DMA_ACTIVE;
  pstDmaRegs->UDPHS_DMAADDRESS = (u32)pstDma_->pvBuffer;
  // Locked transfers might interfere with user code, due to sharing the same AHB slave (the RAM). For this reason the
  // burst lock is not enabled.
  pstDmaRegs->UDPHS_DMACONTROL = UDPHS_DMACONTROL_CHANN_ENB | UDPHS_DMACONTROL_END_B_EN | UDPHS_DMACONTROL_END_TR_IT |
                                 UDPHS_DMACONTROL_END_BUFFIT | UDPHS_DMACONTROL_BUFF_LENGTH(pstDma_->u16XferLen);

  return TRUE;
}

u16 UsbRead(u8 u8Endpt_, void *pvDst_, u16 u16MaxLen_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];

  if (pstEpt->stConfig.bUseDma) {
    return 0;
  }

  if (!pstEpt->bIsReady || (pstEpt->stConfig.eDir != USB_EPT_DIR_FROM_HOST)) {
    return 0;
  }

  u16 u16ChunkSz = pstEpt->u16PktSize - pstEpt->u16Offset;
  if (u16ChunkSz > u16MaxLen_) {
    u16ChunkSz = u16MaxLen_;
  }

  memcpy(pvDst_, GetFifoPtr(u8Endpt_) + pstEpt->u16Offset, u16ChunkSz);
  pstEpt->u16Offset += u16ChunkSz;

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    stRequest.stStatus.u16RequestOffset += u16ChunkSz;
  }

  return u16ChunkSz;
}

bool UsbDmaRead(u8 u8Endpt_, DmaInfo *pstDma_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (pstDma_ == NULL) {
    return FALSE;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];

  if (!pstEpt->stConfig.bUseDma) {
    return FALSE;
  }

  if (pstEpt->stConfig.eDir != USB_EPT_DIR_FROM_HOST) {
    return FALSE;
  }

  __disable_irq();
  if (pstEpt->pstDma) {
    __enable_irq();
    return FALSE;
  }
  pstEpt->pstDma = pstDma_;
  __enable_irq();

  pstEpt->pstDma = pstDma_;
  UdphsDma *pstDmaRegs = &UDPHS->UDPHS_DMA[u8Endpt_];

  pstDma_->eStatus = DMA_ACTIVE;
  pstDmaRegs->UDPHS_DMAADDRESS = (u32)pstDma_->pvBuffer;
  pstDmaRegs->UDPHS_DMACONTROL = UDPHS_DMACONTROL_CHANN_ENB | UDPHS_DMACONTROL_END_TR_EN | UDPHS_DMACONTROL_END_B_EN |
                                 UDPHS_DMACONTROL_END_TR_IT | UDPHS_DMACONTROL_END_BUFFIT |
                                 UDPHS_DMACONTROL_BUFF_LENGTH(pstDma_->u16XferLen);

  return TRUE;
}

void UsbCancelDma(u8 u8Endpt_) {
  if (u8Endpt_ > USB_NUM_EPS) {
    return;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];
  if (!pstEpt->stConfig.bUseDma) {
    return;
  }

  UdphsDma *pstDmaRegs = &UDPHS->UDPHS_DMA[u8Endpt_];
  UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Endpt_];
  pstDmaRegs->UDPHS_DMACONTROL = 0; // Abort in hardware.

  // Cleanup any partial transaction. Reading status bits clears them.
  u32 u32Status = pstDmaRegs->UDPHS_DMASTATUS;
  if (!(u32Status & UDPHS_DMASTATUS_END_TR_ST) && pstEpt->pstDma) {
    // Transaction didn't end normally, see if one was started. There's a small race here with a new packet being made
    // available between us stopping the DMA and checking, but for the API user that's indistinguisable from stopping a
    // DMA transfer that was started but hadn't transferred a byte yet.

    if (pstRegs->UDPHS_EPTSTA & UDPHS_EPTSTA_BYTE_COUNT_Msk) {
      if (pstEpt->stConfig.eDir == USB_EPT_DIR_TO_HOST) {
        pstRegs->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_RXRDY_TXKL;
      } else {
        pstRegs->UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_RXRDY_TXKL;
      }
    }

    // Can finally signal the user code that the DMA was aborted.
    CompleteDma(u8Endpt_, DMA_ABORTED);
  }
}

bool UsbNextPacket(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Endpt_];
  EndptStateType *pstState = &astEndpts[u8Endpt_];
  bool bFinishRequest = FALSE;

  if (!pstState->bIsReady || pstState->stConfig.bUseDma) {
    return FALSE;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    if (!stRequest.bActive) {
      return FALSE;
    }

    if (pstState->u16Offset != pstState->u16PktSize) {
      if (pstState->stConfig.eDir == USB_EPT_DIR_TO_HOST) {
        // Short packet indicates early termination of the request.
        bFinishRequest = TRUE;
      } else {
        // Make sure the request offset reflects the skipped data.
        stRequest.stStatus.u16RequestOffset += pstState->u16PktSize - pstState->u16Offset;
      }
    }

    if (stRequest.stStatus.u16RequestOffset == stRequest.stStatus.stHeader.u16Length) {
      // All data consumed, this request is finished.
      bFinishRequest = TRUE;
    }
  }

  pstState->bIsReady = FALSE;
  pstState->u16Offset = 0;
  pstState->u16PktSize = 0;

  if (pstState->stConfig.eDir == USB_EPT_DIR_TO_HOST) {
    pstRegs->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_TXRDY;
  } else {
    pstRegs->UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_RXRDY_TXKL;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    // Need to send a 0-length packet at the end of OUT transfers.
    if ((pstState->stConfig.eDir == USB_EPT_DIR_FROM_HOST) && bFinishRequest) {
      pstRegs->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_TXRDY;
    }

  } else {
    // Transfers still ongoing, might be ready right away if there's multiple
    // buffers.
    UpdateReadyState(u8Endpt_);
  }

  if (bFinishRequest) {
    TerminateRequest();
  }

  return TRUE;
}

bool UsbIsPacketReady(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  return !UsbIsStalling(u8Endpt_) && astEndpts[u8Endpt_].bIsReady;
}

u16 UsbGetPktOffset(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return 0;
  }

  return astEndpts[u8Endpt_].u16Offset;
}

u16 UsbGetPktSize(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return 0;
  }

  return astEndpts[u8Endpt_].u16PktSize;
}

const UsbSetupPacketType *UsbGetCurrentRequest(void) {
  if (stRequest.bActive) {
    return &stRequest.stStatus.stHeader;
  }

  return NULL;
}

bool UsbAcceptRequest(UsbRequestHandlerCb fnRequestHandler_, UsbRequestCleanupCb fnRequestCleanup_, void *pvUserData_) {
  if (fnRequestHandler_ == NULL) {
    return FALSE;
  }

  // Request must be active and not already assigned a handler.
  if (!stRequest.bActive || stRequest.fnHandler != NULL) {
    return FALSE;
  }

  stRequest.fnHandler = fnRequestHandler_;
  stRequest.fnCleanup = fnRequestCleanup_;
  stRequest.pvUserData = pvUserData_;

  return TRUE;
}

void UsbFailRequest(void) {
  if (!stRequest.bActive) {
    return;
  }

  UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP].UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_FRCESTALL;
  TerminateRequest();
}

void UDPD_IrqHandler(void) {
  if (UDPHS->UDPHS_INTSTA & UDPHS_INTSTA_INT_SOF) {
    UDPHS->UDPHS_CLRINT = UDPHS_CLRINT_INT_SOF;
    u32 u32Now = (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;

    // Make sys tick lag the actual SOF, should ensure that packets are
    // definitely ready by the time user code runs.
    static const s32 s32Diff = U32_SYSTICK_COUNT / 100;

    SysTickSyncEvt(u32Now, s32Diff);
  }

  if (UDPHS->UDPHS_INTSTA & UDPHS_INTSTA_ENDRESET) {
    bResetEvt = TRUE;
    UDPHS->UDPHS_CLRINT = UDPHS_CLRINT_ENDRESET;
  }

  for (u8 dmaIdx = 0; dmaIdx < UDPHSDMA_NUMBER; dmaIdx++) {
    u32 u32Mask = UDPHS_INTSTA_DMA_1 << dmaIdx;

    if (UDPHS->UDPHS_INTSTA & u32Mask) {
      UDPHS->UDPHS_CLRINT = u32Mask;
      u16 u16Rem = (UDPHS->UDPHS_DMA[dmaIdx + 1].UDPHS_DMASTATUS & UDPHS_DMASTATUS_BUFF_COUNT_Msk) >>
                   UDPHS_DMASTATUS_BUFF_COUNT_Pos;
      astEndpts[dmaIdx + 1].pstDma->u16XferLen -= u16Rem;
      astEndpts[dmaIdx + 1].bIsReady = FALSE;

      CompleteDma(dmaIdx + 1, DMA_COMPLETE);
    }
  }
}

//------------------------------------------------------------------------------
//         Private Function Implementations
//------------------------------------------------------------------------------

/// @brief Reset the peripheral and associated state back to a default detached
/// state.
static void ResetAllCfg(void) {
  NVIC_DisableIRQ(UDPHS_IRQn);

  TerminateRequest();
  ResetAllEpts();

  // Switch to a (simulated) detached state. This matches the reset value of the
  // register
  UDPHS->UDPHS_CTRL = UDPHS_CTRL_DETACH;

  // No test modes, set requested speed capabilities.
  if (pstDriverCfg == NULL || pstDriverCfg->bHighSpeedEnabled) {
    UDPHS->UDPHS_TST = UDPHS_TST_SPEED_CFG_NORMAL;
  } else {
    UDPHS->UDPHS_TST = UDPHS_TST_SPEED_CFG_FULL_SPEED;
  }

  // Hardware automatically resets the IEN register when a USB reset happens, so
  // leave as 0 for now. We will enable the interrupts that are interesting in
  // the reset handler.
  UDPHS->UDPHS_IEN = 0;
  UDPHS->UDPHS_CLRINT = ~0;

  bResetEvt = FALSE;
  bAddressPending = FALSE;

  NVIC_ClearPendingIRQ(UDPHS_IRQn);
  NVIC_EnableIRQ(UDPHS_IRQn);
}

static void TerminateRequest(void) {
  if (!stRequest.bActive) {
    return;
  }

  astEndpts[USB_DEF_CTRL_EP].u16Offset = 0;
  astEndpts[USB_DEF_CTRL_EP].u16PktSize = 0;
  astEndpts[USB_DEF_CTRL_EP].bIsReady = FALSE;

  stRequest.bActive = FALSE;
  if (stRequest.fnCleanup != NULL) {
    stRequest.fnCleanup(stRequest.pvUserData);
  }

  stRequest.fnHandler = NULL;
  stRequest.fnCleanup = NULL;
  stRequest.pvUserData = NULL;
}

static void ResetAllEpts(void) {
  for (u8 u8Idx = 0; u8Idx < USB_NUM_EPS; u8Idx++) {
    ResetEpt(u8Idx);
  }
}

/// @brief Enable the peripheral.
/// This assumes the peripheral is currently in the detached state.
/// It will transition to an attached state and cause the USB handshaking
/// process with the host to start.
static void ActivateController(void) {
  // First disable the pulldowns, as having them enabled at the same time as the
  // UTMI is not recommended.
  UDPHS->UDPHS_CTRL |= UDPHS_CTRL_PULLD_DIS;
  // Now enable the controller.
  UDPHS->UDPHS_CTRL |= UDPHS_CTRL_EN_UDPHS;
  UDPHS->UDPHS_CTRL &= ~UDPHS_CTRL_DETACH;
}

/// @brief Process a USB reset event. This will reset the device to having no
/// active configuration, but with a functional control pipe for accepting
/// requests.
static bool HandleUsbReset(void) {
  TerminateRequest();
  ResetAllEpts();

  // The only event that we need to handle in the IRQ is the SOF interrupt, as
  // it needs to capture accurate timing in order to phase-lock the systick
  // timer.
  UDPHS->UDPHS_IEN = UDPHS_IEN_INT_SOF;

  const UsbEndpointConfigType *pstCfg =
      UsbIsHighSpeed() ? &pstDriverCfg->stHighSpeedEp0Cfg : &pstDriverCfg->stFullSpeedEp0Cfg;
  ConfigEpt(USB_DEF_CTRL_EP, pstCfg);

  pstDriverCfg->pfnEventHandler(USB_EVT_RESET);
  return TRUE;
}

static void ConfigEpt(u8 u8Endpt_, const UsbEndpointConfigType *pstCfg_) {
  EndptStateType *pstState = &astEndpts[u8Endpt_];
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];

  u8 u8SizeField = SizeToRegField(pstCfg_->u16MaxPacketSize);

  pstState->stConfig = *pstCfg_;
  pstState->stConfig.u16MaxPacketSize = RegFieldToSize(u8SizeField);

  u32 u32Reg = UDPHS_EPTCFG_EPT_SIZE(u8SizeField);
  if (pstCfg_->eDir == USB_EPT_DIR_IN) {
    u32Reg |= UDPHS_EPTCFG_EPT_DIR;
  }

  // The register was defined to be directly compatible with the USB spec.
  u32Reg |= UDPHS_EPTCFG_EPT_TYPE(pstCfg_->eXferType);
  u32Reg |= UDPHS_EPTCFG_BK_NUMBER(pstCfg_->u8NumPackets);
  u32Reg |= UDPHS_EPTCFG_NB_TRANS(1); // TODO: make configurable.

  pstEpt->UDPHS_EPTCFG = u32Reg;

  if (pstCfg_->bUseDma) {
    pstEpt->UDPHS_EPTCTLENB = UDPHS_EPTCTLENB_AUTO_VALID;
    UDPHS->UDPHS_IEN |= UDPHS_IEN_DMA_1 << (u8Endpt_ - 1);
  }

  if (pstCfg_->u8NumPackets) {
    pstEpt->UDPHS_EPTCTLENB = UDPHS_EPTCTLENB_EPT_ENABL;
  }
}

static void ResetEpt(u8 u8Endpt_) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];
  EndptStateType *pstState = &astEndpts[u8Endpt_];

  pstEpt->UDPHS_EPTCTLDIS = UDPHS_EPTCTLDIS_EPT_DISABL;
  UDPHS->UDPHS_EPTRST = 1u << u8Endpt_;
  pstEpt->UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_TOGGLESQ;
  pstEpt->UDPHS_EPTCFG = 0;

  memset(pstState, 0, sizeof(*pstState));

  if (u8Endpt_ > 0) {
    UdphsDma *pstDma = &UDPHS->UDPHS_DMA[u8Endpt_];
    pstDma->UDPHS_DMACONTROL = 0;
    pstDma->UDPHS_DMAADDRESS = 0;
    pstDma->UDPHS_DMANXTDSC = 0;
    pstDma->UDPHS_DMASTATUS = pstDma->UDPHS_DMASTATUS; // Recommended method to clear-by-read from the data sheet.

    CompleteDma(u8Endpt_, DMA_ABORTED);
  }
}

/// @brief Update the ready status of an ednpoint based on the state reflected
/// in the peripheral registers.
static void UpdateReadyState(u8 u8Endpt_) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];
  EndptStateType *pstState = &astEndpts[u8Endpt_];

  if (pstState->bIsReady || pstState->pstDma != NULL) {
    // Either the endpoint is already ready, or a DMA is still in progress that needs to be waited for.
    return;
  }

  // Careful, TXRDY being set actually means all tx buffers are used/busy.
  if ((pstState->stConfig.eDir == USB_EPT_DIR_TO_HOST) && !(pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_TXRDY)) {
    pstState->bIsReady = TRUE;
    pstState->u16PktSize = pstState->stConfig.u16MaxPacketSize;

    if (u8Endpt_ == USB_DEF_CTRL_EP) {
      u16 u16Remain = stRequest.stStatus.stHeader.u16Length - stRequest.stStatus.u16RequestOffset;
      if (u16Remain < pstState->u16PktSize) {
        pstState->u16PktSize = u16Remain;
      }
    }
  }

  if ((pstState->stConfig.eDir == USB_EPT_DIR_FROM_HOST) && (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_RXRDY_TXKL)) {
    pstState->bIsReady = TRUE;
    pstState->u16PktSize = (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_BYTE_COUNT_Msk) >> UDPHS_EPTSTA_BYTE_COUNT_Pos;
  }
}

static void CompleteDma(u8 u8Endpt_, DmaStatus eStatus) {
  EndptStateType *pstEpt = &astEndpts[u8Endpt_];
  if (!pstEpt->pstDma) {
    return;
  }

  DmaInfo *pstDma = pstEpt->pstDma;
  pstEpt->pstDma = NULL;
  pstDma->eStatus = eStatus;

  if (pstDma->OnCompleteCb) {
    pstDma->OnCompleteCb(pstDma);
  }
}

/// @brief Do all periodic processing needed for the control pipe.
static void ServiceControlPipe(void) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP];

  /// Shouldn't really ever happen, it would mean the host ignored our stated
  /// max packet size.
  if (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_ERR_OVFLW) {
    UsbFailRequest();
    return;
  }

  if (bAddressPending && !(pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_TXRDY)) {
    // Can apply the address change now as the status packet is done sending.
    UDPHS->UDPHS_CTRL &= ~(UDPHS_CTRL_DEV_ADDR_Msk | UDPHS_CTRL_FADDR_EN);
    UDPHS->UDPHS_CTRL |= UDPHS_CTRL_DEV_ADDR(u8NewAddress) | UDPHS_CTRL_FADDR_EN;
    bAddressPending = FALSE;
  }

  if (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_RX_SETUP) {
    StartNewRequest();
  }

  if (stRequest.bActive) {
    UpdateReadyState(USB_DEF_CTRL_EP);

    if (astEndpts[USB_DEF_CTRL_EP].bIsReady) {
      stRequest.fnHandler(&stRequest.stStatus, stRequest.pvUserData);
    }
  }
}

/// @brief Begin processing a new control pipe request.
static void StartNewRequest(void) {
  TerminateRequest();

  UsbSetupPacketType *pstHeader = &stRequest.stStatus.stHeader;

  // Take advantage of the fact that on the SAM3U the structure will match the
  // USB defined layout.
  memcpy(pstHeader, GetFifoPtr(USB_DEF_CTRL_EP), sizeof(*pstHeader));

  // Clear out the status packet imemdiately so that the handlers can do things
  // synchronously if more convenient.
  UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP].UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_RX_SETUP;

  EndptStateType *pstState = &astEndpts[USB_DEF_CTRL_EP];

  if (pstHeader->stRequestType.eDir == USB_REQ_DIR_DEV_TO_HOST) {
    pstState->stConfig.eDir = USB_EPT_DIR_TO_HOST;
  } else {
    pstState->stConfig.eDir = USB_EPT_DIR_FROM_HOST;
  }

  stRequest.stStatus.u16RequestOffset = 0;
  stRequest.bActive = TRUE;

  if (pstHeader->u16Length > 0) {
    UpdateReadyState(USB_DEF_CTRL_EP);
  } else {
    pstState->bIsReady = TRUE;
  }

  if (pstHeader->stRequestType.eType == USB_REQ_TYPE_STANDARD) {
    HandleStandardRequest(pstHeader);
  }

  if (stRequest.bActive && stRequest.fnHandler == NULL) {
    // No one handled it yet.
    pstDriverCfg->pfnEventHandler(USB_EVT_REQUEST);
  }

  if (stRequest.bActive && stRequest.fnHandler == NULL) {
    // No one will continue the request, so just fail it now.
    UsbFailRequest();
  }
}

/// @brief Handler for all standard requests.
static void HandleStandardRequest(UsbSetupPacketType *pstHeader_) {

  switch (pstHeader_->u8RequestId) {
  case USB_REQ_SET_ADDRESS:
    HandleSetAddress(pstHeader_);
    break;

  case USB_REQ_GET_STATUS:
    HandleGetStatus(pstHeader_);
    break;

  case USB_REQ_SET_FEATURE:
    HandleSetFeature(pstHeader_);
    break;

  case USB_REQ_CLEAR_FEATURE:
    HandleClearFeature(pstHeader_);
    break;

  default:
    break;
  }
}

static void HandleSetAddress(UsbSetupPacketType *pstHeader_) {
  // Annoyingly the address can't actually be changed until the transaction is
  // complete.
  u8NewAddress = (u8)pstHeader_->u16Value;
  bAddressPending = TRUE;
  UsbNextPacket(USB_DEF_CTRL_EP);
}

static void HandleGetStatus(UsbSetupPacketType *pstHeader_) {
  bool bRequestGood = FALSE;
  u16 u16Status = 0;

  if (pstHeader_->u16Length >= sizeof(u16Status)) {
    switch (pstHeader_->stRequestType.eTgt) {
    case USB_REQ_TGT_DEV:
      // None of the optional features supported right now.
      bRequestGood = TRUE;
      break;

    case USB_REQ_TGT_IFACE:
      // There are no status bits for interfaces defined.
      bRequestGood = TRUE;
      break;

    case USB_REQ_TGT_EPT:
      if (pstHeader_->u16Index < USB_NUM_EPS) {
        if (UsbIsStalling(pstHeader_->u16Index)) {
          u16Status |= 1u << 0;
        }
        bRequestGood = TRUE;
      }
      break;

    default:
      break;
    }
  }

  if (bRequestGood) {
    UsbWrite(USB_DEF_CTRL_EP, &u16Status, sizeof(u16Status));
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}

static void HandleSetFeature(UsbSetupPacketType *pstHeader_) {
  // Remote wakeup not supported so is ignored.
  // Test mode not implemented either (only required for high-speed devices)
  // That leaves HALT as the only other feature.

  if (pstHeader_->u16Value != USB_FEAT_ID_ENDPOINT_HALT) {
    UsbFailRequest();
  }

  if (pstHeader_->stRequestType.eTgt != USB_REQ_TGT_EPT) {
    UsbFailRequest();
  }

  if (UsbSetStall(pstHeader_->u16Index, TRUE)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}

static void HandleClearFeature(UsbSetupPacketType *pstHeader_) {
  // Remote wakeup not supported so is ignored.
  // Test mode not implemented either (only required for high-speed devices)
  // That leaves HALT as the only other feature.

  if (pstHeader_->u16Value != USB_FEAT_ID_ENDPOINT_HALT) {
    UsbFailRequest();
  }

  if (pstHeader_->stRequestType.eTgt != USB_REQ_TGT_EPT) {
    UsbFailRequest();
  }

  if (UsbSetStall(pstHeader_->u16Index, FALSE)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}
