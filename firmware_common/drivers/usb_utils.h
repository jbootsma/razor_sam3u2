#ifndef USB_UTILS_H
#define USB_UTILS_H

/*!<
 * Useful utilities for working with USB.
 */

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

/// Class code for vendor specific interfaces.
#define USB_CLASS_VENDOR_SPECIFIC 0xFF

// Any specific class/subclass/protocol codes should be defined along with the
// implementation of that device class.

#define USB_LANG_ID_EN_US 0x0409

/*
These macros make declaring instances of descriptors easy. Their use looks
something like

static const UsbADescType stADesc = {
  .stHeader = USB_DESC_A_HEADER,
  ...
};
*/

#define USB_DEV_DESC_HEADER                                                    \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbDevDescType), .eType = USB_DESC_TYPE_DEV,            \
  }

#define USB_DEV_QUAL_DESC_HEADER                                               \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbDevQualDescType), .eType = USB_DESC_TYPE_DEV_QUAL,   \
  }

#define USB_CFG_DESC_HEADER                                                    \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbCfgDescType), .eType = USB_DESC_TYPE_CFG,            \
  }

#define USB_OTHER_SPEED_CFG_DESC_HEADER                                        \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbCfgDescType),                                        \
    .eType = USB_DESC_TYPE_OTHER_SPEED_CFG,                                    \
  }

#define USB_IFACE_DESC_HEADER                                                  \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbIfaceDescType), .eType = USB_DESC_TYPE_IFACE,        \
  }

#define USB_EPT_DESC_HEADER                                                    \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbEptDescType), .eType = USB_DESC_TYPE_EPT,            \
  }

#define USB_BOS_DESC_HEADER                                                    \
  (UsbDescHeaderType) {                                                        \
    .u8Length = sizeof(UsbBosDescType), .eType = USB_DESC_TYPE_BOS,            \
  }

#define USB_DEV_CAP_200_EXT_HEADER                                             \
  (UsbDevCapHeaderType) {                                                      \
    .stDescHeader = {.u8Length = sizeof(UsbDevCap200ExtType),                  \
                     .eType = USB_DESC_TYPE_DEV_CAPABILITY},                   \
    .eCap = USB_DEV_CAP_200_EXT,                                               \
  }

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

/**
 * Usb representation of a 3-part version number.
 * Field ranges are limited:
 * - Major can go up to 255.
 * - Minor/Patch can go up to 15.
 */
typedef struct __attribute__((packed)) {
  u8 u4Patch : 4;
  u8 u4Minor : 4;
  u8 u8Major;
} UsbVersionType;

/// @brief USB class identifier
typedef struct __attribute__((packed)) {
  u8 u8Class;
  u8 u8Subclass;
  u8 u8Protocol;
} UsbClassType;

/// @brief Common prefix for all USB descriptors
typedef struct __attribute__((packed)) {
  u8 u8Length;
  UsbDescType eType : 8;
} UsbDescHeaderType;

// Refer to the USB spec. for details on the following descriptors, esp. how
// they are arranged within the configuration descriptor response.

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  UsbVersionType stUsbVersion;
  UsbClassType stDevClass;
  u8 u8Ep0PktSize;
  u16 u16Vid;
  u16 u16Pid;
  UsbVersionType stDevVersion;
  u8 u8ManufacturerStr;
  u8 u8ProductStr;
  u8 u8SerialStr;
  u8 u8NumCfgs;
} UsbDevDescType;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  UsbVersionType stUsbVersion;
  UsbClassType stDevClass;
  u8 u8Ep0PktSize;
  u8 u8NumCfgs;
  u8 _reserved;
} UsbDevQualDescType;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  u16 u16TotLen;
  u8 u8NumIfaces;
  u8 u8CfgIdx;
  u8 u8CfgStr;
  struct __attribute__((packed)) {
    u8 _reserved : 5;
    u8 bRemoteWakeup : 1;
    u8 bSelfPowered : 1;
    u8 _deprecated : 1; // Must be set to 1 according to spec.
  } stAttrib;
  u8 u8MaxPower; // In units of 2 mA (ie. 50 == 100 mA)
} UsbCfgDescType;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  u8 u8IfaceIdx;
  u8 u8AltIdx;
  u8 u8NumEpts;
  UsbClassType stIfaceClass;
  u8 u8IfaceStr;
} UsbIfaceDescType;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  struct __attribute__((packed)) {
    u8 u4EptNum : 4;
    u8 _reserved : 3;
    UsbEptDirType eDir : 1;
  } stAddress;
  struct __attribute__((packed)) {
    UsbTransferType eXfer : 2;
    UsbSyncType eSync : 2;
    UsbEptUsageType eUsage : 2;
    u8 _reserved : 2;
  } stAttrib;
  u16 u16MaxPktSize;
  u8 u8Interval;
} UsbEptDescType;

