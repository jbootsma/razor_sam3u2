/**<
 * @brief Utilities for implementing USB audio devices
 */

#ifndef USB_AUDIO_H
#define USB_AUDIO_H

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
  USB_AUDIO_CAT_UNDEF = 0x00,
  USB_AUDIO_CAT_DESKTOP_SPEAKER = 0x01,
  USB_AUDIO_CAT_HOME_THEATER = 0x02,
  USB_AUDIO_CAT_MICROPHONE = 0x03,
  USB_AUDIO_CAT_HEADSET = 0x04,
  USB_AUDIO_CAT_TELEPHONE = 0x05,
  USB_AUDIO_CAT_CONVERTER = 0x06,
  USB_AUDIO_CAT_RECORDER = 0x07,
  USB_AUDIO_CAT_IO_BOX = 0x08,
  USB_AUDIO_CAT_MUSICAL_INSTRUMENT = 0x09,
  USB_AUDIO_CAT_PRO_AUDIO = 0x0A,
  USB_AUDIO_CAT_AV = 0x0B,
  USB_AUDIO_CAT_CONTROL_PANEL = 0x0C,
  USB_AUDIO_CAT_OTHER = 0xFF,
} UsbAudioFunctionCategoryType;

typedef enum {
  USB_AUDIO_CS_UNDEF = 0x20,
  USB_AUDIO_CS_DEV = 0x21,
  USB_AUDIO_CS_CONFIG = 0x22,
  USB_AUDIO_CS_STRING = 0x23,
  USB_AUDIO_CS_IFACE = 0x24,
  USB_AUDIO_CS_EPT = 0x25,
} UsbAudioClassDescType;

typedef enum {
  USB_AUDIO_AC_DESC_UNDEF = 0x00,
  USB_AUDIO_AC_DESC_HEADER = 0x01,
  USB_AUDIO_AC_DESC_INPUT_TERM = 0x02,
  USB_AUDIO_AC_DESC_OUTPUT_TERM = 0x03,
  USB_AUDIO_AC_DESC_MIXER_UNIT = 0x04,
  USB_AUDIO_AC_DESC_SELECTOR_UNIT = 0x05,
  USB_AUDIO_AC_DESC_FEATURE_UNIT = 0x06,
  USB_AUDIO_AC_DESC_EFFECT_UNIT = 0x07,
  USB_AUDIO_AC_DESC_PROCESSING_UNIT = 0x08,
  USB_AUDIO_AC_DESC_EXTENSION_UNIT = 0x09,
  USB_AUDIO_AC_DESC_CLOCK_SOURCE = 0x0A,
  USB_AUDIO_AC_DESC_CLOCK_SELECTOR = 0x0B,
  USB_AUDIO_AC_DESC_CLOCK_MULTIPLIER = 0x0C,
  USB_AUDIO_AC_DESC_SAMPLE_RATE_CONVERTER = 0x0D,
} UsbAudioCtrlDescSubType;

typedef enum {
  USB_AUDIO_AI_DESC_UNDEF = 0x00,
  USB_AUDIO_AI_DESC_GENERAL = 0x01,
  USB_AUDIO_AI_DESC_FORMAT_TYPE = 0x02,
  USB_AUDIO_AI_DESC_ENCODER = 0x03,
  USB_AUDIO_AI_DESC_DECODER = 0x04,
} UsbAudioIfaceDescSubType;

typedef enum {
  USB_AUDIO_EFFECT_UNDEF = 0x00,
  USB_AUDIO_EFFECT_PARAM_EQ_SECTION = 0x01,
  USB_AUDIO_EFFECT_REVERBERATION = 0x02,
  USB_AUDIO_EFFECT_MOD_DELAY = 0x03,
  USB_AUDIO_EFFECT_DYN_RANGE_COMP = 0x04,

  USB_AUDIO_EFFECT__FORCE_BIG = 0xFFFF,
} UsbAudioEffectUnitType;

typedef enum {
  USB_AUDIO_PROCESS_UNDEF = 0x00,
  USB_AUDIO_PROCESS_UP_DOWN_MIX = 0x01,
  USB_AUDIO_PROCESS_DOLBY_PROLOGIC = 0x02,
  USB_AUDIO_PROCESS_STEREO_EXTENDER = 0x03,

  USB_AUDIO_PROCESS__FORCE_BIG = 0xFFFF,
} UsbAudioProcessingUnitType;

typedef enum {
  USB_AUDIO_AEP_DESC_UNDEF = 0x00,
  USB_AUDIO_AEP_DESC_GENERAL = 0x01,
} UsbAudioEptDescSubType;

typedef enum {
  USB_AUDIO_REQ_UNDEF = 0x00,
  USB_AUDIO_REQ_CUR = 0x01,
  USB_AUDIO_REQ_RANGE = 0x02,
  USB_AUDIO_REQ_MEM = 0x03,
} UsbAudioRequestIdType;

typedef enum {
  USB_AUDIO_ENCODER_UNDEF = 0x00,
  USB_AUDIO_ENCODER_OTHER = 0x01,
  USB_AUDIO_ENCODER_MPEG = 0x02,
  USB_AUDIO_ENCODER_AC3 = 0x03,
  USB_AUDIO_ENCODER_WMA = 0x04,
  USB_AUDIO_ENCODER_DTS = 0x05,
} UsbAudioEncoderType;

typedef enum {
  UBS_AUDIO_DECODER_UNDEF = 0x00,
  USB_AUDIO_DECODER_OTHER = 0x01,
  USB_AUDIO_DECODER_MPEG = 0x02,
  USB_AUDIO_DECODER_AC3 = 0x03,
  USB_AUDIO_DECODER_WMA = 0x04,
  USB_AUDIO_DECODER_DTS = 0x05,
} UsbAudioDecoderType;

typedef enum {
  USB_AUDIO_CLK_SRC_CTRL_UNDEF = 0x00,
  USB_AUDIO_CLK_SRC_CTRL_SAM_FREQ = 0x01,
  USB_AUDIO_CLK_SRC_CTRL_CLOCK_VALID = 0x02,
} UsbAudioClkSrcCtrlType;

