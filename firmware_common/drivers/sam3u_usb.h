#ifndef SAM3U_USB_H
#define SAM3U_USB_H

/*!<
 * Full-speed USB driver for the SAM3U usb peripheral.
 */

/*
Control Pipe Request Handling Notes
===================================

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

There is a small set of requests that the driver will handle internally:
- Set/Clear feature for the DEVICE_REMOTE_WAKEUP, ENDPOINT_HALT, and TEST_MODE
  features.
- Get Status
- Set Address

All other requests will be forwarded to the user app.
*/

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

/// USB reserves endpoint 0 for the default control pipe.
#define USB_DEF_CTRL_EP 0

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// All of the enums here correspond to values directly from the USB spec.
// The spec can be acquired from usb-if.org, and it explains the meaning of
// each.

/// @brief Direction of transfers on an endpoint
typedef enum {
  USB_EPT_DIR_OUT = 0,
  USB_EPT_DIR_IN = 1,

  // Aliases that are a bit clearer then just IN/OUT (especially because IN/OUT
  // is backwards from the device's point of view).
  USB_EPT_DIR_TO_HOST = USB_EPT_DIR_IN,
  USB_EPT_DIR_FROM_HOST = USB_EPT_DIR_OUT,
} UsbEptDirType;

typedef enum {
  USB_XFER_CTRL = 0,
  USB_XFER_ISO = 1,
  USB_XFER_BULK = 2,
  USB_XFER_INTERRUPT = 3,
} UsbTransferType;

typedef enum {
  USB_REQ_DIR_HOST_TO_DEV = 0,
  USB_REQ_DIR_DEV_TO_HOST = 1,
} UsbRequestDirType;

typedef enum {
  USB_REQ_TYPE_STANDARD = 0,
  USB_REQ_TYPE_CLASS = 1,
  USB_REQ_TYPE_VENDOR = 2,
  // 3 is reserved
} UsbRequestType;

typedef enum {
  USB_REQ_TGT_DEV = 0,
  USB_REQ_TGT_IFACE = 1,
  USB_REQ_TGT_EPT = 2,
  USB_REQ_TGT_OTHER = 3,
  // 4..31 are reserved
} UsbRequestTgtType;

typedef enum {
  USB_REQ_GET_STATUS = 0,
  USB_REQ_CLEAR_FEATURE = 1,
  // 2 is reserved
  USB_REQ_SET_FEATURE = 3,
  // 4 is reserved
  USB_REQ_SET_ADDRESS = 5,
  USB_REQ_GET_DESCRIPTOR = 6,
  USB_REQ_SET_DESCRIPTOR = 7,
  USB_REQ_GET_CFG = 8,
  USB_REQ_SET_CFG = 9,
  USB_REQ_GET_IFACE = 10,
  USB_REQ_SET_IFACE = 11,
  USB_REQ_SYNCH_FRAME = 12,
} UsbStandardRequestIdType;

typedef enum {
  USB_DESC_TYPE_DEV = 1,
  USB_DESC_TYPE_CFG = 2,
  USB_DESC_TYPE_STRING = 3,
  USB_DESC_TYPE_IFACE = 4,
  USB_DESC_TYPE_EPT = 5,
  USB_DESC_TYPE_DEV_QUAL = 6,
  USB_DESC_TYPE_OTHER_SPEED_CFG = 7,
  USB_DESC_TYPE_IFACE_PWR = 8,
  USB_DESC_TYPE_IFACE_ASSOC = 11,
  USB_DESC_TYPE_BOS = 15,
  USB_DESC_TYPE_DEV_CAPABILITY = 16,
} UsbDescType;

typedef enum {
  USB_SYNC_NONE = 0,
  USB_SYNC_ASYNC = 1,
  USB_SYNC_ADAPT = 2,
  USB_SYNC_SYNC = 3,
} UsbSyncType;

