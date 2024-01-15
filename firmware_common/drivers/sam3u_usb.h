#ifndef SAM3U_USB_H
#define SAM3U_USB_H

/*!<
 * Full-speed USB driver for the SAM3U usb peripheral.
 */

//// Configiration constants

// The maximum number of strings that can be registered for use in USB
// descriptors. Bump this if UsbRegisterString() starts returning
// USB_INVALID_STR_IDX.
#define U8_USB_MAX_STRINGS ((u8)32)

// Size of a memory arena that will be malloc'd by the driver. This pool will be
// used to store the generated descriptors and any state we don't know the size
// of ahead of time. The default 1024 should be large enough for most
// applications, but bump if needed.
#define USB_MEM_ARENA_BYTES 1024

//// Constants

/// Invalid string descriptor index. Can be used in descriptors to indicate no
/// string.
#define USB_INVALID_STR_IDX 0
/// USB reserved endpoint 0 as the default control pipe.
#define USB_DEF_CTRL_EP 0

// These defines are for working with the bmRequestType field in a usb setup
// packet.

#define USB_RQST_DIR_MASK 0x80
#define USB_RQST_DIR_FROM_HOST 0x00
#define USB_RQST_DIR_TO_HOST 0x80

#define USB_RQST_TYPE_MASK 0x60
#define USB_RQST_TYPE_STANDARD 0x00
#define USB_RQST_TYPE_CLASS 0x01
#define USB_RQST_TYPE_VENDOR 0x02

#define USB_RQST_TGT_MASK 0x1F
#define USB_RQST_TGT_DEV 0x00
#define USB_RQST_TGT_IFACE 0x01
#define USB_RQST_TGT_ENDPT 0x02
#define USB_RQST_TGT_OTHER 0x03

// Most classes should be defined in the code module that implements them, but
// some are handy to have defined globally here.
#define USB_CLASS_VENDOR_SPECIFIC 0xFF

//// Misc. types.

/// @brief USB version number
/// Versions in USB have a major, minor, and sub-minor value. They are encoded
/// into 16-bits using a binary-coded-decimal format.
typedef struct {
  u8 u8Major;
  u8 u8Minor;
  u8 u8SubMinor;
} UsbVersionType;

/// @brief USB vendor/product id combo.
typedef struct {
  u16 u16Vendor;
  u16 u16Product;
} UsbIdType;

/// @brief Classes are always specified using a device class, subclass, and
/// protocol number. Refer to the various specification on usb.org for the
/// defined values.
typedef struct {
  u8 u8Class;
  u8 u8SubClass;
  u8 u8Protocol;
} UsbClassCodeType;

/// @brief Control transfer setup packet contents
/// Contains all the information received in the initial setup packet of a
/// control transfer.
typedef struct {
  u8 u8RequestType;
  u8 u8RequestId;
  u16 u16Value;
  u16 u16Index;
  u16 u16Length;
} UsbRequestHeaderType;

/// @brief Status information passed to control request handlers.
/// If a handler is set with UsbAcceptRequest() it will be passed this status
/// structure on every poll interval.
typedef struct {
  /// @brief The setup packet that was received at the start of the request.
  UsbRequestHeaderType stHeader;

  /// @brief How much of the request data has been read/written so far.
  u16 u16RequestOffset;

  /// @brief If true the request was aborted by the driver, and this is the last
  /// poll call.
  bool bTerminated;
} UsbRequestStatusType;

/// A handler function for request polling. If set through UsbAcceptRequest it
/// will be called during every sytem tick that the control pipe is waiting on
/// the app for more data transfer or to complete the request.
///
/// It will also be called with stStats_.bTerminated set to TRUE if the request
/// had to be aborted. (Which could be due to things like a USB reset, or the
/// host moving on to a new request).
typedef void (*UsbRequestHandlerType)(
    const volatile UsbRequestStatusType *stStatus_, void *pvParam_);

//// USB Device related types

/*
Control Pipe Request Handling Notes
===================================

When a control pipe request is received, the driver will handle it internally if
it's a standard request. Otherwise events will be generated given the
application/middleware a chance to respond.

Events are sent to the most specific handler first, but if the request is not
handled the driver will fall back to more general handlers. (eg. If a request
targetted at an endpoint is received the handler for it's owning interface will
be called first, then the active config handler, and finally the device
handler).

A request can be handled fully within the event callback if all the data is
available (unlikely for control writes) and possible to handle within a single
packet. Otherwise UsbAcceptRequest() should be used to setup a handler which
will be called as more data is received or this is room to send.

Any request can be failed at any time with a call to UsbFailRequest(). Nothing
further can be done until a new request is received in this case.

If the request has associated data then UsbWrite/UsbRead is used to access the
data. UsbNextPacket() indicates when the data has been accepted/generated and
the request can move on. The request completes succesfully when the last packet
has been acknowledged this way. Keep in mind:
- For control read (ie. the device is generating data) if a packet shorter than
  the max packet size is generated it is considered the final packet, and
  calling UsbNextPacket() in this case will end the transfer with success.
- For transfers without data they are treated as a 0-length control read. This
  means they are completed succesfully by a single call to UsbNextPacket().
*/