typedef enum {
  USB_AUDIO_CLK_SEL_CTRL_UNDEF = 0x00,
  USB_AUDIO_CLK_SEL_CTRL_SELECTOR = 0x01,
} UsbAudioClkSelCtrlType;

typedef enum {
  USB_AUDIO_CLK_MUL_CTRL_UNDEF = 0x00,
  USB_AUDIO_CLK_MUL_CTRL_NUMERATOR = 0x01,
  USB_AUDIO_CLK_MUL_CTRL_DENOMINATOR = 0x02,
} UsbAudioClkMulCtrlType;

typedef enum {
  USB_AUDIO_TERM_CTRL_UNDEF = 0x00,
  USB_AUDIO_TERM_CTRL_COPY_PROTECT = 0x01,
  USB_AUDIO_TERM_CTRL_CONNECTOR = 0x02,
  USB_AUDIO_TERM_CTRL_OVERLOAD = 0x03,
  USB_AUDIO_TERM_CTRL_CLUSTER = 0x04,
  USB_AUDIO_TERM_CTRL_UNDERFLOW = 0x05,
  USB_AUDIO_TERM_CTRL_OVERLFLOW = 0x06,
  USB_AUDIO_TERM_CTRL_LATENCY = 0x07,
} UsbAudioTermCtrlType;

typedef enum {
  USB_AUDIO_MIXER_CTRL_UNDEF = 0x00,
  USB_AUDIO_MIXER_CTRL_MIXER = 0x01,
  USB_AUDIO_MIXER_CTRL_CLUSTER = 0x02,
  USB_AUDIO_MIXER_CTRL_UNDERFLOW = 0x03,
  USB_AUDIO_MIXER_CTRL_OVERFLOW = 0x04,
  USB_AUDIO_MIXER_CTRL_LATENCY = 0x05,
} UsbAudioMixerCtrlType;

typedef enum {
  USB_AUDIO_SEL_CTRL_UNDEF = 0x00,
  USB_AUDIO_SEL_CTRL_SELECTOR = 0x01,
  USB_AUDIO_SEL_CTRL_LATENCY = 0x02,
} UsbAudioSelCtrlType;

typedef enum {
  USB_AUDIO_FEAT_CTRL_UNDEF = 0x00,
  USB_AUDIO_FEAT_CTRL_MUTE = 0x01,
  USB_AUDIO_FEAT_CTRL_VOLUME = 0x02,
  USB_AUDIO_FEAT_CTRL_BASS = 0x03,
  USB_AUDIO_FEAT_CTRL_MID = 0x04,
  USB_AUDIO_FEAT_CTRL_TREBLE = 0x05,
  USB_AUDIO_FEAT_CTRL_GRAPH_EQ = 0x06,
  USB_AUDIO_FEAT_CTRL_AUTO_GAIN = 0x07,
  USB_AUDIO_FEAT_CTRL_DELAY = 0x08,
  USB_AUDIO_FEAT_CTRL_BASS_BOST = 0x09,
  USB_AUDIO_FEAT_CTRL_LOUDNESS = 0x0A,
  USB_AUDIO_FEAT_CTRL_INPUT_GAIN = 0x0B,
  USB_AUDIO_FEAT_CTRL_INPUT_GAIN_PAD = 0x0C,
  USB_AUDIO_FEAT_CTRL_PHASE_INVERTER = 0x0D,
  USB_AUDIO_FEAT_CTRL_UNDERFLOW = 0x0E,
  USB_AUDIO_FEAT_CTRL_OVERFLOW = 0x0F,
  USB_AUDIO_FEAT_CTRL_LATENCY = 0x10,
} UsbAudioFeatCtrlType;

typedef enum {
  USB_AUDIO_PARA_EQ_CTRL_UNDEF = 0x00,
  USB_AUDIO_PARA_EQ_CTRL_ENABLE = 0x01,
  USB_AUDIO_PARA_EQ_CTRL_CENTERFREQ = 0x02,
  USB_AUDIO_PARA_EQ_CTRL_QFACTOR = 0x03,
  USB_AUDIO_PARA_EQ_CTRL_GAIN = 0x04,
  USB_AUDIO_PARA_EQ_CTRL_UNDEFLOW = 0x05,
  USB_AUDIO_PARA_EQ_CTRL_OVERFLOW = 0x06,
  USB_AUDIO_PARA_EQ_CTRL_LATENCY = 0x07,
} UsbAudioParaEqCtrlType;

typedef enum {
  USB_AUDIO_REVERB_CTRL_UNDEF = 0x00,
  USB_AUDIO_REVERB_CTRL_ENABLE = 0x01,
  USB_AUDIO_REVERB_CTRL_TYPE = 0x02,
  USB_AUDIO_REVERB_CTRL_LEVEL = 0x03,
  USB_AUDIO_REVERB_CTRL_TIME = 0x04,
  USB_AUDIO_REVERB_CTRL_FEEDBACK = 0x05,
  USB_AUDIO_REVERB_CTRL_PREDELAY = 0x06,
  USB_AUDIO_REVERB_CTRL_DENSITY = 0x07,
  USB_AUDIO_REVERB_CTRL_HIFREQ_CUTOFF = 0x08,
  USB_AUDIO_REVERB_CTRL_UNDERFLOW = 0x09,
  USB_AUDIO_REVERB_CTRL_OVERFLOW = 0x0A,
  USB_AUDIO_REVERB_CTRL_LATENCY = 0x0B,
} UsbAudioReverbCtrlType;

typedef enum {
  USB_AUDIO_MOD_DELAY_CTRL_UNDEF = 0x00,
  USB_AUDIO_MOD_DELAY_CTRL_ENABLE = 0x01,
  USB_AUDIO_MOD_DELAY_CTRL_BALANCE = 0x02,
  USB_AUDIO_MOD_DELAY_CTRL_RATE = 0x03,
  USB_AUDIO_MOD_DELAY_CTRL_DEPTH = 0x04,
  USB_AUDIO_MOD_DELAY_CTRL_TIME = 0x05,
  USB_AUDIO_MOD_DELAY_CTRL_FEEDBACK = 0x06,
  USB_AUDIO_MOD_DELAY_CTRL_UNDERFLOW = 0x07,
  USB_AUDIO_MOD_DELAY_CTRL_OVERFLOW = 0x08,
  USB_AUDIO_MOD_DELAY_CTRL_LATENCY = 0x09,
} UsbAudioModDelayCtrlType;