/**
 * @brief A list of discriptors. Use MAKE_USB_DESC_LIST to easily create one.
 *
 * This is useful for working with requests that return multiple descriptors at
 * once.
 */
typedef struct {
  u8 u8NumDescs;
  const void **apvDescs;
} UsbDescListType;

/**
 * Easily define the contents of UsbDescListType. Each argument to this macro
 * should be a pointer to a descriptor that is included in the list.
 */
#define MAKE_USB_DESC_LIST(...)                                                \
  (UsbDescListType) {                                                          \
    .u8NumDescs =                                                              \
        sizeof((const void *[]){__VA_ARGS__}) / sizeof(const void *),          \
    .apvDescs = (const void *[]){__VA_ARGS__},                                 \
  }

// BOS descriptors are based on USB 3.2 spec rev 1.1. They were initially
// introduced in a now-deprecated and hard-to-find wireless usb spec.
// The definitions here are only enough to support the platform-specific data
// used by windows to auto-install drivers for the device.

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stHeader;
  u16 u16TotalLength;
  u8 u8NumDeviceCaps;
} UsbBosDescType;

typedef enum {
  USB_DEV_CAP_200_EXT = 2,
  USB_DEV_CAP_PLATFORM = 5,
} UsbDevCapType;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stDescHeader;
  UsbDevCapType eCap : 8;
} UsbDevCapHeaderType;

typedef struct __attribute__((packed)) {
  UsbDevCapHeaderType stCapHeader;
  struct {
    int _reserved : 1;
    bool bLpmSupported : 1;
    int _reserved2 : 6;
  } stAttrib;
} UsbDevCap200ExtType;

typedef struct __attribute__((packed)) {
  UsbDevCapHeaderType stCapHeader;
  u8 _reserved;
  u8 au8Uuid[16];
} UsbPlatformDescHeaderType;

// Microsoft specifics for their own BOS platform descriptor, and their OS
// descriptors 2.0. The document describing them can be found at
// https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification

typedef struct __attribute__((packed)) {
  u32 u32NtddiWinVersion;
  u16 u16DescSetLength;
  u8 u8VendorCode;
  u8 u8AltEnumCode;
} UsbMsDescSetInfoType;

// clang-format off
#define USB_MS_PLAT_CAPABILITY_UUID                                            \
  {                                                                            \
    0xDF, 0x60, 0xDD, 0xD8,                                                    \
    0x89, 0x45, 0xC7, 0x4C,                                                    \
    0x9C, 0xD2, 0x65, 0x9D,                                                    \
    0x9E, 0x64, 0x8A, 0x9F                                                     \
  }
// clang-format on

// Declare a MS platform capability descriptor. The extra arguments are the
// initializers for each UsbMsDescSetInfoType that will be included in the full
// descriptor.
#define DECL_USB_MS_PLAT_CAPABILITY(name, ...)                                 \
  struct __attribute__((packed)) {                                             \
    UsbPlatformDescHeaderType stHeader;                                        \
    UsbMsDescSetInfoType                                                       \
        astDescSets[sizeof(UsbMsDescSetInfoType[]){__VA_ARGS__} /              \
                    sizeof(UsbMsDescSetInfoType)];                             \
  } name = {                                                                   \
      .stHeader =                                                              \
          {                                                                    \
              .stCapHeader =                                                   \
                  {                                                            \
                      .stDescHeader =                                          \
                          {                                                    \
                              .u8Length = sizeof(name),                        \
                              .eType = USB_DESC_TYPE_DEV_CAPABILITY,           \
                          },                                                   \
                      .eCap = USB_DEV_CAP_PLATFORM,                            \
                  },                                                           \
              .au8Uuid = USB_MS_PLAT_CAPABILITY_UUID,                          \
          },                                                                   \
      .astDescSets = {__VA_ARGS__},                                            \
  }

