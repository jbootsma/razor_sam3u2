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

// TODO: Still need to implement suspend/resume support.

//------------------------------------------------------------------------------
//         Headers
//------------------------------------------------------------------------------

#include "configuration.h"

//------------------------------------------------------------------------------
//         Literal Constants
//------------------------------------------------------------------------------

// Hardware supports 1 control pipe + 6 arbitrary endpoints.
#define USB_NUM_EPS 7

// Everything but the control pipe has a configurable size.
#define USB_ALLOCABLE_EPS (USB_NUM_EPS - 1)

// Control pipe always uses max size of 64 on full-speed devices.
#define CTRL_PKT_SZ 64

// From the data sheet, peripheral has a 4KiB FIFO shared among all endpoints.
#define FIFO_SIZE (1024 * 4)

// From the data sheet, USB FIFO memory window begins at this address.
#define FIFO_MAP_BASE 0x20180000

// Each endpoint gets 64KiB of memory for it's FIFO access port.
#define FIFO_CHANNEL_SZ (64 * 1024)

// The driver doesn't support multiple languages, only US ENGLISH for now.
#define US_ENGLISH_LANGID (0x0409)

// FIXME: Need to determine what the actual max on the board is. Not allowed to
// draw more until after cfg though, so would need infra in place for it.
// Biggest concern is if the LEDs on the peripherals or the screen can push
// it to far. The core @48 MHz theoretically draws ~50 mA worst case.
// If powering anything off-board definitely need to make this BSP/App
// configurable.
//
// 100 mA.
#define CFG_FIXED_PWR 50

// Configuration attribute field.
// D7 must be set to 1 according to spec.
// Don't support any of the other features.
#define CFG_FIXED_ATTRIB 0x80

// All descriptors have a common header.
#define DESC_LENGTH_OFFSET 0
#define DESC_TYPE_OFFSET 1
#define DESC_HEADER_SZ 2

// Standard descriptor types.
enum {
  DESC_TYPE_DEV = 1,
  DESC_TYPE_CFG = 2,
  DESC_TYPE_STRING = 3,
  DESC_TYPE_IFACE = 4,
  DESC_TYPE_ENDPT = 5,
  DESC_TYPE_DEV_QUAL = 6,
  DESC_TYPE_ALT_SPEED = 7,
  DESC_TYPE_IFACE_PWR = 8,
};

// Definitions for the layout of each standard descriptor.

#define DEV_DESC_USB_VER_OFFSET 2
#define DEV_DESC_CLASS_OFFSET 4
#define DEV_DESC_EP0_PKT_SZ 7
#define DEV_DESC_ID_OFFSET 8
#define DEV_DESC_DEV_VER_OFFSET 12
#define DEV_DESC_MFGR_NAME_OFFSET 14
#define DEV_DESC_PROD_NAME_OFFSET 15
#define DEV_DESC_SERIAL_NUM_OFFSET 16
#define DEV_DESC_NUM_CFGS_OFFSET 17
#define DEV_DESC_SZ 18

#define CFG_DESC_TOTAL_LEN_OFFSET 2
#define CFG_DESC_NUM_IFACES_OFFSET 4
#define CFG_DESC_CFG_VAL_OFFSET 5
#define CFG_DESC_CFG_NAME_OFFSET 6
#define CFG_DESC_ATTRIB_OFFSET 7
#define CFG_DESC_MAX_POWER_OFFSET 8
#define CFG_DESC_SZ 9

#define IFACE_DESC_IDX_OFFSET 2
#define IFACE_DESC_ALT_OFFSET 3
#define IFACE_DESC_NUM_EPS_OFFSET 4
#define IFACE_DESC_CLASS_OFFSET 5
#define IFACE_DESC_NAME_OFFSET 8
#define IFACE_DESC_SZ 9

#define ENDPT_DESC_ADDR_OFFSET 2
#define ENDPT_DESC_ATTRIB_OFFSET 3
#define ENDPT_DESC_PKT_SZ_OFFSET 4
#define ENDPT_DESC_INTERVAL_OFFSET 6
#define ENDPT_DESC_SZ 7

// All standard request IDs from the spec.

enum {
  RQST_GET_STATUS = 0,
  RQST_CLEAR_FEATURE = 1,
  RQST_SET_FEATURE = 3,
  RQST_SET_ADDRESS = 5,
  RQST_GET_DESCRIPTOR = 6,
  RQST_SET_DESCRIPTOR = 7,
  RQST_GET_CONFIGURATION = 8,
  RQST_SET_CONFIGURATION = 9,
  RQST_GET_INTERFACE = 10,
  RQST_SET_INTERFACE = 11,
  RQST_SYNCH_FRAME = 12,
};

// Feature number specified in the spec.

enum {
  FEAT_EPT_HALT = 0,
  FEAT_REMOTE_WKP = 1,
  FEAT_TEST_MODE = 2,
};

//------------------------------------------------------------------------------
//         Types
//------------------------------------------------------------------------------

/// @brief Runtime data tracked for each interface.
typedef struct IfaceDataType {
  /// @brief Next interface in the list of alternates.
  struct IfaceDataType *pstNext;

  /// @brief The info provded during setup, mostly used for the callback.
  const UsbIfaceInfoType *pstInfo;
} IfaceDataType;

/// @brief Runtime data tracked across an interface and it's alternates.
typedef struct IfaceListType {
  /// @brief The next non-alt interface
  struct IfaceListType *pstNext;

  /// @brief All alternates for this interface, starting with the default.
  struct IfaceDataType *pstAlts;

  /// @brief Which alternate setting is currently selected.
  u8 u8ActiveIface;
} IfaceListType;

/// @brief Allocated memory size for an endpoint in a configuration.
typedef struct {
  /// @brief The memory size allocated for each packet.
  u16 u16PacketSize;

  /// @brief The number of packets allocated for this endpoint.
  u8 u8NumPackets;
} EndptSizeType;

/// @brief Runtime data tracked for each configuration.
typedef struct ConfigDataType {
  /// @brief Next config in the list.
  struct ConfigDataType *pstNextCfg;

  /// @brief The info struct passed during initial setup. Used mainly for the
  /// event handler.
  const UsbConfigInfoType *pstInfo;

  /// @brief All interfaces setup for this configuration.
  IfaceListType *pstIfaces;

  /// @brief The amount of memory allocated to each endpoint in this
  /// configuration.
  EndptSizeType astSizes[USB_ALLOCABLE_EPS];
} ConfigDataType;

/// @brief State tracked individuallt for each endpoint.
typedef struct {
  /// @brief The maximum packet size set in the active interface owning the
  /// endpoint.
  u16 u16MaxPktSize;

  /// @brief Offset into the active packet (if there is one).
  u16 u16Offset;

  /// @brief Actual size of the active packet (if there is one).
  u16 u16PktSize;

  /// @brief Owning interface for the endpoint. N/A for the control pipe.
  u8 u8Iface;

  /// @brief If true there is an active packet and the endpoint is ready for
  /// reading/writing.
  bool bIsReady;

  /// @brief If true the endpoint is for writing (IN transfers from host's point
  /// of view).
  bool bIsWrite;
} EndptStateType;

//------------------------------------------------------------------------------
//         Memory Constants
//------------------------------------------------------------------------------

/// @brief The maximum possible sizes that can be used with each endpoint
/// according to the data sheet.
static const EndptSizeType astMaxSizes[USB_ALLOCABLE_EPS] = {
    {.u16PacketSize = 512, .u8NumPackets = 2},
    {.u16PacketSize = 512, .u8NumPackets = 2},
    {.u16PacketSize = 64, .u8NumPackets = 3},
    {.u16PacketSize = 64, .u8NumPackets = 3},
    {.u16PacketSize = 1024, .u8NumPackets = 3},
    {.u16PacketSize = 1024, .u8NumPackets = 3},
};

/// @brief Version of the USB spec this driver (currently partially) implements.
static const UsbVersionType UsbSpecVersion = {
    .u8Major = 2,
    .u8Minor = 0,
};

//------------------------------------------------------------------------------
//         Interrupt accessable variables
//------------------------------------------------------------------------------

/// @brief This flag is set by the IRQ whenever a USB reset occurs, and is
/// cleared in thread context when handled.
///
/// Note that the hardware does something a bit strange: When a USB reset occurs
/// the peripheral's set of enabled interrupts is forced to be only the USB
/// reset.
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

  /// @brief The driver has been initialized, and it is okay to do initial
  /// setup.
  DRIVER_STATE_CFG,

  /// @brief Driver is not active, but no further setup is allowed (everything
  /// is locked in).
  DRIVER_STATE_DISABLED,

  /// @brief The driver is active, and USB data transfer is (likely) possible.
  DRIVER_STATE_ENABLED,
} eDriverState;

/// @brief Table of string descriptors registered with the driver.
static const char *apcUsbStrings[U8_USB_MAX_STRINGS];

