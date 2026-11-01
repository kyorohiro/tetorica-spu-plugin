# Out-of-source layout follows iPlug2OOS TemplateProject, pinned in vendor/.
iplug_add_plugin(TetoricaSPU
  SOURCES
    TetoricaSPU.cpp
    InstrumentUI.cpp
    MidiAdapters.cpp
    InstrumentState.cpp
    StateAdapters.cpp
    TetoricaSPU.h
    config.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
    resources/THIRD_PARTY_NOTICES.txt
  FORMATS
    APP AU VST3
  LINK
    tetorica_spu_instrument
)

# The framework's separate translation units do not include plugin/config.h.
# Keep their MIDI bus/mapping/controller registry identical to our wrapper.
target_compile_definitions(TetoricaSPU-vst3 PRIVATE
  VST3_NUM_MIDI_IN_CHANS=16
  VST3_NUM_CC_CHANS=16)

if(WIN32)
  # iPlug2 automatically attaches main.rc to APP only. A VST3 must carry its
  # own TTF resource too; loose resource files are not searched by IGraphicsWin.
  target_sources(TetoricaSPU-vst3 PRIVATE resources/fonts.rc)
  set_source_files_properties(resources/fonts.rc PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
endif()