typedef enum {
  USB_AUDIO_DRC_CTRL_UNDEF = 0x00,
  USB_AUDIO_DRC_CTRL_ENABLE = 0x01,
  USB_AUDIO_DRC_CTRL_COMPRESSION_RATE = 0x02,
  USB_AUDIO_DRC_CTRL_MAXAMPL = 0x03,
  USB_AUDIO_DRC_CTRL_THRESHOLD = 0x04,
  USB_AUDIO_DRC_CTRL_ATTACK_TIME = 0x05,
  USB_AUDIO_DRC_CTRL_RELEASE_TIME = 0x06,
  USB_AUDIO_DRC_CTRL_UNDERFLOW = 0x07,
  USB_AUDIO_DRC_CTRL_OVERFLOW = 0x08,
  USB_AUDIO_DRC_CTRL_LATENCY = 0x09,
} UsbAudioDrcCtrlType;

typedef enum {
  USB_AUDIO_UP_DOWN_MIX_CTRL_UNDEF = 0x00,
  USB_AUDIO_UP_DOWN_MIX_CTRL_ENABLE = 0x01,
  USB_AUDIO_UP_DOWN_MIX_CTRL_MODE_SELECT = 0x02,
  USB_AUDIO_UP_DOWN_MIX_CTRL_CLUSTER = 0x03,
  USB_AUDIO_UP_DOWN_MIX_CTRL_UNDERFLOW = 0x04,
  USB_AUDIO_UP_DOWN_MIX_CTRL_OVERFLOW = 0x05,
  USB_AUDIO_UP_DOWN_MIX_CTRL_LATENCY = 0x06,
} UsbAudioUpDownMixCtrlType;

typedef enum {
  USB_AUDIO_DOLBY_PRO_CTRL_UNDEF = 0x00,
  USB_AUDIO_DOLBY_PRO_CTRL_ENABLE = 0x01,
  USB_AUDIO_DOLBY_PRO_CTRL_MODE_SELECT = 0x02,
  USB_AUDIO_DOLBY_PRO_CTRL_CLUSTER = 0x03,
  USB_AUDIO_DOLBY_PRO_CTRL_UNDERFLOW = 0x04,
  USB_AUDIO_DOLBY_PRO_CTRL_OVERFLOW = 0x05,
  USB_AUDIO_DOLBY_PRO_CTRL_LATENCY = 0x06,
} UsbAudioDolbyProCtrlType;

typedef enum {
  USB_AUDIO_STEREO_EXT_CTRL_UNDEF = 0x00,
  USB_AUDIO_STEREO_EXT_CTRL_ENABLE = 0x01,
  USB_AUDIO_STEREO_EXT_CTRL_WIDTH = 0x02,
  USB_AUDIO_STEREO_EXT_CTRL_UNDERFLOW = 0x03,
  USB_AUDIO_STEREO_EXT_CTRL_OVERFLOW = 0x04,
  USB_AUDIO_STEREO_EXT_CTRL_LATENCY = 0x05,
} UsbAudioStereoExtCtrlType;

typedef enum {
  USB_AUDIO_EXT_UNIT_CTRL_UNDEF = 0x00,
  USB_AUDIO_EXT_UNIT_CTRL_ENABLE = 0x01,
  USB_AUDIO_EXT_UNIT_CTRL_CLUSTER = 0x02,
  USB_AUDIO_EXT_UNIT_CTRL_UNDERFLOW = 0x03,
  USB_AUDIO_EXT_UNIT_CTRL_OVERFLOW = 0x04,
  USB_AUDIO_EXT_UNIT_CTRL_LATENCY = 0x05,
} UsbAudioExtUnitCtrlType;

typedef enum {
  USB_AUDIO_STREAM_CTRL_UNDEF = 0x00,
  USB_AUDIO_STREAM_CTRL_ACT_ALT_SETTING = 0x01,
  USB_AUDIO_STREAM_CTRL_VAL_ALT_SETTINGS = 0x02,
  USB_AUDIO_STREAM_CTRL_AUDIO_DATA_FORMAT = 0x03,
} UsbAudioStreamCtrlType;

typedef enum {
  USB_AUDIO_ENCODER_CTRL_UNDEF = 0x00,
  USB_AUDIO_ENCODER_CTRL_BIT_RATE = 0x01,
  USB_AUDIO_ENCODER_CTRL_QUALITY = 0x02,
  USB_AUDIO_ENCODER_CTRL_VBR = 0x03,
  USB_AUDIO_ENCODER_CTRL_TYPE = 0x04,
  USB_AUDIO_ENCODER_CTRL_UNDERFLOW = 0x05,
  USB_AUDIO_ENCODER_CTRL_OVERFLOW = 0x06,
  USB_AUDIO_ENCODER_CTRL_ENCODER_ERR = 0x07,
  USB_AUDIO_ENCODER_CTRL_PARAM1 = 0x08,
  USB_AUDIO_ENCODER_CTRL_PARAM2 = 0x09,
  USB_AUDIO_ENCODER_CTRL_PARAM3 = 0x0A,
  USB_AUDIO_ENCODER_CTRL_PARAM4 = 0x0B,
  USB_AUDIO_ENCODER_CTRL_PARAM5 = 0x0C,
  USB_AUDIO_ENCODER_CTRL_PARAM6 = 0x0D,
  USB_AUDIO_ENCODER_CTRL_PARAM7 = 0x0E,
  USB_AUDIO_ENCODER_CTRL_PARAM8 = 0x0F,
} UsbAudioEncoderCtrlType;