/// @brief Track available memory in the memory pool used for descriptors and
/// runtime data.
///
/// Data is alloced in two ways:
/// 1. From the front of the pool for descriptors. This allows the driver to
/// build up the configuration descriptors in a piecewise fashion and have them
/// contiguous for bulk transfer to the host.
///
/// 2. From the back of the pool for arbitrary stucts, which will be aligned to
/// 4 byte boundaries. This is for more arbitrary data.
///
/// The function implementing these allocations are DescriptorAlloc() and
/// StructAlloc()
static struct {
  /// @brief Front of the free area, location of the next DescriptorAlloc()
  u8 *pFront;

  /// @brief End of the free area, location of the next StructAlloc()
  u8 *pBack;
} stArena;

/// @brief The device configuration provided during setup, or NULL if that
/// hasn't happened yet.
static const UsbDeviceInfoType *pstDevInfo;

/// @brief The device descriptor in memory. It will be followed by the
/// configuration descriptors.
static u8 *pu8DevDesc;

/// @brief List of configurations that have been setup for the device.
static ConfigDataType *pstFirstCfg;

/// @brief The active device configuration.
/// During setup this is the one new interfaces are being added to.
/// During operation it's the configuration that was selected as active by the
/// host.
static ConfigDataType *pstCurrCfg;

/// @brief Descriptor to match pstCurrCfg.
static u8 *pu8CurrCfgDesc;

/// @brief Descriptor of the current interface during setup.
static u8 *pu8CurrIfaceDesc;

/// @brief The active request handler for the current control request.
static UsbRequestHandlerType pfnRequestHandler;

/// @brief Parameter to pass to the request handler.
static void *pvRequestParam;

/// @brief Info about the current control request if one is in progress.
/// If no reuest is active the bIsTerminated field will be TRUE.
static UsbRequestStatusType stCurrRequest;

/// @brief Runtime state for each endpoint the hardware supports.
static EndptStateType astEndpts[USB_NUM_EPS];

/// @brief If true an address change is pending.
static bool bAddressPending;

/// @brief The new address to use when a pending address change is applied.
static u8 u8NewAddress;

//------------------------------------------------------------------------------
//         Inline Functions
//------------------------------------------------------------------------------

/// @brief Get amount of free space left in the memory pool.
static inline u32 ArenaSpace(void) {
  return (u32)(stArena.pBack - stArena.pFront);
}

/// @brief Allocate memory for a descriptor from the front of the memory pool.
/// Memory will not have any guaranteed alignment, if there's not enough free
/// mem then NULL is returned.
static inline u8 *DescriptorAlloc(u32 u32Size_) {
  u8 *pAlloc = NULL;
  if (ArenaSpace() >= u32Size_) {
    pAlloc = stArena.pFront;
    stArena.pFront += u32Size_;
  }

  return pAlloc;
}

/// @brief Allocate memory for usage in a structure from the back of the memory
/// pool.
/// The memory will be aligned to a 32-bit boundary.
/// If there's not enough available then NULL will be returned.
static inline void *StructAlloc(u32 u32Size_) {
  // Back alloc is for structures, so always round up to multiple of 4 to ensure
  // alignment.
  u32Size_ = (u32Size_ + 0x3) & ~0x3;
  void *pAlloc = NULL;

  if (ArenaSpace() >= u32Size_) {
    stArena.pBack -= u32Size_;
    pAlloc = stArena.pBack;
  }

  return pAlloc;
}

// Need accessors for dealing with word values in possibly unaligned descriptor
// memory.

/// @brief Write a 16 bit little-endian word into an arbitrary memory location.
static inline void WriteWord(u8 *pu8Dst_, u16 u16Val_) {
  pu8Dst_[0] = u16Val_;
  pu8Dst_[1] = u16Val_ >> 8;
}

/// @brief Read a 16 bit little endian word from an arbitrary memory location.
static inline u16 ReadWord(const u8 *pu8Src_) {
  return pu8Src_[0] | (pu8Src_[1] << 8);
}

/// @brief Add a value to a 16-bit little endian word at an arbitrary memory
/// location.
static inline void AddToWord(u8 *pu8Word_, u16 u16Val_) {
  WriteWord(pu8Word_, ReadWord(pu8Word_) + u16Val_);
}

/// @brief Encode a USB bcdVersion into descriptor memory.
static inline void WriteVersion(u8 *pu8Dst_, UsbVersionType stVersion_) {
  u8 u8EncodedMajor =
      (stVersion_.u8Major / 10) * 16 + (stVersion_.u8Major % 10);
  u8 u8EncodedMinor = stVersion_.u8Minor * 16 + stVersion_.u8SubMinor;

  pu8Dst_[0] = u8EncodedMinor;
  pu8Dst_[1] = u8EncodedMajor;
}

/// @brief Encode a class definition into descriptor memory.
static inline void WriteClass(u8 *pu8Dst_, UsbClassCodeType stClass_) {
  pu8Dst_[0] = stClass_.u8Class;
  pu8Dst_[1] = stClass_.u8SubClass;
  pu8Dst_[2] = stClass_.u8Protocol;
}

/// @brief Encode PID/VID into descriptor memory.
static inline void WriteUsbId(u8 *pu8Dst_, UsbIdType stId_) {
  WriteWord(pu8Dst_, stId_.u16Vendor);
  WriteWord(pu8Dst_ + 2, stId_.u16Product);
}

/// @brief Create the address used for an endpoint.
static inline u8 MakeEndptAddr(u8 u8Endpt_, bool bIsIn_) {
  u8 u8Addr = u8Endpt_;
  if (bIsIn_) {
    u8Addr |= 0x80;
  }

  return u8Addr;
}

/// @brief Parse the endpoint number from an endpoint address.
static inline u8 EndptFromAddr(u8 u8Addr_) { return u8Addr_ & 0x7f; }

/// @brief Check if an address indicates an IN endpoint.
static inline bool IsInAddr(u8 u8Addr_) { return !!(u8Addr_ & 0x80); }

/// @brief Create the endpoint attributes field corresponding to the provided
/// info.
static inline u8 MakeAttribs(const UsbEndptInfoType *pstInfo_) {
  return pstInfo_->u8TransferType | (pstInfo_->u8SyncType << 2) |
         (pstInfo_->u8EndptUsage << 4);
};

/// @brief Get the start of the memory window used to access an endpoint's FIFO
/// memory.
static inline void *GetFifoPtr(u8 u8Endpt_) {
  return (void *)(FIFO_MAP_BASE + FIFO_CHANNEL_SZ * u8Endpt_);
}

//------------------------------------------------------------------------------
//         Function Prototypes
//------------------------------------------------------------------------------

static void ResetAllCfg(void);
static void AbortRequest(void);
static void ResetEndpointCfg(u8 u8Endpt_);

static void ActivateController(void);
static bool HandleUsbReset(void);

static void UpdateReadyState(u8 u8Endpt_);

static bool CfgControlPipe(void);
static void ServiceControlPipe(void);
static void StartNewRequest(void);
static void HandleStandardRequest(void);
static void HandleGetDescriptor(void);
static void HandleSetAddress(void);
static void HandleSetConfig(void);
static void HandleGetConfig(void);
static void HandleSetIface(void);
static void HandleGetIface(void);
static void HandleGetStatus(void);
static void HandleSetFeature(void);
static void HandleClearFeature(void);
static void HandleNonStandardRequest(void);
static void DispatchEptRequest(u8 u8Endpt_);
static void DispatchIfaceRequest(u8 u8Iface_);
static void DispatchDevRequest(void);

static void SendDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                           const u8 *pu8Desc_, u16 u16Len_);
static void
GetDeviceDescriptor(const volatile UsbRequestStatusType *pstRequest_, void *);
static void
GetConfigDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                    void *pvDesc_);
static void
GetStringDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                    void *pvDesc_);

static u8 *MakeStringDesc(const char *pcStr_);

static void Unconfigure(void);
static bool Configure(u8 u8CfgNum_);
static bool ChangeIface(u8 u8IfaceIdx_, u8 u8AltIdx_);
static void SetupIface(u8 u8IfaceIdx_, u8 u8AltIdx_);
static void TeardownIface(u8 u8IfaceIdx_);

static u8 *FindCfgDesc(u8 u8CfgIdx_);
static ConfigDataType *FindCfgData(u8 u8CfgIdx_);
static IfaceListType *FindIfaceList(u8 u8ListIdx_);
static IfaceDataType *FindIfaceData(IfaceListType *pstIfaces, u8 u8AltIdx_);
static u8 *FindIfaceDesc(u8 u8IfaceIdx_, u8 u8AltIdx_);
static u8 *FindNextEptDesc(u8 *pu8CurrDesc_);

static void GetEndptInfo(const u8 *pu8EptDesc, UsbEndptInfoType *pstInfo);

static bool IsEndptInfoValid(u8 u8Endpt_, const UsbEndptInfoType *pstInfo_);