/// @brief Events that apply to the entire device.
typedef enum {
  /// @brief A USB reset has completed. Note that there will likely be more than
  /// one reset during device enumeration.
  USB_DEV_EVT_RESET,

  /// @brief A USB suspend state has occured. This will happen after all
  /// endpoints/interfaces are
  /// suspended.
  USB_DEV_EVT_SUSPEND,

  /// @brief A USB resume has occured. This will happen before individual
  /// interfaces/endpoints
  /// are resumed.
  USB_DEV_EVT_RESUME,

  /// @brief A control pipe request was received.
  USB_DEV_EVT_CMD_REQUESTED,
} UsbDevEvtIdType;

/// @brief Device-wide USB configuration.
typedef struct {
  // Info strings will be automatically registered as string descriptors. They
  // can be left as NULL if desired.
  const char *pcManufacturerName;
  const char *pcProductName;
  const char *pcSerialNum;

  /// @brief Handler function for device-wide events.
  void (*pfnEventHandler)(UsbDevEvtIdType eEvt_);

  /// @brief PID/VID identifying the device, often used by the OS to match a
  /// suitable driver.
  UsbIdType stId;

  /// @brief Device class, refer to the spec for details. If the class is well
  /// known the OS may load a generic driver for the device.
  UsbClassCodeType stClass;

  /// @brief Device version. Update on new hardware/software revisions.
  UsbVersionType stDevVersion;
} UsbDeviceInfoType;

//// USB Configuration related types

/// @brief Events for the active configuration.
typedef enum {
  /// @brief The configuration has been set as the active configuration on the
  /// device.
  USB_CFG_EVT_SETUP,

  /// @brief The configuration is no longer active.
  USB_CFG_EVT_TEARDOWN,

  /// @brief The USB device is suspending.
  /// The configuration event will happen after the interface events but before
  /// the device-level
  /// event.
  USB_CFG_EVT_SUSPEND,

  /// @brief The USB device is resuming.
  /// This will occur after the device-level event but before interfaces are
  /// resumed.
  USB_CFG_EVT_RESUME,

  /// @brief A command request was received on the control pipe.
  USB_CFG_EVT_CMD_REQUESTED,
} UsbCfgEvtIdType;

/// @brief Configuration level config data.
typedef struct {
  /// @brief Optional name for the config.
  const char *pcConfigName;

  /// @brief Handler for events specific to this configuration.
  void (*pfnEventHandler)(UsbCfgEvtIdType eEvt_);
} UsbConfigInfoType;

//// USB interface related types

/// @brief Interface specific events.
typedef enum {
  /// @brief The interface has been made active.
  USB_IFACE_EVT_SETUP,

  /// @brief The interface is no longer active, either the owning configuration
  /// is being torn down
  /// or an alternative interface was selected.
  USB_IFACE_EVT_TEARDOWN,

  /// @brief The USB device is suspending.
  /// Interfaces will recieve the suspend events before the owning configuration
  /// or device.
  USB_IFACE_EVT_SUSPEND,

  /// @brief The USB device is resuming.
  /// Interfaces will be the last to receive resume events.
  USB_IFACE_EVT_RESUME,

  /// @brief An interface or endpoint targeted control pipe command was
  /// received.
  USB_IFACE_EVT_CMD_REQUESTED,
} UsbIfaceEvtIdType;

/// @brief Interface specific configuration data
typedef struct {
  /// @brief Optional name for the interface.
  const char *pcIfaceName;

  /// @brief Optional handler for interface events.
  void (*pfnEventHandler)(UsbIfaceEvtIdType eEvt_);

  /// @brief Interface's class code.
  /// Often used by the OS to match a driver on composite devices.
  UsbClassCodeType stClass;
} UsbIfaceInfoType;

//// USB endpoint related types.

/// @brief The types of transfer the endpoint will be used for.
typedef enum {
  USB_XFER_TYPE_CTRL,
  USB_XFER_TYPE_ISO,
  USB_XFER_TYPE_BULK,
  USB_XFER_TYPE_INT,

  USB_XFER_TYPE_COUNT,
} UsbTransferType;

/// @brief For ISO endpoints, the type of synchronization to be used.
typedef enum {
  USB_SYNC_NONE,
  USB_SYNC_ASYNC,
  USB_SYNC_ADAPT,
  USB_SYNC_SYNC,

  USB_SYNC_COUNT,
} UsbSyncType;