typedef enum {
  USB_AUDIO_MPEG_DECODER_CTRL_UNDEF = 0x00,
  USB_AUDIO_MPEG_DECODER_CTRL_DUAL_CHANNEL = 0x01,
  USB_AUDIO_MPEG_DECODER_CTRL_SECOND_STEREO = 0x02,
  USB_AUDIO_MPEG_DECODER_CTRL_MULTILINGUAL = 0x03,
  USB_AUDIO_MPEG_DECODER_CTRL_DYN_RANGE = 0x04,
  USB_AUDIO_MPEG_DECODER_CTRL_SCALING = 0x05,
  USB_AUDIO_MPEG_DECODER_CTRL_HILO_SCALING = 0x06,
  USB_AUDIO_MPEG_DECODER_CTRL_UNDERFLOW = 0x07,
  USB_AUDIO_MPEG_DECODER_CTRL_OVERFLOW = 0x08,
  USB_AUDIO_MPEG_DECODER_CTRL_DECODER_ERR = 0x09,
} UsbAudioMpegDecoderCtrlType;

typedef enum {
  USB_AUDIO_AC3_DECODER_CTRL_UNDEF = 0x00,
  USB_AUDIO_AC3_DECODER_CTRL_MODE = 0x01,
  USB_AUDIO_AC3_DECODER_CTRL_DYN_RANGE = 0x02,
  USB_AUDIO_AC3_DECODER_CTRL_SCALING = 0x03,
  USB_AUDIO_AC3_DECODER_CTRL_HILO_SCALING = 0x04,
  USB_AUDIO_AC3_DECODER_CTRL_UNDERFLOW = 0x05,
  USB_AUDIO_AC3_DECODER_CTRL_OVERFLOW = 0x06,
  USB_AUDIO_AC3_DECODER_CTRL_DECODER_ERR = 0x07,
} UsbAudioAc3DecoderCtrlType;

typedef enum {
  USB_AUDIO_WMA_DECODER_CTRL_UNDEF = 0x00,
  USB_AUDIO_WMA_DECODER_CTRL_UNDERFLOW = 0x01,
  USB_AUDIO_WMA_DECODER_CTRL_OVERFLOW = 0x02,
  USB_AUDIO_WMA_DECODER_CTRL_DECODER_ERR = 0x03,
} UsbAudioWmaDecoderCtrlType;

typedef enum {
  USB_AUDIO_DTS_DECODER_CTRL_UNDEF = 0x00,
  USB_AUDIO_DTS_DECODER_CTRL_UNDERFLOW = 0x01,
  USB_AUDIO_DTS_DECODER_CTRL_OVERFLOW = 0x02,
  USB_AUDIO_DTS_DECODER_CTRL_DECODER_ERR = 0x03,
} UsbAudioDtsDecoderCtrlType;

typedef enum {
  USB_AUDIO_EPT_CTRL_UNDEF = 0x00,
  USB_AUDIO_EPT_CTRL_PITCH = 0x01,
  USB_AUDIO_EPT_CTRL_DATA_OVERRUN = 0x02,
  USB_AUDIO_EPT_CTRL_DATA_UNDERRUN = 0x03,
} UsbAudioEptCtrlType;

typedef enum {
  USB_AUDIO_CTRL_PROP_NOT_PRESENT = 0b00,
  USB_AUDIO_CTRL_PROP_READ_ONLY = 0b01,
  USB_AUDIO_CTRL_PROP_HOST_PROG = 0b11,
} UsbAudioCtrlPropType;

typedef enum {
  USB_AUDIO_CLK_EXTERNAL = 0,
  USB_AUDIO_CLK_INTERNAL_FIX = 1,
  USB_AUDIO_CLK_INTERNAL_VAR = 2,
  USB_AUDIO_CLK_INTERNAL_PROG = 3,
} UsbAudioClkType;