//------------------------------------------------------------------------------
//         Public Function Impls
//------------------------------------------------------------------------------

u8 UsbRegisterString(const char *pcStr_) {
  if (pcStr_ == NULL) {
    return 0;
  }

  for (u8 idx = 0; idx < U8_USB_MAX_STRINGS; idx++) {
    if (apcUsbStrings[idx] == NULL) {
      apcUsbStrings[idx] = pcStr_;
      return idx + 1;
    }

    if (0 == strcmp(apcUsbStrings[idx], pcStr_)) {
      return idx + 1;
    }
  }

  return 0;
}

bool UsbSetEnabled(bool bIsEnabled_) {
  if (eDriverState == DRIVER_STATE_CFG) {
    if (pstFirstCfg == NULL) {
      // Require at least one configuration.
      return FALSE;
    }

    // All configs must have at least one interface.
    for (ConfigDataType *pstCfg = pstFirstCfg; pstCfg != NULL;
         pstCfg = pstCfg->pstNextCfg) {
      if (pstCfg->pstIfaces == NULL) {
        return FALSE;
      }
    }

    pstCurrCfg = NULL;
    eDriverState = DRIVER_STATE_DISABLED;
  }

  switch (eDriverState) {
  case DRIVER_STATE_ENABLED:
    if (!bIsEnabled_) {
      ResetAllCfg();
      eDriverState = DRIVER_STATE_DISABLED;
    }
    return TRUE;

  case DRIVER_STATE_DISABLED:
    if (!bIsEnabled_) {
      eDriverState = DRIVER_STATE_DISABLED;
    } else {
      eDriverState = DRIVER_STATE_ENABLED;
      ActivateController();
    }
    return TRUE;

  default:
    break;
  }

  return FALSE;
}

void UsbInitialize(void) {
  if (eDriverState != DRIVER_STATE_NOT_INIT) {
    return;
  }

  if (USB_MEM_ARENA_BYTES % 4 != 0) {
    eDriverState = DRIVER_STATE_ERR;
    return;
  }

  stArena.pFront = malloc(USB_MEM_ARENA_BYTES);
  if (stArena.pFront == NULL) {
    eDriverState = DRIVER_STATE_ERR;
    return;
  }

  stArena.pBack = stArena.pFront + USB_MEM_ARENA_BYTES;
  ResetAllCfg();
  eDriverState = DRIVER_STATE_CFG;
}

void UsbRunActiveState(void) {
  if (eDriverState != DRIVER_STATE_ENABLED) {
    return;
  }

  // A reset event always has priority and is handled the same no matter what.
  if (bResetEvt) {
    bResetEvt = FALSE;

    if (HandleUsbReset()) {
      eDriverState = DRIVER_STATE_ENABLED;
    } else {
      eDriverState = DRIVER_STATE_ERR;
    }
  } else {
    ServiceControlPipe();

    for (u8 u8Endpt = 1; u8Endpt < USB_NUM_EPS; u8Endpt++) {
      UpdateReadyState(u8Endpt);
    }
  }
}

bool UsbSetDeviceInfo(const UsbDeviceInfoType *pstInfo_) {
  if (pstInfo_ == NULL) {
    return FALSE;
  }

  if (eDriverState != DRIVER_STATE_CFG || pu8DevDesc != NULL) {
    return FALSE;
  }

  pu8DevDesc = DescriptorAlloc(DEV_DESC_SZ);
  if (pu8DevDesc == NULL) {
    return FALSE;
  }

  pu8DevDesc[DESC_LENGTH_OFFSET] = DEV_DESC_SZ;
  pu8DevDesc[DESC_TYPE_OFFSET] = DESC_TYPE_DEV;
  WriteVersion(pu8DevDesc + DEV_DESC_USB_VER_OFFSET, UsbSpecVersion);
  WriteClass(pu8DevDesc + DEV_DESC_CLASS_OFFSET, pstInfo_->stClass);
  pu8DevDesc[DEV_DESC_EP0_PKT_SZ] = CTRL_PKT_SZ;
  WriteUsbId(pu8DevDesc + DEV_DESC_ID_OFFSET, pstInfo_->stId);
  WriteVersion(pu8DevDesc + DEV_DESC_DEV_VER_OFFSET, pstInfo_->stDevVersion);
  pu8DevDesc[DEV_DESC_MFGR_NAME_OFFSET] =
      UsbRegisterString(pstInfo_->pcManufacturerName);
  pu8DevDesc[DEV_DESC_PROD_NAME_OFFSET] =
      UsbRegisterString(pstInfo_->pcProductName);
  pu8DevDesc[DEV_DESC_SERIAL_NUM_OFFSET] =
      UsbRegisterString(pstInfo_->pcSerialNum);
  pu8DevDesc[DEV_DESC_NUM_CFGS_OFFSET] = 0;

  pstDevInfo = pstInfo_;
  return TRUE;
}

bool UsbAddConfig(const UsbConfigInfoType *pstInfo_) {
  if (pstInfo_ == NULL) {
    return FALSE;
  }

  if (eDriverState != DRIVER_STATE_CFG || pu8DevDesc == NULL) {
    return FALSE;
  }

  pstCurrCfg = StructAlloc(sizeof(*pstCurrCfg));
  pu8CurrCfgDesc = DescriptorAlloc(CFG_DESC_SZ);
  if (pu8CurrCfgDesc == NULL || pstCurrCfg == NULL) {
    return FALSE;
  }

  memset(pstCurrCfg, 0, sizeof(*pstCurrCfg));
  pstCurrCfg->pstInfo = pstInfo_;

  // Add to end of config list.
  ConfigDataType **ppCfg = &pstFirstCfg;
  while (*ppCfg != NULL) {
    ppCfg = &(*ppCfg)->pstNextCfg;
  }
  *ppCfg = pstCurrCfg;

  u8 u8CfgNum = ++pu8DevDesc[DEV_DESC_NUM_CFGS_OFFSET];

  pu8CurrCfgDesc[DESC_LENGTH_OFFSET] = CFG_DESC_SZ;
  pu8CurrCfgDesc[DESC_TYPE_OFFSET] = DESC_TYPE_CFG;
  WriteWord(pu8CurrCfgDesc + CFG_DESC_TOTAL_LEN_OFFSET, CFG_DESC_SZ);
  pu8CurrCfgDesc[CFG_DESC_NUM_IFACES_OFFSET] = 0;
  pu8CurrCfgDesc[CFG_DESC_CFG_VAL_OFFSET] = u8CfgNum;
  pu8CurrCfgDesc[CFG_DESC_CFG_NAME_OFFSET] =
      UsbRegisterString(pstInfo_->pcConfigName);
  pu8CurrCfgDesc[CFG_DESC_ATTRIB_OFFSET] = CFG_FIXED_ATTRIB;
  pu8CurrCfgDesc[CFG_DESC_MAX_POWER_OFFSET] = CFG_FIXED_PWR;

  pu8CurrIfaceDesc = NULL;
  return TRUE;
}