typedef enum {
  USB_EPT_USAGE_DATA = 0,
  USB_EPT_USAGE_FB = 1,
  USB_EPT_USAGE_IMPLICIT_FB = 2,
  // 3 reserved
} UsbEptUsageType;

typedef enum {
  USB_FEAT_ID_DEVICE_REMOTE_WAKEUP = 1,
  USB_FEAT_ID_ENDPOINT_HALT = 0,
  USB_FEAT_ID_TEST_MODE = 2,
} UsbStandardFeatureIdType;

/**
 * @brief Setup data for a control transfer.
 *
 * Fields correspond to the one described in the USB spec for a Setup packet.
 */
typedef struct __attribute__((packed)) {
  struct __attribute__((packed)) {
    UsbRequestTgtType eTgt : 5;
    UsbRequestType eType : 2;
    UsbRequestDirType eDir : 1;
  } stRequestType;

  u8 u8RequestId;
  u16 u16Value;
  u16 u16Index;
  u16 u16Length;
} UsbSetupPacketType;

/**
 * @brief Status information passed to control request handlers.
 *
 * If a handler is set with UsbAcceptRequest() it will be passed this status
 * structure on every poll interval.
 */
typedef struct {
  /// @brief The setup packet that was received at the start of the request.
  UsbSetupPacketType stHeader;

  /// @brief How much of the total request data has been read/written so far.
  /// This is useful to deal with requests that are split across multple
  /// packets.
  u16 u16RequestOffset;
} UsbRequestStatusType;

/// @brief Events generated by the driver.
typedef enum {
  /// @brief A USB reset was detected.
  /// @note This will happen multiple times during the enumeration process.
  USB_EVT_RESET,

  /// @brief A USB suspension was detected.
  /// @note The user code should ensure that the limits on suspend mode power
  /// draw are met.
  USB_EVT_SUSPEND,

  /// @brief USB activity is resumed.
  USB_EVT_RESUME,

  /// @brief A request was detected on the default control pipe.
  /// @note The event handler must either complete, fail or accept the request
  /// before returning. If this is not done the driver will automatically fail
  /// the request.
  USB_EVT_REQUEST,
} UsbEventIdType;

/**
 * @brief A handler function for request polling.
 *
 * If set through UsbAcceptRequest() it will be called during every sytem tick
 * that the control pipe is waiting on the app to read/write data or to signal
 * that the request is complete.
 *
 * @param stStatus_ Status of the current in-progress request. const because it
 * should not be modified directly. volatile because it will be modified as data
 * is written/read.
 * @param pvUserData_ Whatever was passed as user data to UsbAcceptRequest()
 */
typedef void (*UsbRequestHandlerCb)(const volatile UsbRequestStatusType *stStatus_, void *pvUserData_);

/**
 * @brief A handler for cleaning up after a request.
 * If such a handler was passed to UsbAcceptRequest() it will be called once the
 * request processing has completed. This can be due to:
 *   - The request has actually completed.
 *   - The request was aborted by the driver.
 *     - This could be due to USB reset or a new request being started.
 *   - The request was aborted by user code. (See UsbFailRequest())
 */
typedef void (*UsbRequestCleanupCb)(void *pvParam_);

/**
 * @brief Configuration for a single endpoint.
 */
typedef struct {
  /// @brief Maximum packet size that will be used with this endpoint.
  u16 u16MaxPacketSize;

  /// @brief The maximum number of packets to buffer for this endpoint.
  /// Must be at least 1.
  u8 u8NumPackets;

  /// @brief What type of transfers will the endpoint be used for.
  UsbTransferType eXferType;

  /// @brief Direction of data transfers on this endpoint.
  UsbEptDirType eDir;
} UsbEndpointConfigType;

/// @brief Configuration for the USB driver.
typedef struct {
  /// @brief Handler function for USB events.
  void (*pfnEventHandler)(UsbEventIdType eEvt_);

  UsbEndpointConfigType stFullSpeedEp0Cfg;

  UsbEndpointConfigType stHighSpeedEp0Cfg;

  /// @brief Whether to allow for high-speed operation.
  /// @note If supported the application code must handle descriptor requests
  ///       for Device_Qualifier and Other_Speed_Configuration descriptors.
  bool bHighSpeedEnabled;
} UsbDriverConfigType;