typedef enum {
  USB_AUDIO_TERM_USB_UNDEF = 0x0100,
  USB_AUDIO_TERM_USB_STREAM = 0x0101,
  USB_AUDIO_TERM_USB_VENDOR = 0x01FF,

  USB_AUDIO_TERM_IN_UNDEF = 0x0200,
  USB_AUDIO_TERM_IN_MIC = 0x0201,
  USB_AUDIO_TERM_IN_DESKTOP_MIC = 0x0202,
  USB_AUDIO_TERM_IN_PERSONAL_MIC = 0x0203,
  USB_AUDIO_TERM_IN_OMNI_MIC = 0x0204,
  USB_AUDIO_TERM_IN_MIC_ARRAY = 0x0205,
  USB_AUDIO_TERM_IN_PROC_MIC_ARRAY = 0x0206,

  USB_AUDIO_TERM_OUT_UNDEF = 0x0300,
  USB_AUDIO_TERM_OUT_SPEAKER = 0x0301,
  USB_AUDIO_TERM_OUT_HEADPHONES = 0x0302,
  USB_AUDIO_TERM_OUT_HMD_AUDIO = 0x0303,
  USB_AUDIO_TERM_OUT_DESKTOP_SPEAKER = 0x0304,
  USB_AUDIO_TERM_OUT_ROOM_SPEAKER = 0x0305,
  USB_AUDIO_TERM_OUT_COMMS_SPEAKER = 0x0306,
  USB_AUDIO_TERM_OUT_LFE_SPEAKER = 0x0307,

  USB_AUDIO_TERM_BIDIR_UNDEF = 0x0400,
  USB_AUDIO_TERM_BIDIR_HANDSET = 0x0401,
  USB_AUDIO_TERM_BIDIR_HEADSET = 0x0402,
  USB_AUDIO_TERM_BIDIR_SPEAKER_PHONE = 0x0403,
  USB_AUDIO_TERM_BIDIR_ECHO_SUPP_SPEAKER_PHONE = 0x0404,
  USB_AUDIO_TERM_BIDIR_ECHO_CANCEL_SPEAKER_PHONE = 0x0405,

  USB_AUDIO_TERM_TELE_UNDEF = 0x0500,
  USB_AUDIO_TERM_TELE_PHONE_LINE = 0x501,
  USB_AUDIO_TERM_TELE_TELEPHONE = 0x0502,
  USB_AUDIO_TERM_TELE_DOWN_LINE_PHONE = 0x0503,

  USB_AUDIO_TERM_EXT_UNDEF = 0x0600,
  USB_AUDIO_TERM_EXT_GEN_ANALOG = 0x0601,
  USB_AUDIO_TERM_EXT_GEN_DIGITAL = 0x0602,
  USB_AUDIO_TERM_EXT_LINE_CONN = 0x0603,
  USB_AUDIO_TERM_EXT_LEGACY_CONN = 0x0604,
  USB_AUDIO_TERM_EXT_SPDIF = 0x0605,
  USB_AUDIO_TERM_EXT_1394_DA = 0x0606,
  USB_AUDIO_TERM_EXT_1394_DV = 0x0607,
  USB_AUDIO_TERM_EXT_ADAT_LIGHTPIPE = 0x608,
  USB_AUDIO_TERM_EXT_TDIF = 0x0609,
  USB_AUDIO_TERM_EXT_MADI = 0x060A,

  USB_AUDIO_TERM_EMB_UNDEF = 0x0700,
  USB_AUDIO_TERM_EMB_LVL_CAL_NOISE_SRC = 0x0701,
  USB_AUDIO_TERM_EMB_EQ_NOISE = 0x0702,
  USB_AUDIO_TERM_EMB_CD_PLAYER = 0x0703,
  USB_AUDIO_TERM_EMB_DAT = 0x0704,
  USB_AUDIO_TERM_EMB_DCC = 0x0705,
  USB_AUDIO_TERM_EMB_COMPRESSED_AUDIO_PLAYER = 0x0706,
  USB_AUDIO_TERM_EMB_ANALOG_TAPE = 0x0707,
  USB_AUDIO_TERM_EMB_PHONOGRAPH = 0x0708,
  USB_AUDIO_TERM_EMB_VCR = 0x0709,
  USB_AUDIO_TERM_EMB_VIDEO_DISC = 0x070A,
  USB_AUDIO_TERM_EMB_DVD = 0x070B,
  USB_AUDIO_TERM_EMB_TV_TUNER = 0x070C,
  USB_AUDIO_TERM_EMB_STALLITE_RCVR = 0x070D,
  USB_AUDIO_TERM_EMB_CABLE_TUNER = 0x070E,
  USB_AUDIO_TERM_EMB_DSS = 0x070F,
  USB_AUDIO_TERM_EMB_RADIO_RCVR = 0x0710,
  USB_AUDIO_TERM_EMB_RADIO_XMTR = 0x0711,
  USB_AUDIO_TERM_EMB_MULTI_TRACK_REC = 0x0712,
  USB_AUDIO_TERM_EMB_SYNTH = 0x0713,
  USB_AUDIO_TERM_EMB_PIANO = 0x0714,
  USB_AUDIO_TERM_EMB_GUITAR = 0x0715,
  USB_AUDIO_TERM_EMB_DRUMS = 0x0716,
  USB_AUDIO_TERM_EMB_OTHER_INSTRUMENT = 0x0717,
} UsbAudioTermType;

typedef enum {
  USB_AUDIO_FORMAT_TYPE_UNDEF = 0x00,
  USB_AUDIO_FORMAT_TYPE_I = 0x01,
  USB_AUDIO_FORMAT_TYPE_II = 0x02,
  USB_AUDIO_FORMAT_TYPE_III = 0x03,
  USB_AUDIO_FORMAT_TYPE_IV = 0x04,
  USB_AUDIO_FORMAT_EXT_TYPE_I = 0x81,
  USB_AUDIO_FORMAT_EXT_TYPE_II = 0x82,
  USB_AUDIO_FORMAT_EXT_TYPE_III = 0x83,
} UsbAudioFormatCatType;

typedef enum {
  USB_AUDIO_LOCK_DELAY_UNITS_UNDEF = 0,
  USB_AUDIO_LOCK_DELAY_UNITS_MS = 1,
  USB_AUDIO_LOCK_DELAY_UNITS_PCM_SAMPLES = 2,
  // 3..255 reserved
} UsbAudioLockDelayUnitsType;

// There's just way to many descriptors in the audio spec. Lots of the core ones were added but if you are gonna do
// something fancy with the encoded/decoded formats you will probably need to add some definitions.

typedef struct __attribute__((packed)) {
  bool bFrontLeft : 1;
  bool bFrontRight : 1;
  bool bFrontCenter : 1;
  bool bLowFreqEffects : 1;
  bool bBackLeft : 1;
  bool bBackRight : 1;
  bool bFrontLeftOfCenter : 1;
  bool bFrontRightOfCenter : 1;
  bool bBackCenter : 1;
  bool bSideLeft : 1;
  bool bSideRight : 1;
  bool bTopCenter : 1;
  bool bTopFrontLeft : 1;
  bool bTopFrontCenter : 1;
  bool bTopFrontRight : 1;
  bool bTopBackLeft : 1;
  bool bTopBackCenter : 1;
  bool bTopBackRight : 1;
  bool bTopFrontLeftOfCenter : 1;
  bool bTopFrontRightOfCenter : 1;
  bool bLeftLowFreqEffects : 1;
  bool bRightLowFreqEffects : 1;
  bool bTopSideLeft : 1;
  bool bTopSideRight : 1;
  bool bBottomCenter : 1;
  bool bBackLeftOfCenter : 1;
  bool bBackRightOfCenter : 1;
  int _reserved : 4;
  bool bRawData : 1;
} UsbAudioChannelMapType;

typedef struct __attribute__((packed)) {
  u8 u8NrChannels;
  UsbAudioChannelMapType stChannelConfig;
  u8 u8ChannelNames;
} UsbAudioChannelClusterDesc;