bool UsbSetEndpointCapacity(u8 u8Endpt_, u16 u16MaxPacketSize_,
                            u8 u8NumPackets_) {
  if (u8Endpt_ == USB_DEF_CTRL_EP || u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  // Packet sizes must be a power of 2 and >= 8 for this part.
  u16 u16RealSize = 8;
  while (u16RealSize < u16MaxPacketSize_) {
    u16RealSize <<= 1;
  }
  u16MaxPacketSize_ = u16RealSize;

  if (astMaxSizes[u8Endpt_ - 1].u16PacketSize < u16MaxPacketSize_ ||
      astMaxSizes[u8Endpt_].u8NumPackets < u8NumPackets_) {
    return FALSE;
  }

  if (eDriverState != DRIVER_STATE_CFG || pstCurrCfg == NULL) {
    return FALSE;
  }

  if (pstCurrCfg->astSizes[u8Endpt_ - 1].u8NumPackets != 0) {
    return FALSE;
  }

  u16 u16Avail = FIFO_SIZE - CTRL_PKT_SZ;

  for (u8 idx = 0; idx < USB_ALLOCABLE_EPS; idx++) {
    EndptSizeType stSize = pstCurrCfg->astSizes[idx];
    u16Avail -= stSize.u16PacketSize * stSize.u8NumPackets;
  }

  if (u16MaxPacketSize_ * u8NumPackets_ > u16Avail) {
    return FALSE;
  }

  pstCurrCfg->astSizes[u8Endpt_ - 1].u16PacketSize = u16MaxPacketSize_;
  pstCurrCfg->astSizes[u8Endpt_ - 1].u8NumPackets = u8NumPackets_;
  return TRUE;
}

bool UsbAddIface(const UsbIfaceInfoType *pstInfo_, bool bIsAlternate_) {
  if (pstInfo_ == NULL) {
    return FALSE;
  }

  if (eDriverState != DRIVER_STATE_CFG || pu8CurrCfgDesc == NULL) {
    return FALSE;
  }

  if (bIsAlternate_ && pu8CurrIfaceDesc == NULL) {
    return FALSE;
  }

  u8 u8IfaceIdx;
  u8 u8AltIdx;
  IfaceListType *pstIfaces = NULL;

  if (bIsAlternate_) {
    u8IfaceIdx = pu8CurrCfgDesc[CFG_DESC_NUM_IFACES_OFFSET];
    u8AltIdx = pu8CurrIfaceDesc[IFACE_DESC_ALT_OFFSET] + 1;
    pstIfaces = FindIfaceList(u8IfaceIdx);
  } else {
    u8IfaceIdx = pu8CurrCfgDesc[CFG_DESC_NUM_IFACES_OFFSET]++;
    u8AltIdx = 0;

    pstIfaces = StructAlloc(sizeof(*pstIfaces));

    IfaceListType **ppList = &pstCurrCfg->pstIfaces;
    memset(pstIfaces, 0, sizeof(*pstIfaces));
    while (*ppList != NULL) {
      ppList = &(*ppList)->pstNext;
    }
    *ppList = pstIfaces;
  }

  IfaceDataType *pstIface = StructAlloc(sizeof(*pstIface));
  if (pstIface == NULL) {
    return FALSE;
  }

  u8 *pu8Alloc = DescriptorAlloc(IFACE_DESC_SZ);
  if (pu8Alloc == NULL) {
    return FALSE;
  }

  memset(pstIface, 0, sizeof(*pstIface));
  pstIface->pstInfo = pstInfo_;
  IfaceDataType **ppIface = &pstIfaces->pstAlts;
  while (*ppIface != NULL) {
    ppIface = &(*ppIface)->pstNext;
  }
  *ppIface = pstIface;

  AddToWord(pu8CurrCfgDesc + CFG_DESC_TOTAL_LEN_OFFSET, IFACE_DESC_SZ);

  pu8CurrIfaceDesc = pu8Alloc;

  pu8CurrIfaceDesc[DESC_LENGTH_OFFSET] = IFACE_DESC_SZ;
  pu8CurrIfaceDesc[DESC_TYPE_OFFSET] = DESC_TYPE_IFACE;
  pu8CurrIfaceDesc[IFACE_DESC_IDX_OFFSET] = u8IfaceIdx;
  pu8CurrIfaceDesc[IFACE_DESC_ALT_OFFSET] = u8AltIdx;
  pu8CurrIfaceDesc[IFACE_DESC_NUM_EPS_OFFSET] = 0;
  WriteClass(pu8CurrIfaceDesc + IFACE_DESC_CLASS_OFFSET, pstInfo_->stClass);
  pu8CurrIfaceDesc[IFACE_DESC_NAME_OFFSET] =
      UsbRegisterString(pstInfo_->pcIfaceName);

  return TRUE;
}

bool UsbUseEndpt(u8 u8Endpt_, const UsbEndptInfoType *pstInfo_) {
  if (!IsEndptInfoValid(u8Endpt_, pstInfo_)) {
    return FALSE;
  }

  if (eDriverState != DRIVER_STATE_CFG || pu8CurrIfaceDesc == NULL) {
    return FALSE;
  }

  EndptSizeType pstSize = pstCurrCfg->astSizes[u8Endpt_ - 1];
  if (pstInfo_->u16PacketSize > pstSize.u16PacketSize) {
    return FALSE;
  }

  u8 *pu8Desc = DescriptorAlloc(ENDPT_DESC_SZ);
  if (pu8Desc == NULL) {
    return FALSE;
  }

  AddToWord(pu8CurrCfgDesc + CFG_DESC_TOTAL_LEN_OFFSET, ENDPT_DESC_SZ);
  pu8CurrIfaceDesc[IFACE_DESC_NUM_EPS_OFFSET] += 1;

  pu8Desc[DESC_LENGTH_OFFSET] = ENDPT_DESC_SZ;
  pu8Desc[DESC_TYPE_OFFSET] = DESC_TYPE_ENDPT;
  pu8Desc[ENDPT_DESC_ADDR_OFFSET] = MakeEndptAddr(u8Endpt_, pstInfo_->bIsIn);
  pu8Desc[ENDPT_DESC_ATTRIB_OFFSET] = MakeAttribs(pstInfo_);
  WriteWord(pu8Desc + ENDPT_DESC_PKT_SZ_OFFSET, pstInfo_->u16PacketSize);
  pu8Desc[ENDPT_DESC_INTERVAL_OFFSET] = pstInfo_->u8Interval;

  return TRUE;
}

bool UsbAddCustomDescriptor(u8 u8DescriptorType, const void *pvBody_,
                            u8 u8BodyLen_) {
  if (eDriverState != DRIVER_STATE_CFG) {
    return FALSE;
  }

  u8 u8TrueLen = u8BodyLen_ + 2;
  if (u8TrueLen < u8BodyLen_) {
    return FALSE;
  }

  u8 *pu8Desc = DescriptorAlloc(u8TrueLen);
  if (pu8Desc == NULL) {
    return FALSE;
  }

  pu8Desc[DESC_LENGTH_OFFSET] = u8TrueLen;
  pu8Desc[DESC_TYPE_OFFSET] = u8DescriptorType;
  memcpy(pu8Desc + DESC_HEADER_SZ, pvBody_, u8BodyLen_);
  return TRUE;
}

bool UsbSetStall(u8 u8Endpt_, bool bIsStalling_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    return FALSE; // Use the request API instead.
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

  if (!pstEpt->bIsReady || !pstEpt->bIsWrite) {
    return 0;
  }

  u16 u16ChunkSz = pstEpt->u16PktSize - pstEpt->u16Offset;
  if (u16ChunkSz > u16MaxLen_) {
    u16ChunkSz = u16MaxLen_;
  }

  memcpy(GetFifoPtr(u8Endpt_) + pstEpt->u16Offset, pvSrc_, u16ChunkSz);
  pstEpt->u16Offset += u16ChunkSz;

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    stCurrRequest.u16RequestOffset += u16ChunkSz;
  }

  return u16ChunkSz;
}

u16 UsbRead(u8 u8Endpt_, void *pvDst_, u16 u16MaxLen_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  EndptStateType *pstEpt = &astEndpts[u8Endpt_];

  if (!pstEpt->bIsReady || pstEpt->bIsWrite) {
    return 0;
  }

  u16 u16ChunkSz = pstEpt->u16PktSize - pstEpt->u16Offset;
  if (u16ChunkSz > u16MaxLen_) {
    u16ChunkSz = u16MaxLen_;
  }

  memcpy(pvDst_, GetFifoPtr(u8Endpt_) + pstEpt->u16Offset, u16ChunkSz);
  pstEpt->u16Offset += u16ChunkSz;

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    stCurrRequest.u16RequestOffset += u16ChunkSz;
  }

  return u16ChunkSz;
}