typedef enum {
  USB_MS_OS_20_DESCRIPTOR_INDEX = 7,
  USB_MS_OS_20_SET_ALT_ENUMERATION = 8,
} UsbMsOsVendorCommandType;

typedef enum {
  USB_MS_OS_20_SET_HEADER_DESCRIPTOR = 0,
  USB_MS_OS_20_SUBSET_HEADER_CONFIGURATION = 1,
  USB_MS_OS_20_SUBSET_HEADER_FUNCTION = 2,
  USB_MS_OS_20_FEATURE_COMPATBLE_ID = 3,
  USB_MS_OS_20_FEATURE_REG_PROPERTY = 4,
  USB_MS_OS_20_FEATURE_MIN_RESUME_TIME = 5,
  USB_MS_OS_20_FEATURE_MODEL_ID = 6,
  USB_MS_OS_20_FEATURE_CCGP_DEVICE = 7,
  USB_MS_OS_20_FEATURE_VENDOR_REVISION = 8,

  // Force type to be at least 16 bits for use in bitfields.
  _USB_MS_OS_20_FORCE_BIG = UINT16_MAX,
} UsbMsOs20DescriptorType;

typedef struct __attribute__((packed)) {
  u16 u16Length;
  UsbMsOs20DescriptorType eType : 16;
} UsbMsOs20DescHeaderType;

typedef struct __attribute__((packed)) {
  UsbMsOs20DescHeaderType stHeader;
  u32 u32NtddiWindowsVersion;
  u16 u16TotalLength;
} UsbMsOs20DescSetHeaderType;

#define USB_MS_OS_20_SET_HEADER_DESCRIPTOR_HEADER                              \
  (UsbMsOs20DescHeaderType) {                                                  \
    .u16Length = sizeof(UsbMsOs20DescSetHeaderType),                           \
    .eType = USB_MS_OS_20_SET_HEADER_DESCRIPTOR,                               \
  }

typedef struct __attribute__((packed)) {
  UsbMsOs20DescHeaderType stHeader;
  u8 u8ConfigVal;
  u8 _reserved;
  u16 u16SubsetLength;
} UsbMsOs20ConfigSubsetHeaderType;

#define USB_MS_OS_20_SUBSET_HEADER_CONFIGURATION_HEADER                        \
  (UsbMsOs20DescHeaderType) {                                                  \
    .u16Length = sizeof(UsbMsOs20ConfigSubsetHeaderType),                      \
    .eType = USB_MS_OS_20_SUBSET_HEADER_CONFIGURATION,                         \
  }

typedef struct __attribute__((packed)) {
  UsbMsOs20DescHeaderType stHeader;
  u8 u8FirstIface;
  u8 _reserved;
  u16 u16SubsetLength;
} UsbMsOs20FunctionSubsetHeaderType;

#define USB_MS_OS_20_SUBSET_HEADER_FUNCTION_HEADER                             \
  (UsbMsOs20DescHeaderType) {                                                  \
    .u16Length = sizeof(UsbMsOs20FunctionSubsetHeaderType),                    \
    .eType = USB_MS_OS_20_SUBSET_HEADER_FUNCTION,                              \
  }

typedef struct __attribute__((packed)) {
  UsbMsOs20DescHeaderType stHeader;
  char stCompatId[8];
  char stSubCompatId[8];
} UsbMsOs20CompatitbleIdType;

#define USB_MS_OS_20_FEATURE_COMPATBLE_ID_HEADER                               \
  (UsbMsOs20DescHeaderType) {                                                  \
    .u16Length = sizeof(UsbMsOs20CompatitbleIdType),                           \
    .eType = USB_MS_OS_20_FEATURE_COMPATBLE_ID,                                \
  }

// TODO other MS OS 2.0 feature descriptors.
// registry property is challenging because it is variable length, probably
// treat similar to the MS BOS platform capability descriptor.

// These are like the standard descriptor list equivalents, but accounting for
// the different descriptor header type used by microsoft.

typedef struct {
  u8 u8NumDescs;
  const void **apvDescs;
} UsbMsOs20DescListType;