typedef struct __attribute__((packed)) {
  UsbDescHeaderType stUsbHeader;
  UsbAudioCtrlDescSubType eSubtype : 8;
} UsbAudioDescHeaderType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  UsbVersionType stAdcVersion;
  UsbAudioFunctionCategoryType eCategory : 8;
  u16 u16TotalLength;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eLatency : 2;
    int _reserved : 6;
  } stControls;
} UsbAudioCtrlHeaderDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8ClkId;
  struct __attribute__((packed)) {
    UsbAudioClkType eClkType : 2;
    bool bSofSynced : 1;
    int _reserved : 5;
  } stAttributes;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eClkFreq : 2;
    UsbAudioCtrlPropType eClkValid : 2;
    int _reserved : 4;
  } stControls;
  u8 u8AssocTerminal;
  u8 u8NameStr;
} UsbAudioCtrlClkSrcDescType;

#define USB_AUDIO_CLK_SEL_DESC_TYPE(N)                                                                                 \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8ClkId;                                                                                                        \
    u8 u8NrInPins;                                                                                                     \
    u8 au8SrcIds[N];                                                                                                   \
    struct __attribute__((packed)) {                                                                                   \
      UsbAudioCtrlPropType eClkSel : 2;                                                                                \
      int _reserved : 6;                                                                                               \
    } stControls;                                                                                                      \
    u8 u8NameStr;                                                                                                      \
  }

typedef USB_AUDIO_CLK_SEL_DESC_TYPE(2) UsbAudioClkSel2DescType;
typedef USB_AUDIO_CLK_SEL_DESC_TYPE(3) UsbAudioClkSel3DescType;
typedef USB_AUDIO_CLK_SEL_DESC_TYPE(4) UsbAudioClkSel4DescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8ClkId;
  u8 u8SrcId;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eNumerator : 2;
    UsbAudioCtrlPropType eDenominator : 2;
    int _reserved : 4;
  } stControls;
} UsbAudioClkMulDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8TermId;
  UsbAudioTermType eTermType : 16;
  u8 u8AssocTerm;
  u8 u8ClkSrcId;
  UsbAudioChannelClusterDesc stChannels;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eCopyProtect : 2;
    UsbAudioCtrlPropType eConnector : 2;
    UsbAudioCtrlPropType eOverload : 2;
    UsbAudioCtrlPropType eCluster : 2;
    UsbAudioCtrlPropType eUnderflow : 2;
    UsbAudioCtrlPropType eOverflow : 2;
    int _reserved : 4;
  } stControls;
  u8 u8Name;
} UsbAudioInTermDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8TermId;
  UsbAudioTermType eTermType : 16;
  u8 u8AssocTerm;
  u8 u8SrcId;
  u8 u8ClkSrcId;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eCopyProtect : 2;
    UsbAudioCtrlPropType eConnector : 2;
    UsbAudioCtrlPropType eOverload : 2;
    UsbAudioCtrlPropType eUnderflow : 2;
    UsbAudioCtrlPropType eOverflow : 2;
    int _reserved : 6;
  } stControls;
  u8 u8Name;
} UsbAudioOutTermDescType;

#define USB_AUDIO_MIXER_DESC_TYPE(N_IN, N_OUT)                                                                         \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    u8 u8NumInPins;                                                                                                    \
    u8 au8SrcIds[N_IN];                                                                                                \
    UsbAudioChannelClusterDesc stChannels;                                                                             \
    u8 au8MixerCtrls[((N_IN * N_OUT) + 7) / 8];                                                                        \
    struct __attribute__((packed)) {                                                                                   \
      UsbAudioCtrlPropType eCluster : 2;                                                                               \
      UsbAudioCtrlPropType eUnderflow : 2;                                                                             \
      UsbAudioCtrlPropType eOverflow : 2;                                                                              \
      int _reserved : 2;                                                                                               \
    } stControls;                                                                                                      \
    u8 u8Name;                                                                                                         \
  }

#define USB_AUDIO_SEL_DESC_TYPE(N)                                                                                     \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    u8 u8NrInPins;                                                                                                     \
    u8 au8SrcIds[N];                                                                                                   \
    struct __attribute__((packed)) {                                                                                   \
      UsbAudioCtrlPropType eSelector : 2;                                                                              \
      int _reserved : 6;                                                                                               \
    } stControls;                                                                                                      \
    u8 u8Name;                                                                                                         \
  }

typedef USB_AUDIO_SEL_DESC_TYPE(2) UsbAudioSel2DescType;
typedef USB_AUDIO_SEL_DESC_TYPE(3) UsbAudioSel3DescType;
typedef USB_AUDIO_SEL_DESC_TYPE(4) UsbAudioSel4DescType;

#define USB_AUDIO_FEAT_DESC_TYPE(N)                                                                                    \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    u8 u8SrcId;                                                                                                        \
    struct __attribute__((packed)) {                                                                                   \
      UsbAudioCtrlPropType eMute : 2;                                                                                  \
      UsbAudioCtrlPropType eVolume : 2;                                                                                \
      UsbAudioCtrlPropType eBass : 2;                                                                                  \
      UsbAudioCtrlPropType eMid : 2;                                                                                   \
      UsbAudioCtrlPropType eTreble : 2;                                                                                \
      UsbAudioCtrlPropType eGraphEq : 2;                                                                               \
      UsbAudioCtrlPropType eAgc : 2;                                                                                   \
      UsbAudioCtrlPropType eDelay : 2;                                                                                 \
      UsbAudioCtrlPropType eBassBoost : 2;                                                                             \
      UsbAudioCtrlPropType eLoudness : 2;                                                                              \
      UsbAudioCtrlPropType eInputGain : 2;                                                                             \
      UsbAudioCtrlPropType eInputGainPad : 2;                                                                          \
      UsbAudioCtrlPropType ePhaseInverter : 2;                                                                         \
      UsbAudioCtrlPropType eUnderflow : 2;                                                                             \
      UsbAudioCtrlPropType eOverflow : 2;                                                                              \
      int _reserved : 2;                                                                                               \
    } astControls[N + 1];                                                                                              \
    u8 u8Name;                                                                                                         \
  }