bool UsbNextPacket(u8 u8Endpt_) {
  if (u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Endpt_];
  EndptStateType *pstState = &astEndpts[u8Endpt_];

  if (!pstState->bIsReady) {
    return FALSE;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    if (stCurrRequest.bTerminated) {
      return FALSE;
    }

    if (pstState->u16Offset != pstState->u16PktSize) {
      if (pstState->bIsWrite) {
        // Short packet indicates early termination of the request.
        stCurrRequest.bTerminated = TRUE;
      } else {
        // Make sure the request offset reflects the skipped data.
        stCurrRequest.u16RequestOffset +=
            pstState->u16PktSize - pstState->u16Offset;
      }
    }

    if (stCurrRequest.u16RequestOffset == stCurrRequest.stHeader.u16Length) {
      // All data consumed, this request is finished.
      stCurrRequest.bTerminated = TRUE;
    }
  }

  pstState->bIsReady = FALSE;
  pstState->u16Offset = 0;
  pstState->u16PktSize = 0;

  if (pstState->bIsWrite) {
    pstRegs->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_TXRDY;
  } else {
    pstRegs->UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_RXRDY_TXKL;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    // Need to send a 0-length packet at the end of OUT transfers.
    if (!pstState->bIsWrite && stCurrRequest.bTerminated) {
      pstRegs->UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_TXRDY;
    }

  } else {
    // Transfers still ongoing, might be ready right away if there's multiple
    // buffers.
    UpdateReadyState(u8Endpt_);
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

const UsbRequestHeaderType *UsbGetCurrentRequest(void) {
  if (stCurrRequest.bTerminated) {
    return NULL;
  } else {
    return &stCurrRequest.stHeader;
  }
}

bool UsbAcceptRequest(UsbRequestHandlerType fnRequestHandler_, void *pvParam_) {
  if (fnRequestHandler_ == NULL) {
    return FALSE;
  }

  if (stCurrRequest.bTerminated || pfnRequestHandler != NULL) {
    return FALSE;
  }

  pfnRequestHandler = fnRequestHandler_;
  pvRequestParam = pvParam_;

  return TRUE;
}

bool UsbFailRequest(void) {
  if (stCurrRequest.bTerminated) {
    return FALSE;
  }

  stCurrRequest.bTerminated = TRUE;
  UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP].UDPHS_EPTSETSTA = UDPHS_EPTSETSTA_FRCESTALL;
  return TRUE;
}

void UDPD_IrqHandler(void) {
  if (UDPHS->UDPHS_INTSTA & UDPHS_INTSTA_INT_SOF) {
    UDPHS->UDPHS_CLRINT = UDPHS_CLRINT_INT_SOF;
    u32 u32Now =
        (SysTick->VAL & SysTick_VAL_CURRENT_Msk) >> SysTick_VAL_CURRENT_Pos;

    // Make sys tick lag the actual SOF, should ensure that packets are
    // definitely ready by the time user code runs.
    static const s32 s32Diff = U32_SYSTICK_COUNT / 100;

    SysTickSyncEvt(u32Now, s32Diff);
  }

  if (UDPHS->UDPHS_INTSTA & UDPHS_INTSTA_ENDRESET) {
    bResetEvt = TRUE;
    UDPHS->UDPHS_CLRINT = UDPHS_CLRINT_ENDRESET;
  }
}

//------------------------------------------------------------------------------
//         Private Function Implementations
//------------------------------------------------------------------------------

/// @brief Reset the peripheral and associated state back to a default detached
/// state.
static void ResetAllCfg(void) {
  NVIC_DisableIRQ(UDPHS_IRQn);

  AbortRequest();
  Unconfigure();

  ResetEndpointCfg(USB_DEF_CTRL_EP);

  // Switch to a (simulated) detached state. This matches the reset value of the
  // register
  UDPHS->UDPHS_CTRL = UDPHS_CTRL_DETACH;

  // No test modes, disable high-speed operation.
  UDPHS->UDPHS_TST = UDPHS_TST_SPEED_CFG_FULL_SPEED;

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

/// @brief Abort any ongoing control pipe request.
///
/// If there is an active handler registered it will receive one final call
/// with bTerminated set to TRUE so that it can clean up any resources it was
/// using to satisfy the request.
static void AbortRequest(void) {
  if (stCurrRequest.bTerminated) {
    return;
  }

  memset(&astEndpts[USB_DEF_CTRL_EP], 0, sizeof(EndptStateType));
  stCurrRequest.bTerminated = TRUE;
  if (pfnRequestHandler != NULL) {
    pfnRequestHandler(&stCurrRequest, pvRequestParam);
  }
}

/// @brief Rest an endpoint back to an unconfigured/unused state.
static void ResetEndpointCfg(u8 u8Endpt_) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];
  pstEpt->UDPHS_EPTCFG = 0;
  pstEpt->UDPHS_EPTCTLDIS = ~0;
  pstEpt->UDPHS_EPTCLRSTA = ~0;

  EndptStateType *pstState = &astEndpts[u8Endpt_];
  memset(pstState, 0, sizeof(*pstState));
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
  AbortRequest();
  Unconfigure();

  ResetEndpointCfg(USB_DEF_CTRL_EP);

  // The only event that we need to handle in the IRQ is the SOF interrupt, as
  // it needs to capture accurate timing in order to phase-lock the systick
  // timer.
  UDPHS->UDPHS_IEN = UDPHS_IEN_INT_SOF;

  if (!CfgControlPipe()) {
    return FALSE;
  }

  pstDevInfo->pfnEventHandler(USB_DEV_EVT_RESET);
  return TRUE;
}

/// @brief Update the ready status of an ednpoint based on the state reflected
/// in the peripheral registers.
static void UpdateReadyState(u8 u8Endpt_) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[u8Endpt_];
  EndptStateType *pstState = &astEndpts[u8Endpt_];

  if (pstState->bIsReady) {
    // If the endpoint was already ready there's nothing to do.
    return;
  }

  // Careful, TXRDY being set actually means all tx buffers are used/busy.
  if (pstState->bIsWrite && !(pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_TXRDY)) {
    pstState->bIsReady = TRUE;
    pstState->u16PktSize = pstState->u16MaxPktSize;

    if (u8Endpt_ == USB_DEF_CTRL_EP) {
      u16 u16Remain =
          stCurrRequest.stHeader.u16Length - stCurrRequest.u16RequestOffset;
      if (u16Remain < pstState->u16PktSize) {
        pstState->u16PktSize = u16Remain;
      }
    }
  }

  if (!pstState->bIsWrite && (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_RXRDY_TXKL)) {
    if (!pstState->bIsReady) {
      pstState->bIsReady = TRUE;
      pstState->u16PktSize =
          (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_BYTE_COUNT_Msk) >>
          UDPHS_EPTSTA_BYTE_COUNT_Pos;
    }
  }
}

/// @brief Apply the fixed configuration used for the control pipe.
static bool CfgControlPipe(void) {
  UdphsEpt *pstEpt = &UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP];

  pstEpt->UDPHS_EPTCFG = UDPHS_EPTCFG_EPT_SIZE_64 |
                         UDPHS_EPTCFG_EPT_TYPE_CTRL8 | UDPHS_EPTCFG_BK_NUMBER_1;

  if (!(pstEpt->UDPHS_EPTCFG & UDPHS_EPTCFG_EPT_MAPD)) {
    return FALSE;
  }

  pstEpt->UDPHS_EPTCTLENB = UDPHS_EPTCTLENB_EPT_ENABL;
  return TRUE;
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
    UDPHS->UDPHS_CTRL |=
        UDPHS_CTRL_DEV_ADDR(u8NewAddress) | UDPHS_CTRL_FADDR_EN;
    bAddressPending = FALSE;
  }

  if (pstEpt->UDPHS_EPTSTA & UDPHS_EPTSTA_RX_SETUP) {
    StartNewRequest();
  }

  if (!stCurrRequest.bTerminated) {
    UpdateReadyState(USB_DEF_CTRL_EP);

    if (astEndpts[USB_DEF_CTRL_EP].bIsReady) {
      pfnRequestHandler(&stCurrRequest, pvRequestParam);
    }
  }
}

/// @brief Begin processing a new control pipe request.
static void StartNewRequest(void) {
  AbortRequest();

  // Take advantage of the fact that on the SAM3U the structure will match the
  // USB defined layout.
  memcpy(&stCurrRequest.stHeader, GetFifoPtr(USB_DEF_CTRL_EP),
         sizeof(UsbRequestHeaderType));

  // Clear out the status packet imemdiately so that the handlers can do things
  // synchronously if more convenient.
  UDPHS->UDPHS_EPT[USB_DEF_CTRL_EP].UDPHS_EPTCLRSTA = UDPHS_EPTCLRSTA_RX_SETUP;

  EndptStateType *pstState = &astEndpts[USB_DEF_CTRL_EP];
  pstState->u16MaxPktSize = CTRL_PKT_SZ;
  pstState->u16PktSize = 0;
  pstState->bIsReady = FALSE;
  pstState->u16Offset = 0;

  if ((stCurrRequest.stHeader.u8RequestType & USB_RQST_DIR_MASK) ==
      USB_RQST_DIR_TO_HOST) {
    pstState->bIsWrite = TRUE;
  } else {
    pstState->bIsWrite = FALSE;
  }

  pfnRequestHandler = NULL;
  stCurrRequest.bTerminated = FALSE;
  stCurrRequest.u16RequestOffset = 0;

  if (stCurrRequest.stHeader.u16Length > 0) {
    UpdateReadyState(USB_DEF_CTRL_EP);
  } else {
    pstState->bIsReady = TRUE;
  }

  if ((stCurrRequest.stHeader.u8RequestType & USB_RQST_TYPE_MASK) ==
      USB_RQST_TYPE_STANDARD) {
    HandleStandardRequest();
  } else {
    HandleNonStandardRequest();
  }

  if (pfnRequestHandler == NULL && !stCurrRequest.bTerminated) {
    // No one will continue the request, so just fail it now.
    UsbFailRequest();
  }
}

/// @brief Handler for all standard requests.
static void HandleStandardRequest(void) {

  switch (stCurrRequest.stHeader.u8RequestId) {
  case RQST_GET_DESCRIPTOR:
    HandleGetDescriptor();
    break;

  case RQST_SET_ADDRESS:
    HandleSetAddress();
    break;

  case RQST_SET_CONFIGURATION:
    HandleSetConfig();
    break;

  case RQST_GET_CONFIGURATION:
    HandleGetConfig();
    break;

  case RQST_SET_INTERFACE:
    HandleSetIface();
    break;

  case RQST_GET_INTERFACE:
    HandleGetIface();
    break;

  case RQST_GET_STATUS:
    HandleGetStatus();
    break;

  case RQST_SET_FEATURE:
    HandleSetFeature();
    break;

  case RQST_CLEAR_FEATURE:
    HandleClearFeature();
    break;

  default:
    UsbFailRequest();
    break;
  }
}

