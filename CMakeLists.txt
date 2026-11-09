cmake_minimum_required(VERSION 3.24)
project(Composer LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Uses the wxWidgets tree pointed to by the WXWIN environment variable.
set(wxWidgets_ROOT_DIR "$ENV{WXWIN}" CACHE PATH "wxWidgets root")
set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/vc_x64_lib" CACHE PATH "wxWidgets libs")
find_package(wxWidgets 3.3 REQUIRED COMPONENTS aui core net base)
include(${wxWidgets_USE_FILE})

# The parts of Steinberg's VST3 SDK needed to host plugins (see build.bat for the checkout).
set(VST3_SDK "${CMAKE_SOURCE_DIR}/third_party/vst3sdk")
add_library(vst3host STATIC
    ${VST3_SDK}/pluginterfaces/base/conststringtable.cpp
    ${VST3_SDK}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK}/pluginterfaces/base/ustring.cpp
    ${VST3_SDK}/base/source/baseiids.cpp
    ${VST3_SDK}/base/source/fbuffer.cpp
    ${VST3_SDK}/base/source/fdebug.cpp
    ${VST3_SDK}/base/source/fobject.cpp
    ${VST3_SDK}/base/source/fstreamer.cpp
    ${VST3_SDK}/base/source/fstring.cpp
    ${VST3_SDK}/base/source/updatehandler.cpp
    ${VST3_SDK}/base/thread/source/fcondition.cpp
    ${VST3_SDK}/base/thread/source/flock.cpp
    ${VST3_SDK}/public.sdk/source/common/commonstringconvert.cpp
    ${VST3_SDK}/public.sdk/source/common/memorystream.cpp
    ${VST3_SDK}/public.sdk/source/common/threadchecker_win32.cpp
    ${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/eventlist.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/module.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/processdata.cpp
)
target_include_directories(vst3host PUBLIC ${VST3_SDK})
target_compile_definitions(vst3host PUBLIC RELEASE=1 UNICODE _UNICODE NOMINMAX)
if(MSVC)
    target_compile_options(vst3host PRIVATE /W0 /utf-8)
endif()

add_executable(Composer WIN32
    src/App.cpp
    src/MainFrame.cpp
    src/ScoreCanvas.cpp
    src/PalettePanel.cpp
    src/TransportBar.cpp
    src/TimelinePanel.cpp
    src/DirectorPanel.cpp
    src/InstrumentsWindow.cpp
    src/Project.cpp
    src/ProjectDialogs.cpp
    src/director/ScoreText.cpp
    src/director/DirectorClient.cpp
    src/director/DirectorSession.cpp
    src/director/DirectorSettings.cpp
    src/audio/AudioEngine.cpp
    src/audio/PlaceholderSynth.cpp
    src/audio/Vst3Host.cpp
    src/Composer.rc
)

target_include_directories(Composer PRIVATE src third_party)
target_link_libraries(Composer PRIVATE ${wxWidgets_LIBRARIES} vst3host avrt ole32 shell32 winhttp crypt32 advapi32)
target_compile_definitions(Composer PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(Composer PRIVATE /W3 /utf-8 /permissive-)
    # The manifest comes from wx.rc (see src/Composer.rc).
    target_link_options(Composer PRIVATE /MANIFEST:NO)
endif()