typedef USB_AUDIO_FEAT_DESC_TYPE(1) UsbAudioMonoFeatDescType;
typedef USB_AUDIO_FEAT_DESC_TYPE(2) UsbAudioStereoFeatDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8UnitId;
  u8 u8SrcId;
  u8 u8ClkInId;
  u8 u8ClkOutId;
  u8 u8Name;
} UsbAudioSampleRateConverterDescType;

#define USB_AUDIO_EFFECT_DESC_TYPE(N, ...)                                                                             \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    UsbAudioEffectUnitType eType : 16;                                                                                 \
    u8 u8SrcId;                                                                                                        \
    struct __attribute__((packed)) __VA_ARGS__ stControls[N + 1];                                                      \
    u8 u8Name;                                                                                                         \
  }

#define USB_AUDIO_PARA_EQ_DESC_TYPE(N)                                                                                 \
  USB_AUDIO_EFFECT_DESC_TYPE(N, {                                                                                      \
    UsbAudioCtrlPropType eEnable : 2;                                                                                  \
    UsbAudioCtrlPropType eCenterFreq : 2;                                                                              \
    UsbAudioCtrlPropType eQFactor : 2;                                                                                 \
    UsbAudioCtrlPropType eGain : 2;                                                                                    \
    UsbAudioCtrlPropType eUnderflow : 2;                                                                               \
    UsbAudioCtrlPropType eOverflow : 2;                                                                                \
    int _reserved : 20;                                                                                                \
  })

#define USB_AUDIO_REVERB_DESC_TYPE(N)                                                                                  \
  USB_AUDIO_EFFECT_DESC_TYPE(N, {                                                                                      \
    UsbAudioCtrlPropType eEnable : 2;                                                                                  \
    UsbAudioCtrlPropType eType : 2;                                                                                    \
    UsbAudioCtrlPropType eLevel : 2;                                                                                   \
    UsbAudioCtrlPropType eTime : 2;                                                                                    \
    UsbAudioCtrlPropType eDelayFeedback : 2;                                                                           \
    UsbAudioCtrlPropType ePreDelay : 2;                                                                                \
    UsbAudioCtrlPropType eDensity : 2;                                                                                 \
    UsbAudioCtrlPropType eHiFreqRolloff : 2;                                                                           \
    UsbAudioCtrlPropType eUnderflow : 2;                                                                               \
    UsbAudioCtrlPropType eOverflow : 2;                                                                                \
    int _reserved : 12;                                                                                                \
  })

#define USB_AUDIO_MOD_DELAY_DESC_TYPE(N)                                                                               \
  USB_AUDIO_EFFECT_DESC_TYPE(N, {                                                                                      \
    UsbAudioCtrlPropType eEnable : 2;                                                                                  \
    UsbAudioCtrlPropType eBalance : 2;                                                                                 \
    UsbAudioCtrlPropType eRate : 2;                                                                                    \
    UsbAudioCtrlPropType eDepth : 2;                                                                                   \
    UsbAudioCtrlPropType eTime : 2;                                                                                    \
    UsbAudioCtrlPropType eFeedback : 2;                                                                                \
    UsbAudioCtrlPropType eUnderflow : 2;                                                                               \
    UsbAudioCtrlPropType eOverflow : 2;                                                                                \
    int _reserved : 16;                                                                                                \
  })

#define USB_AUDIO_DRC_DESC_TYPE(N)                                                                                     \
  USB_AUDIO_EFFECT_DESC_TYPE(N, {                                                                                      \
    UsbAudioCtrlPropType eEnable : 2;                                                                                  \
    UsbAudioCtrlPropType eCompressionRatio : 2;                                                                        \
    UsbAudioCtrlPropType eMaxAmpl : 2;                                                                                 \
    UsbAudioCtrlPropType eThreshold : 2;                                                                               \
    UsbAudioCtrlPropType eAttackTime : 2;                                                                              \
    UsbAudioCtrlPropType eReleaseTime : 2;                                                                             \
    UsbAudioCtrlPropType eUnderflow : 2;                                                                               \
    UsbAudioCtrlPropType eOverflow : 2;                                                                                \
    int _reserved : 16;                                                                                                \
  })

#define USB_AUDIO_PROCESSING_DESC_HEADER_TYPE(N, ...)                                                                  \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    UsbAudioProcessingUnitType eType : 16;                                                                             \
    u8 u8NrInPins;                                                                                                     \
    u8 au8SrcIds[N];                                                                                                   \
    UsbAudioChannelClusterDesc stChannels;                                                                             \
    struct __attribute__((packed)) __VA_ARGS__ stControls;                                                             \
    u8 u8Name;                                                                                                         \
  }

#define USB_AUDIO_UP_DOWN_MIX_DESC_TYPE(N_CHAN, N_MODES)                                                               \
  struct __attribute__((packed)) {                                                                                     \
    USB_AUDIO_PROCESSING_DESC_HEADER_TYPE(N_CHAN, {                                                                    \
      UsbAudioCtrlPropType eEnable : 2;                                                                                \
      UsbAudioCtrlPropType eModeSel : 2;                                                                               \
      UsbAudioCtrlPropType eCluster : 2;                                                                               \
      UsbAudioCtrlPropType eUnderflow : 2;                                                                             \
      UsbAudioCtrlPropType eOverflow : 2;                                                                              \
      int _reserved : 6;                                                                                               \
    })                                                                                                                 \
    stCommon;                                                                                                          \
    u8 u8NrModes;                                                                                                      \
    UsbAudioChannelMapType astModes[N_MODES];                                                                          \
  }

#define USB_AUDIO_DOLBY_PRO_DESC_TYPE(N_CHAN, N_MODES)                                                                 \
  struct __attribute__((packed)) {                                                                                     \
    USB_AUDIO_PROCESSING_DESC_HEADER_TYPE(N_CHAN, {                                                                    \
      UsbAudioCtrlPropType eEnable : 2;                                                                                \
      UsbAudioCtrlPropType eModeSel : 2;                                                                               \
      UsbAudioCtrlPropType eCluster : 2;                                                                               \
      UsbAudioCtrlPropType eUnderflow : 2;                                                                             \
      UsbAudioCtrlPropType eOverflow : 2;                                                                              \
      int _reserved : 6;                                                                                               \
    })                                                                                                                 \
    stCommon;                                                                                                          \
    u8 u8NrModes;                                                                                                      \
    UsbAudioChannelMapType astModes[N_MODES];                                                                          \
  }