static void HandleGetDescriptor(void) {
  u8 u8DescType = stCurrRequest.stHeader.u16Value >> 8;
  u8 u8DescIdx = stCurrRequest.stHeader.u16Value;

  switch (u8DescType) {
  case DESC_TYPE_DEV:
    if (u8DescIdx == 0) {
      UsbAcceptRequest(GetDeviceDescriptor, NULL);
    } else {
      UsbFailRequest();
    }
    break;

  case DESC_TYPE_CFG: {

    const u8 *pu8Cfg = FindCfgDesc(u8DescIdx);

    if (pu8Cfg != NULL) {
      UsbAcceptRequest(GetConfigDescriptor, (void *)pu8Cfg);
    } else {
      UsbFailRequest();
    }
  } break;

  case DESC_TYPE_STRING: {
    if (u8DescIdx == 0) {
      // Only support a fixed single language. The descriptor is guaranteed to
      // fit in a single control pipe packet.

      u8 au8Desc[] = {
          4, // Length,
          DESC_TYPE_STRING,
          US_ENGLISH_LANGID & 0xFF,
          (US_ENGLISH_LANGID >> 8) & 0xFF,
      };

      UsbWrite(USB_DEF_CTRL_EP, au8Desc, sizeof(au8Desc));
      UsbNextPacket(USB_DEF_CTRL_EP);

    } else if (u8DescIdx <= U8_USB_MAX_STRINGS) {
      const char *pcStr = apcUsbStrings[u8DescIdx - 1];
      void *pvStr = MakeStringDesc(pcStr);

      if (pvStr != NULL) {
        UsbAcceptRequest(GetStringDescriptor, pvStr);
      } else {
        UsbFailRequest();
      }
    } else {
      UsbFailRequest();
    }
  } break;

  default:
    UsbFailRequest();
    break;
  }
}

static void HandleSetAddress(void) {
  // Annoyingly the address can't actually be changed until the transaction is
  // complete.
  u8NewAddress = (u8)stCurrRequest.stHeader.u16Value;
  bAddressPending = TRUE;
  UsbNextPacket(USB_DEF_CTRL_EP);
}