#define MAKE_USB_MS_OS_20_DESC_LIST(...)                                       \
  (UsbMsOs20DescListType) {                                                    \
    .u8NumDescs =                                                              \
        sizeof((const void *[]){__VA_ARGS__}) / sizeof(const void *),          \
    .apvDescs = (const void *[]){__VA_ARGS__},                                 \
  }

//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------

/**
 * Utility function for easily sending parts of request responses to the host.
 *
 * It shouldn't often be used directly, the other utility functions are built on
 * top of this and probably do what you want.
 *
 * When called it will write the appropriate piece of the chunk to the control
 * endpoint. If it returns true then the chunk is full written (either in this
 * call or a previous) and the next one should also be written, or the final
 * packet should be signalled completed.
 *
 * @param pstStatus_ Status of the current request.
 * @param u16ChunkStart_ Byte offset into the request response where this chunk
 * should be written.
 * @param pvData_ The data of the chunk.
 * @param u16DataLen_ The length of the chunk.
 *
 * @retval TRUE The chunk has been fully written and the next part of the
 * response should be written. TRUE is also returned if the maximum lenght of
 * the request has been reached before the chunk was fully written.
 * @retval FALSE The chunk is not completely written due to the current USB
 * packet being filled. The packet was automatically flushed, and UsbWriteChunk
 * should be called again on the next callback with the same arguments to finish
 * writing the chunk.
 */
bool UsbWriteChunk(const volatile UsbRequestStatusType *pstStatus_,
                   u16 u16ChunkStart_, const void *pvData_, u16 u16DataLen_);

/**
 * @brief Helper utility to send an array of data as a response to a request.
 *
 * This is useful when the full array is always available. The helper will
 * determine the approppriate piece of the array to write based on the request
 * offset, and will automatically end the request when the full array is
 * written.
 */
void UsbWriteArray(const volatile UsbRequestStatusType *pstStatus_,
                   const void *pvData_, u16 u16DataLen_);

/**
 * Callback for use with UsbAcceptRequest when the response is a single USB
 * descriptor.
 *
 * @param pvDesc_ should be a pointer to the descriptor to be sent as the
 * response.
 */
void UsbSendDesc(const volatile UsbRequestStatusType *pstStatus_,
                 void *pvDesc_);

/// @brief Calculate the total length in bytes of the provided list of
/// descriptors.
u16 UsbDescListByteLen(UsbDescListType stDescs_);

/**
 * Callback for use with UsbAcceptRequest that sends a list of descriptors as
 * the response.
 *
 * @param pvList_ A pointer to UsbDescListType indicating which descriptors to
 * send in the response.
 */
void UsbSendDescList(const volatile UsbRequestStatusType *pstStatus_,
                     void *pvList_);

/**
 * Dynamically allocate a descriptor describing a list of string IDs.
 *
 * This descriptor is suitable for sending as string descriptor 0. The caller
 * must free() the descriptor memory when it is done with it.
 *
 * @param u8NumLangs The number of LANG_ID values in the descriptor.
 * @param au16Langs The array of LANG_ID values to include in the descriptor.
 *
 * @return A pointer to the newly allocated descriptor, or NULL if there was an
 * error.
 */
UsbDescHeaderType *UsbCreateLangIds(u8 u8NumLangs, u16 *au16Langs);

/**
 * Dynamically allocate a string descriptor representing the provided string.
 *
 * The string may be truncated if it is too long to fit in a descriptor. The
 * caller must free() the descriptor when it is done with it as well.
 *
 * @param pcStr A null-terminated ASCII string to be converted into a
 * descriptor.
 *
 * @return The newly allocated descriptor, or NULL if there was an error.
 */
UsbDescHeaderType *UsbCreateStringDesc(const char *pcStr);

/**
 * Calculate the total length in bytes of a series of MS OS 2.0 descriptors.
 */
u16 UsbMsOs20DescListByteLen(UsbMsOs20DescListType stList_);

/**
 * Callback for use with UsbAcceptRequest that will send a list of MS OS 2.0
 * descriptors as the response.
 *
 * @param pvDescList_ A pointer to UsbMsOs20DescListType indicating the
 * descriptors to include in the response.
 */
void UsbSendMsOs20DescSet(const volatile UsbRequestStatusType *pstStatus_,
                          void *pvDescList_);

#endif
