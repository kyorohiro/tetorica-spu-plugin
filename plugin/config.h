// Tetorica instrument configuration; based on the iPlug2OOS template.
#define PLUG_NAME "TetoricaSPU"
#define PLUG_MFR "Tetorica"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'Tspu'
#define PLUG_MFR_ID 'Tetr'
#define PLUG_URL_STR "https://github.com/kyorohiro/tetorica-spu-plugin"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Tetorica contributors"
#define PLUG_CLASS_NAME TetoricaSPU

#define BUNDLE_NAME "TetoricaSPU"
#define BUNDLE_MFR "Tetorica"
#define BUNDLE_DOMAIN "com"
#define SHARED_RESOURCES_SUBPATH "TetoricaSPU"

#define PLUG_CHANNEL_IO "0-2"
#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 720
#define PLUG_HEIGHT 360
#define PLUG_FPS 30
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY TetoricaSPU_Entry
#define AUV2_ENTRY_STR "TetoricaSPU_Entry"
#define AUV2_FACTORY TetoricaSPU_Factory
#define AUV2_VIEW_CLASS TetoricaSPU_View
#define AUV2_VIEW_CLASS_STR "TetoricaSPU_View"

#define VST3_SUBCATEGORY "Instrument|Synth"
#ifndef VST3_NUM_MIDI_IN_CHANS
#define VST3_NUM_MIDI_IN_CHANS 16
#endif
#ifndef VST3_NUM_CC_CHANS
#define VST3_NUM_CC_CHANS 16
#endif
#define CLAP_MANUAL_URL "https://github.com/kyorohiro/tetorica-spu-plugin"
#define CLAP_SUPPORT_URL "https://github.com/kyorohiro/tetorica-spu-plugin/issues"
#define CLAP_DESCRIPTION "A PS1 SPU instrument using Tetorica's native sound core"
#define CLAP_FEATURES "instrument", "sampler"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
