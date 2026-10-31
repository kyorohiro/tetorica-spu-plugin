# Out-of-source layout follows iPlug2OOS TemplateProject, pinned in vendor/.
iplug_add_plugin(TetoricaSPU
  SOURCES
    TetoricaSPU.cpp
    InstrumentUI.cpp
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
