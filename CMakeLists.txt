cmake_minimum_required(VERSION 3.24)
project(Composer LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Uses the wxWidgets tree pointed to by the WXWIN environment variable.
set(wxWidgets_ROOT_DIR "$ENV{WXWIN}" CACHE PATH "wxWidgets root")
set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/vc_x64_lib" CACHE PATH "wxWidgets libs")
find_package(wxWidgets 3.3 REQUIRED COMPONENTS aui core base)
include(${wxWidgets_USE_FILE})

add_executable(Composer WIN32
    src/App.cpp
    src/MainFrame.cpp
    src/ScoreCanvas.cpp
    src/PalettePanel.cpp
    src/TransportBar.cpp
    src/TimelinePanel.cpp
    src/DirectorPanel.cpp
    src/Composer.rc
)

target_link_libraries(Composer PRIVATE ${wxWidgets_LIBRARIES})
target_compile_definitions(Composer PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(Composer PRIVATE /W3 /utf-8 /permissive-)
    # The manifest comes from wx.rc (see src/Composer.rc).
    target_link_options(Composer PRIVATE /MANIFEST:NO)
endif()