//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------

// Main loop hooks

void UsbInitialize(void);
void UsbRunActiveState(void);

// Configuration

/**
 * @brief Configure the USB driver
 *
 * This must be done once before enabling the USB interface.
 * The driver config can be set multiple times, but if the USB interface is
 * enabled when a new config is set it will be forcibly disabled first.
 */
bool UsbSetDriverConfig(const UsbDriverConfigType *pstConfig_);

/**
 * @brief Check if an endpoint configuration is valid
 *
 * Checks a given endpoint configuration against the drivers capabilities.
 * Endpoint numbers are implicit based on the index in the array. (ie. the first
 * config is for endpoint 1, the second for endpoint 2, and so on).
 *
 * @param u8NumEndpoints_ The number of endpoints in the configuration
 * @param astConfigs_ Pointer to an array of endpoint configurations, of length
 * u8NumEndpoints_
 * @param ppcErrorStrOut_ If non-null, will be pointed at a string describing
 * any validation error.
 *
 * @retval TRUE The driver can support the provided endpoint configuration.
 * @retval FALSE The driver is not able to support the endpoint configuration.
 */
bool UsbValidateEndpoints(const UsbEndpointConfigType *pstEp0Cfg_, u8 u8NumExtraEndpoints_,
                          const UsbEndpointConfigType *astConfigs_, const char **ppcErrorStrOut_);

/**
 * @brief Configure the active set of endpoints
 *
 * Set up the driver's endpoint configuration, including the needed backing
 * memory for the requested endpoint configuration. This should usually be
 * called in response to a set configuration command to setup the endpoints
 * specific to that configuration.
 *
 * @note After a USB reset only EP0 is configured, this must be called to setup
 * further endpoints.
 *
 * @warning When the endpoints are reconfigured only the control endpoint
 * retains any packet state. If the device is enabled the old and new
 * configuration for the default control endpoint must match.
 */
bool UsbSetEndpointsConfig(u8 u8NumExtraEndpoints_, const UsbEndpointConfigType *astConfigs_);

/**
 * @brief Change the enabled state of the USB peripheral.
 *
 * When disabled the device will simulate being detached on the port, but will
 * still draw power from the USB VBus. The spec indicates that the device should
 * not draw more than 100mA in this state. Once enabled and a configuration is
 * set then the device may draw as much power as that configuration's descriptor
 * indicates.
 *
 * @pre UsbSetDriverConfig() Must have been used to configure the driver.
 * @pre UsbSetEndpointsConfig() Must have been used to set a valid control
 * endpoint configuration.
 *
 * @param bIsEnabled_ Whether the USB interface should be enabled or disabled.
 *
 * @return TRUE if the state was succesfully switched.
 */
bool UsbSetEnabled(bool bIsEnabled_);

/// @brief Check if the USB interface is currently enabled.
bool UsbIsEnabled(void);

/// @brief Check if the USB interface is currently suspended.
bool UsbIsSuspended(void);

/// @brief Check if the USB interface is currently in high speed mode.
bool UsbIsHighSpeed(void);

/// @brief Get the current USB frame number. This will be 0 if the interface is
/// not active.
u16 UsbGetFrame(void);

//// Data Transfer

/**
 * @brief Set or clear the stalling (also known as HALT state) on an
 * endpoint.
 *
 * While stalling an endpoint is essentially unusable, and this is usually
 * used to indicate some sort of error that requires a recovery process.
 */
bool UsbSetStall(u8 u8Endpt_, bool bIsStalling_);

/// @brief Check if a specific endpoint is currently in the stall state.
bool UsbIsStalling(u8 u8Endpt_);