/// @brief The usage of an endpoint, relevant to ISO endpoints. See the USB spec
/// for details.
typedef enum {
  USB_ENDPT_USAGE_DATA,
  USB_ENDPT_USAGE_EXPLICIT_FB,
  USB_ENDPT_USAGE_IMPLICIT_FB,

  USB_ENDPT_USAGE_COUNT,
} UsbEndptUsageType;

/// @brief Information on how a USB endpoint will be used.
typedef struct {
  /// @brief Will the endpoint be for IN transfers.
  /// NOTE: This is IN as defined by the spec, which means if TRUE the endpoint
  /// is for sending data to the host (ie. UsbWrite() will be used).
  bool bIsIn;

  /// @brief Type of transfers used for the endpoint.
  /// @see UsbTransferType
  u8 u8TransferType;

  /// @brief Synchronization type of the endpoint.
  /// Should be USB_SYNC_NONE for non-ISO endpoints.
  /// @see UsbSyncType
  u8 u8SyncType;

  /// @brief Usage of the endpoint.
  /// Should be UBS_ENDPT_USAGE_DATA for most endpoints.
  /// @see UsbEndptUsageType
  u8 u8EndptUsage;

  /// @brief The maximum packet size for the endpoint.
  u16 u16PacketSize;

  /// @brief Interval value for the endpoint.
  /// Has different meanins for different transfer types, see the spec for
  /// details. For bulk transfer endpoints this should usually be 0.
  u8 u8Interval;
} UsbEndptInfoType;

//// Main loop hooks

void UsbInitialize(void);
void UsbRunActiveState(void);

//// Misc. Functions

/**
 * @brief Register a string descriptor for a string.
 *
 * This function expects the string to remaind valid for the remainder of the
 * program runtime (ie. It should be a string-literal, or a global variable).
 *
 * Only up to U8_USB_MAX_STRINGS can be registered, this function will
 * de-duplicate strings that are registered multiple times though.
 *
 * @param pcStr_ The string to be registered.
 * @return The string descriptor index, that can be used by the host to retrieve
 * the string contents. This should be used in any descriptors that refer to the
 * string.
 */
u8 UsbRegisterString(const char *pcStr_);

/**
 * @brief Change the enabled state of the USB peripheral.
 *
 * All configuration must be complete before the first time the USB device is
 * enabled. When disabled the device will simulate being detached on the port,
 * but will still draw power from the USB VBus. The spec indicates that the
 * device should not draw more than 100mA in this state.
 *
 * @param bIsEnabled_ Whether the USB interface should be enabled or disabled.
 *
 * @return TRUE if the state was succesfully switched.
 */
bool UsbSetEnabled(bool bIsEnabled_);

//// Configuration setup functions.

/*
Configuring the USB device
==========================

The driver builds up the descriptors dynamically as the configuration functions
are called, so they should be called in the same order as their descriptors
would appear:

- Set Device Info
- For each config
  - Add Config
  - Set enpoint capacities for the configuration
    - This can be done anytime before the enpoint is used in an interface.
  - Add custom configuration descriptors.
  - For each interface in the configuration
    - Add Iface (isAlternate = False).
    - Add custom interface descriptors
    - For each endpoint used by the interface:
      - UseEndpt
      - Add custom endpoint descriptors
    - For each alternative to the interface.
      - Add Iface (isAlternate = True).
      - Add endpoints and thier custom descriptors (the same as for the default
        interface).

All of the functions will return TRUE if the configuration was succesfully
appended. They mail fail if:
- They are called in the wrong order
- The device has already been enabled
- There is no room left in the memory pool
- The provided configuration fails validity checks
*/

/// @brief Set the device-wide configuration.
bool UsbSetDeviceInfo(const UsbDeviceInfoType *pstInfo_);

/// @brief Start the definition of a new configuration for the device.
bool UsbAddConfig(const UsbConfigInfoType *pstInfo_);

/// @brief Set the maximum capacity that will be used by an endpoint in the
/// current configuration.
///
/// This must be done before the endpoint is used in any UsbUseEndpt() calls for
/// the current configuration. The values provided here must be as large as the
/// largest possible usage of the endpoint across all interface alternatives in
/// the current configuration.
///
/// This may fail if the combination of configured endpoints would require more
/// buffer space than the hardware supports (for the SAM3U this is 4KiB).
///
/// @param u8Endpt_ The endpoint to configure.
/// @param u16MaxPacketSize_ The largest packet size that will be used with the
/// endpoint.
/// @param u8NumPackets_ The number of packet buffers that will be used for the
/// endpoint.
/// @return
bool UsbSetEndpointCapacity(u8 u8Endpt_, u16 u16MaxPacketSize_,
                            u8 u8NumPackets_);

