cmake_minimum_required(VERSION 3.25)
project(TetoricaVST3AudioCheck LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
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
  "${VST3_SDK}/public.sdk/source/common/memorystream.cpp"
  "${VST3_SDK}/public.sdk/source/vst/hosting/plugprovider.cpp")
target_compile_features(tetorica_vst3_audio_check PRIVATE cxx_std_17)
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