static void HandleSetConfig(void) {
  if (stCurrRequest.stHeader.u16Index != 0) {
    UsbFailRequest();
    return;
  }

  u8 u8CfgIdx = stCurrRequest.stHeader.u16Value & 0xFF;

  if (u8CfgIdx == 0) {
    Unconfigure();
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else if (Configure(u8CfgIdx)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}

static void HandleGetConfig(void) {
  u8 u8Cfg = 0;
  if (pu8CurrCfgDesc) {
    u8Cfg = pu8CurrCfgDesc[CFG_DESC_CFG_VAL_OFFSET];
  }

  UsbWrite(USB_DEF_CTRL_EP, &u8Cfg, sizeof(u8Cfg));
  UsbNextPacket(USB_DEF_CTRL_EP);
}

static void HandleSetIface(void) {
  if (stCurrRequest.stHeader.u16Index < 0x100 &&
      stCurrRequest.stHeader.u16Value < 0x100) {
    if (ChangeIface(stCurrRequest.stHeader.u16Index,
                    stCurrRequest.stHeader.u16Value)) {
      UsbNextPacket(USB_DEF_CTRL_EP);
    } else {
      UsbFailRequest();
    }
  } else {
    UsbFailRequest();
  }
}

static void HandleGetIface(void) {
  if (stCurrRequest.stHeader.u16Index < 0x100) {
    IfaceListType *pstIfaces = FindIfaceList(stCurrRequest.stHeader.u16Index);

    if (pstIfaces != NULL) {
      UsbWrite(USB_DEF_CTRL_EP, &pstIfaces->u8ActiveIface,
               sizeof(pstIfaces->u8ActiveIface));
      UsbNextPacket(USB_DEF_CTRL_EP);
    } else {
      UsbFailRequest();
    }
  } else {
    UsbFailRequest();
  }
}

static void HandleGetStatus(void) {
  bool bRequestGood = FALSE;
  u16 u16Status = 0;

  if (stCurrRequest.stHeader.u16Length >= sizeof(u16Status)) {
    switch (stCurrRequest.stHeader.u8RequestType & USB_RQST_TGT_MASK) {
    case USB_RQST_TGT_DEV:
      // None of the optional features supported right now.
      bRequestGood = TRUE;
      break;

    case USB_RQST_TGT_IFACE:
      // There are no status bits for interfaces defined.
      bRequestGood = TRUE;
      break;

    case USB_RQST_TGT_ENDPT:
      if (stCurrRequest.stHeader.u16Index < USB_NUM_EPS) {
        if (UsbIsStalling(stCurrRequest.stHeader.u16Index)) {
          u16Status |= 1u << 0;
        }
        bRequestGood = TRUE;
      }
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

static void HandleSetFeature(void) {
  // Remote wakeup not supported so is ignored.
  // Test mode not implemented either (only required for high-speed devices)
  // That leaves HALT as the only other feature.

  if (stCurrRequest.stHeader.u16Value != FEAT_EPT_HALT) {
    UsbFailRequest();
  }

  if ((stCurrRequest.stHeader.u8RequestType & USB_RQST_TGT_MASK) !=
      USB_RQST_TGT_ENDPT) {
    UsbFailRequest();
  }

  if (UsbSetStall(stCurrRequest.stHeader.u16Index, TRUE)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}

static void HandleClearFeature(void) {
  // Remote wakeup not supported so is ignored.
  // Test mode not implemented either (only required for high-speed devices)
  // That leaves HALT as the only other feature.

  if (stCurrRequest.stHeader.u16Value != FEAT_EPT_HALT) {
    UsbFailRequest();
  }

  if ((stCurrRequest.stHeader.u8RequestType & USB_RQST_TGT_MASK) !=
      USB_RQST_TGT_ENDPT) {
    UsbFailRequest();
  }

  if (UsbSetStall(stCurrRequest.stHeader.u16Index, FALSE)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  } else {
    UsbFailRequest();
  }
}

/// @brief Dispatch events for non standard requests to the proper handlers.
///
/// This set of handlers is setup to start at the most specific handler for a
/// request, but then fall back to the more generic handler if no one completes
/// or accepts the request.
static void HandleNonStandardRequest(void) {
  switch (stCurrRequest.stHeader.u8RequestType & USB_RQST_TGT_MASK) {
  case USB_RQST_TGT_IFACE:
    DispatchIfaceRequest(stCurrRequest.stHeader.u16Index);
    break;

  case USB_RQST_TGT_ENDPT:
    DispatchEptRequest(stCurrRequest.stHeader.u16Index);
    break;

  default:
    DispatchDevRequest();
    break;
  }
}

static void DispatchEptRequest(u8 u8Endpt_) {
  if (u8Endpt_ > USB_NUM_EPS) {
    UsbFailRequest();
    return;
  }

  if (!(UDPHS->UDPHS_EPT[u8Endpt_].UDPHS_EPTCTL & UDPHS_EPTCTL_EPT_ENABL)) {
    UsbFailRequest();
    return;
  }

  if (u8Endpt_ == USB_DEF_CTRL_EP) {
    DispatchDevRequest();
  } else {
    u8 u8Iface = astEndpts[u8Endpt_].u8Iface;
    DispatchIfaceRequest(u8Iface);
  }
}

static void DispatchIfaceRequest(u8 u8Iface_) {
  IfaceListType *pstIfaces = FindIfaceList(u8Iface_);
  IfaceDataType *pstIface = FindIfaceData(pstIfaces, pstIfaces->u8ActiveIface);

  if (pstIface == NULL) {
    UsbFailRequest();
    return;
  }

  if (pstIface->pstInfo->pfnEventHandler) {
    pstIface->pstInfo->pfnEventHandler(USB_IFACE_EVT_CMD_REQUESTED);
  }

  if (!stCurrRequest.bTerminated && pfnRequestHandler == NULL) {
    DispatchDevRequest();
  }
}

static void DispatchDevRequest(void) {
  if (pstCurrCfg && pstCurrCfg->pstInfo->pfnEventHandler) {
    pstCurrCfg->pstInfo->pfnEventHandler(USB_CFG_EVT_CMD_REQUESTED);
  }

  if (!stCurrRequest.bTerminated && pfnRequestHandler == NULL) {
    if (pstDevInfo->pfnEventHandler) {
      pstDevInfo->pfnEventHandler(USB_DEV_EVT_CMD_REQUESTED);
    }
  }
}

/// @brief Generic code for responding to get descriptor requests.
///
/// Given the complete descriptor memory this will write out the appropriate
/// portion into the active packet for the control transfer.
static void SendDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                           const u8 *pu8Desc_, u16 u16Len_) {

  u16 u16XferSize = u16Len_;
  if (u16XferSize > pstRequest_->stHeader.u16Length) {
    u16XferSize = pstRequest_->stHeader.u16Length;
  }

  pu8Desc_ += pstRequest_->u16RequestOffset;
  u16XferSize -= pstRequest_->u16RequestOffset;

  UsbWrite(USB_DEF_CTRL_EP, pu8Desc_, u16XferSize);
  UsbNextPacket(USB_DEF_CTRL_EP);
}

static void
GetDeviceDescriptor(const volatile UsbRequestStatusType *pstRequest_, void *) {
  SendDescriptor(pstRequest_, pu8DevDesc, DEV_DESC_SZ);
}

static void
GetConfigDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                    void *pvDesc_) {
  u8 *pu8Desc = pvDesc_;
  u16 u16Len = ReadWord(pu8Desc + CFG_DESC_TOTAL_LEN_OFFSET);

  SendDescriptor(pstRequest_, pu8Desc, u16Len);
}

static void
GetStringDescriptor(const volatile UsbRequestStatusType *pstRequest_,
                    void *pvDesc_) {
  if (pstRequest_->bTerminated) {
    free(pvDesc_);
    return;
  }

  u8 *pu8Desc = pvDesc_;
  u16 u16Len = pu8Desc[DESC_LENGTH_OFFSET];
  SendDescriptor(pstRequest_, pu8Desc, u16Len);

  // Handle case where SendDescriptor just sent the final packet.
  if (pstRequest_->bTerminated) {
    free(pvDesc_);
  }
}

/// @brief Generate the full descriptor for a string.
///
/// Uses dynamic memory, the caller must ensure free is called on the pointer in
/// order to avoid memory leaks.
///
/// This function translates strings from the ASCII character set into the
/// UTF16LE format specified by the USB standard.
static u8 *MakeStringDesc(const char *pcStr_) {
  if (pcStr_ == NULL) {
    return NULL;
  }

  // May want to support full unicode range some day, but for now assume
  // that strings are all in the ascii range. This makes encoding/length
  // handling much simpler.

  // String descriptors are encoded as UTF16LE, so each char is 2 bytes.
  static const u8 u8MaxChars = (255 - DESC_HEADER_SZ) / 2;
  u8 u8Chars = 0;

  for (const char *pcChar = pcStr_; *pcChar != '\0'; pcChar++) {
    if (*pcChar & 0x80) {
      // Reject non-ascii strings.
      return NULL;
    }

    u8Chars += 1;
    if (u8Chars == u8MaxChars) {
      break;
    }
  }

  u8 u8DescLen = u8Chars * 2 + DESC_HEADER_SZ;

  u8 *pu8Desc = malloc(u8DescLen);
  memset(pu8Desc, 0, u8DescLen);

  pu8Desc[DESC_LENGTH_OFFSET] = u8DescLen;
  pu8Desc[DESC_TYPE_OFFSET] = DESC_TYPE_STRING;

  for (u8 idx = 0; idx < u8Chars; idx++) {
    pu8Desc[DESC_HEADER_SZ + idx * 2] = pcStr_[idx];
  }

  return pu8Desc;
}

/// @brief Deactivate the current configuration, if any.
///
/// This will send out appropriate events and ensure all endpoint hardware is
/// put into an idle state.
static void Unconfigure(void) {
  if (pstCurrCfg == NULL) {
    return;
  }

  for (u8 u8Iface = 0; u8Iface < pu8CurrCfgDesc[CFG_DESC_NUM_IFACES_OFFSET];
       u8Iface++) {
    TeardownIface(u8Iface);
  }

  if (pstCurrCfg->pstInfo->pfnEventHandler) {
    pstCurrCfg->pstInfo->pfnEventHandler(USB_CFG_EVT_TEARDOWN);
  }

  pu8CurrCfgDesc = NULL;
  pstCurrCfg = NULL;

  for (u8 u8Ept = 1; u8Ept < USB_NUM_EPS; u8Ept++) {
    ResetEndpointCfg(u8Ept);
  }
}

/// @brief Make a specific configuration the active configuration.
///
/// This will send out all appropriate events and configure the hardware
/// according to the information that was provided during device setup.
static bool Configure(u8 u8CfgNum_) {
  ConfigDataType *pstNewCfg = FindCfgData(u8CfgNum_ - 1);
  if (pstNewCfg == NULL) {
    return FALSE;
  }

  Unconfigure();

  pstCurrCfg = pstNewCfg;
  pu8CurrCfgDesc = FindCfgDesc(u8CfgNum_ - 1);

  for (u8 u8Idx = 0; u8Idx < USB_ALLOCABLE_EPS; u8Idx++) {
    u8 u8Endpt = u8Idx + 1;
    UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Endpt];

    // Need to convert size to a shift for the register.
    EndptSizeType stSize = pstCurrCfg->astSizes[u8Idx];
    u8 u8Shift = 0;
    while ((8 << u8Shift) < stSize.u16PacketSize) {
      u8Shift += 1;
    }

    pstRegs->UDPHS_EPTCFG = UDPHS_EPTCFG_EPT_SIZE(u8Shift) |
                            UDPHS_EPTCFG_BK_NUMBER(stSize.u8NumPackets);
  }

  if (pstCurrCfg->pstInfo->pfnEventHandler) {
    pstCurrCfg->pstInfo->pfnEventHandler(USB_CFG_EVT_SETUP);
  }

  for (u8 u8Iface = 0; u8Iface < pu8CurrCfgDesc[CFG_DESC_NUM_IFACES_OFFSET];
       u8Iface++) {
    SetupIface(u8Iface, 0);
  }

  return TRUE;
}

/// Change the selected alternative for an interface.
static bool ChangeIface(u8 u8IfaceIdx_, u8 u8AltIdx_) {
  IfaceListType *pstIfaces = FindIfaceList(u8IfaceIdx_);

  if (pstIfaces == NULL || FindIfaceData(pstIfaces, u8AltIdx_) == NULL) {
    return FALSE;
  }

  if (u8AltIdx_ != pstIfaces->u8ActiveIface) {
    TeardownIface(u8IfaceIdx_);
    SetupIface(u8IfaceIdx_, u8AltIdx_);
  }

  return TRUE;
}

/// @brief Setup an interface (alternative).
///
/// Sends out the interface setup event and configures any endpoints used by
/// the interface.
static void SetupIface(u8 u8IfaceIdx_, u8 u8AltIdx_) {
  IfaceListType *pstIfaces = FindIfaceList(u8IfaceIdx_);
  IfaceDataType *pstIface = FindIfaceData(pstIfaces, u8AltIdx_);

  if (pstIfaces == NULL || pstIface == NULL) {
    return;
  }

  pstIfaces->u8ActiveIface = u8AltIdx_;

  u8 *pu8Desc = FindIfaceDesc(u8IfaceIdx_, u8AltIdx_);
  u8 u8NumEps = pu8Desc[IFACE_DESC_NUM_EPS_OFFSET];

  for (u8 idx = 0; idx < u8NumEps; idx++) {
    pu8Desc = FindNextEptDesc(pu8Desc);

    u8 u8Ept = EndptFromAddr(pu8Desc[ENDPT_DESC_ADDR_OFFSET]);
    UsbEndptInfoType stInfo;
    GetEndptInfo(pu8Desc, &stInfo);

    UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Ept];
    EndptStateType *pstState = &astEndpts[u8Ept];

    volatile u32 *pCfg = &pstRegs->UDPHS_EPTCFG;
    if (stInfo.bIsIn) {
      *pCfg |= UDPHS_EPTCFG_EPT_DIR;
    } else {
      *pCfg &= ~UDPHS_EPTCFG_EPT_DIR;
    }

    *pCfg &= ~UDPHS_EPTCFG_EPT_TYPE_Msk;
    *pCfg |= stInfo.u8TransferType << UDPHS_EPTCFG_EPT_TYPE_Pos;

    pstState->u16MaxPktSize = stInfo.u16PacketSize;
    pstState->bIsWrite = stInfo.bIsIn;
    pstState->u8Iface = u8IfaceIdx_;

    pstRegs->UDPHS_EPTCTLENB = UDPHS_EPTCTLENB_EPT_ENABL;
    UpdateReadyState(u8Ept);
  }

  if (pstIface->pstInfo->pfnEventHandler) {
    pstIface->pstInfo->pfnEventHandler(USB_IFACE_EVT_SETUP);
  }
}

/// @brief Deactivate the currently selected alternative for an interface.
///
/// This will deliver the teardown event and disable any endpoints that were
/// used by the interface.
static void TeardownIface(u8 u8IfaceIdx_) {
  IfaceListType *pstIfaces = FindIfaceList(u8IfaceIdx_);

  if (pstIfaces == NULL) {
    return;
  }

  IfaceDataType *pstIface = FindIfaceData(pstIfaces, pstIfaces->u8ActiveIface);

  if (pstIface->pstInfo->pfnEventHandler) {
    pstIface->pstInfo->pfnEventHandler(USB_IFACE_EVT_TEARDOWN);
  }

  u8 *pu8Desc = FindIfaceDesc(u8IfaceIdx_, pstIfaces->u8ActiveIface);
  u8 u8NumEps = pu8Desc[IFACE_DESC_NUM_EPS_OFFSET];

  for (u8 idx = 0; idx < u8NumEps; idx++) {
    pu8Desc = FindNextEptDesc(pu8Desc);

    u8 u8Ept = EndptFromAddr(pu8Desc[ENDPT_DESC_ADDR_OFFSET]);

    UdphsEpt *pstRegs = &UDPHS->UDPHS_EPT[u8Ept];
    EndptStateType *pstState = &astEndpts[u8Ept];

    pstRegs->UDPHS_EPTCTLDIS = UDPHS_EPTCTLDIS_EPT_DISABL;
    UDPHS->UDPHS_EPTRST = 1 << u8Ept;

    memset(pstState, 0, sizeof(*pstState));
  }
}

/// @brief Lookup the descriptor for a specific configuration.
static u8 *FindCfgDesc(u8 u8CfgIdx_) {
  u8 *pu8Desc = pu8DevDesc + DEV_DESC_SZ;
  u8 u8CfgCount = pu8DevDesc[DEV_DESC_NUM_CFGS_OFFSET];

  if (u8CfgIdx_ >= u8CfgCount) {
    return NULL;
  }

  for (u8 i = 0; i < u8CfgIdx_; i++) {
    pu8Desc += ReadWord(pu8Desc + CFG_DESC_TOTAL_LEN_OFFSET);
  }

  return pu8Desc;
}

/// @brief Lookup the runtime state for a specific configuration.
static ConfigDataType *FindCfgData(u8 u8CfgIdx_) {
  ConfigDataType *pstCfg = pstFirstCfg;

  while (u8CfgIdx_ > 0 && pstCfg != NULL) {
    pstCfg = pstCfg->pstNextCfg;
    u8CfgIdx_--;
  }

  return pstCfg;
}

/// @brief Look up the list of alternatives for a specific interface.
static IfaceListType *FindIfaceList(u8 u8ListIdx_) {
  if (pstCurrCfg == NULL) {
    return NULL;
  }

  IfaceListType *pstList = pstCurrCfg->pstIfaces;
  while (u8ListIdx_ > 0 && pstList != NULL) {
    u8ListIdx_ -= 1;
    pstList = pstList->pstNext;
  }

  return pstList;
}

/// @brief Get the runtime data for a specific interface alternative.
static IfaceDataType *FindIfaceData(IfaceListType *pstIfaces, u8 u8AltIdx_) {
  if (pstIfaces == NULL) {
    return NULL;
  }

  IfaceDataType *pstIface = pstIfaces->pstAlts;
  while (u8AltIdx_ > 0 && pstIface != NULL) {
    u8AltIdx_ -= 1;
    pstIface = pstIface->pstNext;
  }

  return pstIface;
}

/// @brief Lookup the descriptor for a specific interface alternative.
static u8 *FindIfaceDesc(u8 u8IfaceIdx_, u8 u8AltIdx_) {
  if (pu8CurrCfgDesc == NULL) {
    return NULL;
  }

  u8 *pu8End =
      pu8CurrCfgDesc + ReadWord(pu8CurrCfgDesc + CFG_DESC_TOTAL_LEN_OFFSET);
  u8 *pu8Desc = pu8CurrCfgDesc;

  while (pu8Desc < pu8End) {
    pu8Desc += pu8Desc[DESC_LENGTH_OFFSET];

    if (pu8Desc[DESC_TYPE_OFFSET] != DESC_TYPE_IFACE) {
      continue;
    }

    if (pu8Desc[IFACE_DESC_IDX_OFFSET] == u8IfaceIdx_ &&
        pu8Desc[IFACE_DESC_ALT_OFFSET] == u8AltIdx_) {
      return pu8Desc;
    }
  }

  return NULL;
}

/// @brief Find the next endpoint descriptor in memory.
///
/// @require The provided pointer points to the beginning of a descriptor.
/// @require There is definitely an endpoint descriptor to find.
///
/// This cannot detect when it's past the end of overall descriptor memory,
/// so if there is no endpoint descriptor to find undefined behavior will occur.
static u8 *FindNextEptDesc(u8 *pu8CurrDesc_) {
  while (TRUE) {
    pu8CurrDesc_ += pu8CurrDesc_[DESC_LENGTH_OFFSET];

    if (pu8CurrDesc_[DESC_TYPE_OFFSET] == DESC_TYPE_ENDPT) {
      return pu8CurrDesc_;
    }
  }
}

/// @brief Recreate the info struct for an endpoint from its descriptor.
static void GetEndptInfo(const u8 *pu8EptDesc, UsbEndptInfoType *pstInfo) {
  memset(pstInfo, 0, sizeof(*pstInfo));

  pstInfo->bIsIn = IsInAddr(pu8EptDesc[ENDPT_DESC_ADDR_OFFSET]);

  u8 u8Attribs = pu8EptDesc[ENDPT_DESC_ATTRIB_OFFSET];

  pstInfo->u8TransferType = u8Attribs & 0x03;
  pstInfo->u8SyncType = (u8Attribs & 0x0C) >> 2;
  pstInfo->u8EndptUsage = (u8Attribs & 0x30) >> 4;
  pstInfo->u16PacketSize = ReadWord(pu8EptDesc + ENDPT_DESC_PKT_SZ_OFFSET);
  pstInfo->u8Interval = pu8EptDesc[ENDPT_DESC_INTERVAL_OFFSET];
}

/// @brief Validate that the provided endpoint info describes a usable
/// configuration.
static bool IsEndptInfoValid(u8 u8Endpt_, const UsbEndptInfoType *pstInfo_) {
  if (u8Endpt_ == USB_DEF_CTRL_EP || u8Endpt_ >= USB_NUM_EPS) {
    return FALSE;
  }

  if (pstInfo_ == NULL) {
    return FALSE;
  }

  if (pstInfo_->u8TransferType == USB_XFER_TYPE_CTRL) {
    // Non-default control endpoints would be extra logic to support, and are
    // not really needed. libusb doesn't even support them in its API.
    return FALSE;
  }

  switch (pstInfo_->u8TransferType) {
  case USB_XFER_TYPE_BULK:
  case USB_XFER_TYPE_INT:
    if (pstInfo_->u8SyncType != USB_SYNC_NONE ||
        pstInfo_->u8EndptUsage != USB_ENDPT_USAGE_DATA) {
      return FALSE;
    }
    break;

  case USB_XFER_TYPE_ISO:
    switch (pstInfo_->u8SyncType) {
    case USB_SYNC_ASYNC:
      // Async to host doesn't use any feedback.
      if (pstInfo_->bIsIn && pstInfo_->u8EndptUsage != USB_ENDPT_USAGE_DATA) {
        return FALSE;
      }
      break;

    case USB_SYNC_SYNC:
      // Sync endpoints use SOF for sync source.
      if (pstInfo_->u8EndptUsage != USB_ENDPT_USAGE_DATA) {
        return FALSE;
      }
      break;

    case USB_SYNC_ADAPT:
      // Adaptive from host doesn't use any feedback.
      if (!pstInfo_->bIsIn && pstInfo_->u8EndptUsage != USB_ENDPT_USAGE_DATA) {
        return FALSE;
      }
      break;

    default:
      return FALSE;
    }

    if (pstInfo_->u8EndptUsage >= USB_ENDPT_USAGE_COUNT) {
      return FALSE;
    }
    break;

  default:
    return FALSE;
  }

  switch (pstInfo_->u8TransferType) {
  case USB_XFER_TYPE_BULK:
    switch (pstInfo_->u16PacketSize) {
    case 8:
    case 16:
    case 32:
    case 64:
      break;
    default:
      return FALSE;
    }
    break;

  case USB_XFER_TYPE_INT:
    if (pstInfo_->u16PacketSize > 64) {
      return FALSE;
    }
    break;

  case USB_XFER_TYPE_ISO:
    if (pstInfo_->u16PacketSize > 1023) {
      return FALSE;
    }
    break;
  }

  switch (pstInfo_->u8TransferType) {
  case USB_XFER_TYPE_BULK:
    if (pstInfo_->u8Interval != 0) {
      return FALSE;
    }
    break;

  case USB_XFER_TYPE_INT:
    if (pstInfo_->u8Interval == 0) {
      return FALSE;
    }
    break;

  case USB_XFER_TYPE_ISO:
    if (pstInfo_->u8Interval == 0 || pstInfo_->u8Interval > 16) {
      return FALSE;
    }
    break;
  }

  return TRUE;
}
