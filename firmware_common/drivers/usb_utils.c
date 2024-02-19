// Utilities for working with USB data. Mostly focused around easy request
// handling and sending of descriptors.

//------------------------------------------------------------------------------
//         Headers
//------------------------------------------------------------------------------

#include "configuration.h"

//------------------------------------------------------------------------------
//         Function Prototypes
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//         Public Function Impls
//------------------------------------------------------------------------------

bool UsbWriteChunk(const volatile UsbRequestStatusType *pstStatus_, u16 u16ChunksStart_, const void *pvData_,
                   u16 u16DataLen_) {
  if (pstStatus_->u16RequestOffset == pstStatus_->stHeader.u16Length) {
    return TRUE;
  }

  if (pstStatus_->u16RequestOffset < u16ChunksStart_) {
    UsbFailRequest();
    return FALSE;
  }

  u16 u16ChunkOffset = pstStatus_->u16RequestOffset - u16ChunksStart_;

  if (u16ChunkOffset >= u16DataLen_) {
    return TRUE;
  }

  u16DataLen_ -= u16ChunkOffset;
  pvData_ = (u8 *)pvData_ + u16ChunkOffset;
  u16 u16Remain = pstStatus_->stHeader.u16Length - pstStatus_->u16RequestOffset;

  if (u16Remain < u16DataLen_) {
    u16DataLen_ = u16Remain;
  }

  if (u16DataLen_ == UsbWrite(USB_DEF_CTRL_EP, pvData_, u16DataLen_)) {
    return TRUE;
  } else {
    UsbNextPacket(USB_DEF_CTRL_EP);
    return FALSE;
  }
}

void UsbWriteArray(const volatile UsbRequestStatusType *pstStatus_, const void *pvData_, u16 u16DataLen_) {
  if (UsbWriteChunk(pstStatus_, 0, pvData_, u16DataLen_)) {
    UsbNextPacket(USB_DEF_CTRL_EP);
  }
}

void UsbSendDesc(const volatile UsbRequestStatusType *pstStatus_, void *pvDesc_) {
  const UsbDescHeaderType *pstDesc = pvDesc_;
  UsbWriteArray(pstStatus_, pstDesc, pstDesc->u8Length);
}

u16 UsbDescListByteLen(UsbDescListType stDescs_) {
  u16 u16Len = 0;

  for (u8 u8Idx = 0; u8Idx < stDescs_.u8NumDescs; u8Idx++) {
    const UsbDescHeaderType *pstHeader = stDescs_.apvDescs[u8Idx];
    u16Len += pstHeader->u8Length;
  }

  return u16Len;
}

void UsbSendDescList(const volatile UsbRequestStatusType *pstStatus_, void *pvList_) {
  const UsbDescListType *pstList = pvList_;
  u16 u16DescStart_ = 0;

  for (u8 u8Idx = 0; u8Idx < pstList->u8NumDescs; u8Idx++) {
    const UsbDescHeaderType *pstHeader = pstList->apvDescs[u8Idx];

    if (!UsbWriteChunk(pstStatus_, u16DescStart_, pstHeader, pstHeader->u8Length)) {
      return;
    }

    u16DescStart_ += pstHeader->u8Length;
  }

  UsbNextPacket(USB_DEF_CTRL_EP);
}

UsbDescHeaderType *UsbCreateLangIds(u8 u8NumLangs, u16 *au16Langs) {
  static const u8 u8MaxLangs = (UINT8_MAX - sizeof(UsbDescHeaderType)) / sizeof(u16);

  if (u8NumLangs > u8MaxLangs) {
    u8NumLangs = u8MaxLangs;
  }

  u8 u8DescLen = sizeof(UsbDescHeaderType) + u8NumLangs * sizeof(u16);
  UsbDescHeaderType *pstDesc = malloc(u8DescLen);
  if (pstDesc == NULL) {
    return NULL;
  }

  pstDesc->eType = USB_DESC_TYPE_STRING;
  pstDesc->u8Length = u8DescLen;
  memcpy(pstDesc + 1, au16Langs, u8NumLangs * sizeof(u16));
  return pstDesc;
}

UsbDescHeaderType *UsbCreateStringDesc(const char *pcStr) {
  static const u8 u8MaxChars = (UINT8_MAX - sizeof(UsbDescHeaderType)) / sizeof(u16);

  u8 u8NumChars = 0;
  for (const char *pcTmp = pcStr; *pcTmp != '\0' && u8NumChars < u8MaxChars; pcTmp++) {
    if (*pcTmp & 0x80) {
      // TODO: Full utf8 support.
      return NULL;
    }

    u8NumChars += 1;
  }

  u8 u8DescLen = sizeof(UsbDescHeaderType) + u8NumChars * sizeof(u16);
  UsbDescHeaderType *pstHdr = malloc(u8DescLen);
  if (pstHdr == NULL) {
    return NULL;
  }

  pstHdr->eType = USB_DESC_TYPE_STRING;
  pstHdr->u8Length = u8DescLen;
  u16 *pstDest = (u16 *)(pstHdr + 1);

  for (; u8NumChars > 0; u8NumChars--) {
    *pstDest = *pcStr;
    pstDest++;
    pcStr++;
  }

  return pstHdr;
}

u16 UsbMsOs20DescListByteLen(UsbMsOs20DescListType stList_) {
  u32 u32Len = 0; // Big enough to not overflow.
  for (u8 u8Idx = 0; u8Idx < stList_.u8NumDescs; u8Idx++) {
    const UsbMsOs20DescHeaderType *pstHeader = stList_.apvDescs[u8Idx];
    u32Len += pstHeader->u16Length;
  }

  if (u32Len > UINT16_MAX) {
    return UINT16_MAX;
  }

  return (u16)u32Len;
}

void UsbSendMsOs20DescSet(const volatile UsbRequestStatusType *pstStatus_, void *pvDescList_) {
  const UsbMsOs20DescListType *pstList = pvDescList_;
  u16 u16DescStart = 0;

  for (u8 u8Idx = 0; u8Idx < pstList->u8NumDescs; u8Idx++) {
    const UsbMsOs20DescHeaderType *pstHeader = pstList->apvDescs[u8Idx];

    if (!UsbWriteChunk(pstStatus_, u16DescStart, pstHeader, pstHeader->u16Length)) {
      return;
    }

    u16DescStart += pstHeader->u16Length;
  }

  UsbNextPacket(USB_DEF_CTRL_EP);
}

//------------------------------------------------------------------------------
//         Private Function Implementations
//------------------------------------------------------------------------------