typedef USB_AUDIO_PROCESSING_DESC_HEADER_TYPE(1, {
  UsbAudioCtrlPropType eEnable : 2;
  UsbAudioCtrlPropType eWidth : 2;
  UsbAudioCtrlPropType eCluster : 2;
  UsbAudioCtrlPropType eUnderflow : 2;
  UsbAudioCtrlPropType eOverflow : 2;
  int _reserved : 6;
}) UsbAudioStereoExtDescType;

#define USB_AUDIO_EXT_UNIT_DESC_TYPE(N)                                                                                \
  struct __attribute__((packed)) {                                                                                     \
    UsbAudioDescHeaderType stHeader;                                                                                   \
    u8 u8UnitId;                                                                                                       \
    u16 u16ExtCode;                                                                                                    \
    u8 u8NrInPins;                                                                                                     \
    u8 au8SourceIds[N];                                                                                                \
    UsbAudioChannelClusterDesc stChannels;                                                                             \
    struct __attribute__((packed)) {                                                                                   \
      UsbAudioCtrlPropType eEnable : 2;                                                                                \
      UsbAudioCtrlPropType eCluster : 2;                                                                               \
      UsbAudioCtrlPropType eUnderflow : 2;                                                                             \
      UsbAudioCtrlPropType eOverflow : 2;                                                                              \
    } stControls;                                                                                                      \
    u8 u8Name;                                                                                                         \
  }

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8TerminalLink;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType eActiveAlt : 2;
    UsbAudioCtrlPropType eValidAlts : 2;
    int _reserved : 4;
  } stControls;
  UsbAudioFormatCatType eFormatType : 8;
  union __attribute__((packed)) {
    struct __attribute__((packed)) {
      bool bPcm : 1;
      bool bPcm8 : 1;
      bool bIeeeFloat : 1;
      bool bAlaw : 1;
      bool bMulaw : 1;
      int _reserved : 26;
      bool bRawData : 1;
    } stTypeI;

    struct __attribute__((packed)) {
      bool bMpeg : 1;
      bool bAc3 : 1;
      bool bWma : 1;
      bool bDts : 1;
      int _reserved : 27;
      bool bRawData : 1;
    } stTypeII;

    struct __attribute__((packed)) {
      bool bIec61937_Ac3 : 1;
      bool bIec61937_Mpeg1Layer1 : 1;
      bool bIec61937_Mpeg1Layer23_Mpeg2NoExt : 1;
      bool bIec61937_Mpeg2Ext : 1;
      bool bIec61937_Mpeg2AacAdts : 1;
      bool bIec61937_Mpeg2Layer1Ls : 1;
      bool bIec61937_Mpeg2Layer23Ls : 1;
      bool bIec61937_DtsI : 1;
      bool bIec61937_DtsII : 1;
      bool bIec61937_DtsIII : 1;
      bool bIec61937_Atrac : 1;
      bool bIec61937_Atrac23 : 1;
      bool bWma : 1;
      int _reserved : 19;
    } stTypeIII;

    struct __attribute__((packed)) {
      bool bPcm : 1;
      bool bPcm8 : 1;
      bool bIeeeFloat : 1;
      bool bAlaw : 1;
      bool bMulaw : 1;
      bool bMpeg : 1;
      bool bAc3 : 1;
      bool bWma : 1;
      bool bIec61937_Ac3 : 1;
      bool bIec61937_Mpeg1Layer1 : 1;
      bool bIec61937_Mpeg1Layer23_Mpeg2NoExt : 1;
      bool bIec61937_Mpeg2Ext : 1;
      bool bIec61937_Mpeg2AacAdts : 1;
      bool bIec61937_Mpeg2Layer1Ls : 1;
      bool bIec61937_Mpeg2Layer23Ls : 1;
      bool bIec61937_DtsI : 1;
      bool bIec61937_DtsII : 1;
      bool bIec61937_DtsIII : 1;
      bool bIec61937_Atrac : 1;
      bool bIec61937_Atrac23 : 1;
      bool bTypeIIIWma : 1;
      bool bIec60958_Pcm : 1;
      int _reserved : 10;
    } stTypeIV;
  } uFormats;
  UsbAudioChannelClusterDesc stChannels;
} UsbAudioStrIfaceDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  u8 u8FormatType;
  u8 u8SubslotSize;
  u8 u8BitResolution;
} UsbAudioTypeIFormatDescType;

typedef struct __attribute__((packed)) {
  UsbAudioDescHeaderType stHeader;
  struct __attribute__((packed)) {
    bool bMaxPacketsOnly : 1;
    int _reserved : 7;
  } stAttributes;
  struct __attribute__((packed)) {
    UsbAudioCtrlPropType ePitch : 2;
    UsbAudioCtrlPropType eDataOverrun : 2;
    UsbAudioCtrlPropType eDataUnderrun : 2;
    int _reserved : 2;
  } stControls;
  UsbAudioLockDelayUnitsType eLockDelayUnits : 8;
  u16 u16LockDelay;
} UsbAudioIsoEptDescType;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define USB_CLASS_AUDIO 0x01

#define USB_SUBCLASS_AUDIO_UNDEF 0x00
#define USB_SUBCLASS_AUDIO_CONTROL 0x01
#define USB_SUBCLASS_AUDIO_STREAMING 0x02
#define USB_SUBCLASS_AUDIO_MIDISTREAMING 0x03

#define USB_PROTO_AUDIO_UNDEF 0x00
#define USB_PROTO_AUDIO_VERSION_02_00 0x20

#define USB_AUDIO_CLASS_CTRL                                                                                           \
  (UsbClassType) { USB_CLASS_AUDIO, USB_SUBCLASS_AUDIO_CONTROL, USB_PROTO_AUDIO_VERSION_02_00 }

#endif /* USB_AUDIO_H */
