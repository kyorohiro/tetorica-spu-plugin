cmake_minimum_required(VERSION 3.25)
project(TetoricaVST3AudioCheck LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(SMTG_USE_STATIC_CRT ON CACHE BOOL "Match the plugin CRT" FORCE)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
endif()

get_filename_component(TETORICA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(VST3_SDK "${TETORICA_ROOT}/vendor/vst3sdk")
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
add_subdirectory("${VST3_SDK}" sdk EXCLUDE_FROM_ALL)

add_executable(tetorica_vst3_audio_check
  main.cpp
  "${TETORICA_ROOT}/src/InstrumentState.cpp"
  "${TETORICA_ROOT}/src/SpuWaveform.cpp"
  "${TETORICA_ROOT}/src/SpuMorph.cpp"
  "${TETORICA_ROOT}/src/SpuAdpcm.cpp"
  "${TETORICA_ROOT}/src/WebInstrument.cpp"
  "${TETORICA_ROOT}/src/SpuExportSource.cpp"
  "${TETORICA_ROOT}/src/WebInstrumentExport.cpp"
  "${TETORICA_ROOT}/src/WavImport.cpp"
  "${TETORICA_ROOT}/src/AdpcmPlayback.cpp"
  "${TETORICA_ROOT}/src/SoftwarePcm.cpp"
  "${TETORICA_ROOT}/src/VabBank.cpp"
  "${VST3_SDK}/public.sdk/source/common/memorystream.cpp"
  "${VST3_SDK}/public.sdk/source/vst/hosting/plugprovider.cpp")
target_compile_features(tetorica_vst3_audio_check PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties("${TETORICA_ROOT}/src/SpuWaveform.cpp" "${TETORICA_ROOT}/src/SpuMorph.cpp" "${TETORICA_ROOT}/src/WebInstrument.cpp" "${TETORICA_ROOT}/src/WavImport.cpp"
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
elseif(MSVC)
  set_source_files_properties("${TETORICA_ROOT}/src/SpuWaveform.cpp" "${TETORICA_ROOT}/src/SpuMorph.cpp" "${TETORICA_ROOT}/src/WebInstrument.cpp" "${TETORICA_ROOT}/src/WavImport.cpp"
    PROPERTIES COMPILE_OPTIONS "/fp:strict")
endif()
target_include_directories(tetorica_vst3_audio_check PRIVATE "${TETORICA_ROOT}/src")
target_include_directories(tetorica_vst3_audio_check SYSTEM PRIVATE
  "${TETORICA_ROOT}/vendor/iPlug2OOS/iPlug2/Dependencies/Extras")
target_compile_definitions(tetorica_vst3_audio_check PRIVATE VAB_FIXTURE_DIR="${TETORICA_ROOT}/tests/fixtures/vab")
target_compile_definitions(tetorica_vst3_audio_check PRIVATE WEB_FIXTURE_DIR="${TETORICA_ROOT}/tests/fixtures/web")
target_link_libraries(tetorica_vst3_audio_check PRIVATE sdk_hosting)
if(APPLE)
  target_sources(tetorica_vst3_audio_check PRIVATE
    "${VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm")
  set_source_files_properties(
    "${VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  target_link_libraries(tetorica_vst3_audio_check PRIVATE "${COCOA_FRAMEWORK}")
elseif(WIN32)
  target_sources(tetorica_vst3_audio_check PRIVATE
    "${VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp")
else()
  target_sources(tetorica_vst3_audio_check PRIVATE
    "${VST3_SDK}/public.sdk/source/vst/hosting/module_linux.cpp")
  target_link_libraries(tetorica_vst3_audio_check PRIVATE ${CMAKE_DL_LIBS})
endif()