/// @brief Add a new interface to the current configuration.
/// @param bIsAlternative_ If TRUE then this is an alternative version of the
/// last interface added with bIsAlternate_ equal to FALSE.
bool UsbAddIface(const UsbIfaceInfoType *pstInfo_, bool bIsAlternate_);

/// @brief Mark an endpoint as used by the current interface.
bool UsbUseEndpt(u8 u8Endpt_, const UsbEndptInfoType *pstInfo_);

/// @brief Add a new custom descriptor to the current configuration.
///
/// As noted by the USB spec custom descriptors should be appended immediately
/// after the item they modify.
///
/// @param u8DescriptorType The type field to use for the descriptor. This
/// should not be one of the standard descriptor types.
/// @param pvDescriptor_ A pointer to the raw descriptor data that will be
/// added. This will be copied by the driver.
/// @param u8DescriptorLen_ Size of the data pointed to by pvDescriptor_.
bool UsbAddCustomDescriptor(u8 u8DescriptorType, const void *pvDescriptor_,
                            u8 u8DescriptorLen_);

//// Data Transfer

/// @brief Set or clear the stalling (also known as HALT state) on an endpoint.
/// While stalling an endpoint is essentially unusable, and this is usually used
/// to indicate some sort of error that requires a recovery process.
///
/// Note that the host can use SET FEATURE requests to change the stall state
/// independent of the application.
bool UsbSetStall(u8 u8Endpt_, bool bIsStalling_);

/// @brief Check if a specific endpoint is currently in the stall state.
bool UsbIsStalling(u8 u8Endpt_);

/// @brief Write some data to the current packet for an IN endpoint.
/// @return The number of actual bytes written. Will be 0 if there was an error
/// or if there is no room in the current packet.
u16 UsbWrite(u8 u8Endpt_, const void *pvSrc_, u16 u16MaxLen_);

/// @brief Read some data from the current packet on an OUT endpoint.
/// @return The actual number of byte read. May be 0 if there was an error but
/// could also mean that the current packet was already fully consumed.
u16 UsbRead(u8 u8Endpt_, void *pvDst_, u16 u16MaxLen_);

/// @brief Indicate that the application is done processing the current packet,
/// and allow further transfers on the endpoint.
///
/// For OUT endpoints this makes the current packet buffer available to be
/// filled by the host.
///
/// For IN endpoints this queues the current packet buffer for transmission to
/// the host.
///
/// For endpoints with multiple buffers, the endpoint may be immediately ready
/// after this is called.
bool UsbNextPacket(u8 u8Endpt_);

/// @brief Check if the endpoint has an active packet.
///
/// For IN endpoints this means there's a packet buffer available for writing
/// to (though it may already have data). It will be send after the next call
/// to UsbNextPacket().
///
/// For OUT endpoints this means there's a received packet available for
/// reading. It will be discarded when UsbNextPacket() is called to make room
/// for more transfers from the host.
bool UsbIsPacketReady(u8 u8Endpt_);

/// @brief Get the size of the currently active packet on the provided endpoint.
///
/// If the endpoint has no active packet this will be 0.
///
/// For OUT endpoints it is the actual size of the received packet.
///
/// For IN endpoints it is generally the max packet size that was set in the
/// UsbUseEndpt() function. However for control transfer reads the last packet
/// may be shorter to keep the overall transfer capped to the length specified
/// in the setup packet.
u16 UsbGetPktSize(u8 u8Endpt_);

/// @brief Get the offset into the currently active packet.
///
/// If there is no active packet this will be 0.
///
/// For OUT endpoints it's the amount of data read from the packet so far.
///
/// For IN endpoints it's the amount of data written to the packet so far.
u16 UsbGetPktOffset(u8 u8Endpt_);

//// Control Point Requests

/// @brief Get the header of the current request.
///
/// This is intended to be used during handling of CMD_REQUEST events to
/// determine if the handler can process the request or not.
///
/// If there is no active request this will return NULL.
const UsbRequestHeaderType *UsbGetCurrentRequest(void);

/// @brief Accept the active request, providing a handler for request polling.
///
/// If no handler completes or accepts a request it will be automatically
/// failed. If accepted with this function then the provided request handler
/// will be called during system ticks where the request is active and any of
/// the following holds:
/// A. There is data available to read during a control write.
/// B. The driver is waiting for data to be written during a control read.
/// C. The driver is waiting for the request to be completed through a call to
///    UsbNextPacket().
///
/// @param pvParam_ An arbitrary value that will be passed to the request each
/// time it is called for this request.
bool UsbAcceptRequest(UsbRequestHandlerType fnRequestHandler_, void *pvParam_);

/// @brief Fail the active request.
///
/// This will stop any further processing of the request and indicate failure to
/// the host.
bool UsbFailRequest(void);

#endif