/**
 * @brief Write some data to the current packet for an IN endpoint.
 *
 * @return The number of actual bytes written. Will be 0 if there was an error
 * or if there is no room in the current packet.
 */
u16 UsbWrite(u8 u8Endpt_, const void *pvSrc_, u16 u16MaxLen_);

/**
 * @brief Read some data from the current packet on an OUT endpoint.
 *
 * @return The actual number of byte read. May be 0 if there was an error but
 * could also mean that the current packet was already fully consumed.
 */
u16 UsbRead(u8 u8Endpt_, void *pvDst_, u16 u16MaxLen_);

/**
 * @brief Indicate that the application is done processing the current packet,
 * and allow further transfers on the endpoint.
 *
 * For OUT endpoints this makes the current packet buffer available to be
 * filled by the host.
 *
 * For IN endpoints this queues the current packet buffer for transmission to
 * the host.
 *
 * For endpoints with multiple buffers, the endpoint may be immediately ready
 * after this is called.
 */
bool UsbNextPacket(u8 u8Endpt_);

/**
 * @brief Check if the endpoint has an active packet.
 *
 * For IN endpoints this means there's a packet buffer available for writing
 * to (though it may already have some data). It will be sent after the next
 * call to UsbNextPacket().
 *
 * For OUT endpoints this means there's a received packet available for
 * reading. It will be discarded when UsbNextPacket() is called to make room
 * for more transfers from the host.
 */
bool UsbIsPacketReady(u8 u8Endpt_);

/**
 * @brief Get the size of the currently active packet on the provided endpoint.
 *
 * If the endpoint has no active packet this will be 0.
 *
 * For OUT endpoints it is the actual size of the received packet.
 *
 * For IN endpoints it is generally the max packet size that was set in the
 * UsbUseEndpt() function. However for control transfer reads the last packet
 * may be shorter to keep the overall transfer capped to the length specified
 * in the setup packet.
 */
u16 UsbGetPktSize(u8 u8Endpt_);

/** @brief Get the offset into the currently active packet.
 *
 * If there is no active packet this will be 0.
 *
 * For OUT endpoints it's the amount of data read from the packet so far.
 *
 * For IN endpoints it's the amount of data written to the packet so far.
 */
u16 UsbGetPktOffset(u8 u8Endpt_);

//// Control Point Requests

/**
 * @brief Get the header of the current request.
 *
 * This is intended to be used during handling of USB_EVT_REQUEST events to
 * determine if the handler can process the request or not.
 *
 * If there is no active request this will return NULL.
 */
const UsbSetupPacketType *UsbGetCurrentRequest(void);

/**
 * @brief Accept the active request, providing a handler for request polling.
 *
 * If nothing completes or accepts a request it will be automatically failed. If
 * accepted with this function then the provided request handler will be called
 * during system ticks where the request is active and any of the following
 * holds:
 *   A. There is data available to read during a control write.
 *   B. The driver is waiting for data to be written during a control read.
 *   C. The driver is waiting for the request to be completed through a call to
 *      UsbNextPacket().
 *
 * @param fnRequestHandler_ The required handler function that will be called
 * while the request is active.
 * @param fnRequestCleanup_ An optional cleanup handler for the request. If not
 * NULL it will be called when the request is completed or aborted. It can be
 * used to free any memory or other resources that may have been acquired at the
 * time the request was accepted.
 * @param pvUserData_ Arbitrary data that will be passed to the handler and
 * cleanup callbacks. The caller must ensure this stays valid until the request
 * is completed.
 *
 * @return TRUE if there was a pending request and it has been succesfully
 * accepted. FALSE otherwise.
 */
bool UsbAcceptRequest(UsbRequestHandlerCb fnRequestHandler_, UsbRequestCleanupCb fnRequestCleanup_, void *pvUserData_);

/**
 * @brief Fail the active request.
 *
 * This will stop any further processing of the request and indicate failure to
 * the host.
 *
 * If there is no ongoing request this is a noop.
 */
void UsbFailRequest(void);

#endif
