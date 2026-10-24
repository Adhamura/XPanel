cmake_minimum_required(VERSION 3.21)
project(XPanel VERSION 1.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Network DBus Svg)
if(NOT WIN32)
    find_package(LayerShellQt QUIET)
    # libxcb: marks the panel as a gamescope overlay in Steam Game Mode (optional)
    option(XPANEL_WITH_XCB "Use libxcb for the Steam Game Mode (gamescope) overlay when found" ON)
    if(XPANEL_WITH_XCB)
        find_path(XPANEL_XCB_INCLUDE_DIR xcb/xcb.h)
        find_library(XPANEL_XCB_LIBRARY xcb)
    endif()
endif()

include(GNUInstallDirs)
option(XPANEL_BUILD_TOOLS "Build the developer tools (xpanel-mapper, xpanel-input-probe)" ON)
# Translations (i18n/*.ts) need Qt LinguistTools. Without it the app still builds, in English only.
# Release builds and CI turn this on so a missing tool cannot ship an English-only package unnoticed.
option(XPANEL_REQUIRE_TRANSLATIONS "Fail when Qt LinguistTools (lrelease, lupdate) is missing" OFF)
# Release archives: no ahead-of-time QML compilation. AOT code links Qt's private API, which
# only works with the exact Qt minor version it was built with; without it one binary runs
# on every Qt >= the build version (the QML is compiled at run time instead).
option(XPANEL_PORTABLE "Build binaries that run on newer Qt 6 versions too (release archives)" OFF)
set(XPANEL_QML_OPTIONS)
if(XPANEL_PORTABLE)
    set(XPANEL_QML_OPTIONS NO_CACHEGEN)
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(XPanel
    src/main.cpp
    src/DeviceClient.h src/DeviceClient.cpp
    src/SystemControl.h src/SystemControl.cpp
    src/PanelController.h src/PanelController.cpp
    src/Config.h src/Config.cpp
    src/Proc.h
    src/DeviceInfo.h src/DeviceInfo.cpp
    src/AudioControl.h src/AudioControl.cpp
    src/NetworkControl.h src/NetworkControl.cpp
    src/UpdateCenter.h src/UpdateCenter.cpp
    src/AppUpdate.h src/AppUpdate.cpp src/UpdateLogic.h
    src/GameLibrary.h src/GameLibrary.cpp
    src/GameWatcher.h src/GameWatcher.cpp
    src/ProfileManager.h src/ProfileManager.cpp
    src/ProfileRules.h src/ProfileRules.cpp
    src/MediaGallery.h src/MediaGallery.cpp
    src/DesktopTweaks.h src/DesktopTweaks.cpp
    src/Desktop.h src/Desktop.cpp
    src/Translations.h src/Translations.cpp src/LanguageMatch.h
    src/Feedback.h src/Feedback.cpp src/FeedbackReport.h src/FeedbackReport.cpp
    src/QrMatrix.h src/QrMatrix.cpp
    third_party/qrcodegen/qrcodegen.hpp third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(XPanel PRIVATE third_party/qrcodegen)

file(GLOB XPANEL_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/icons/*.svg)

if(QT_KNOWN_POLICY_QTP0004) # Qt 6.8+
    qt_policy(SET QTP0004 NEW)
endif()
qt_add_qml_module(XPanel
    URI XPanel
    VERSION 1.0
    NO_RESOURCE_TARGET_PATH
    ${XPANEL_QML_OPTIONS}
    QML_FILES
        qml/Main.qml
        qml/Tile.qml
        qml/VSlider.qml
        qml/Chooser.qml
        qml/TilePopups.qml
        qml/EditPanel.qml
        qml/StatsStrip.qml
        qml/MonitorOsd.qml
        qml/Theme.js
        qml/ControlDefaults.js
        qml/app/AppWindow.qml qml/app/CcPage.qml qml/app/CcButton.qml qml/app/CcSwitch.qml
        qml/app/CcSlider.qml qml/app/CcChoice.qml qml/app/CcDropdown.qml qml/app/CcScrollBar.qml qml/app/ControllerMap.qml qml/app/ControllerMapHotspots.js qml/app/CcRow.qml qml/app/CcSection.qml qml/app/CcField.qml
        qml/app/LibraryPage.qml qml/app/ControlPage.qml qml/app/GalleryPage.qml qml/app/UserPage.qml
        qml/app/SettingsPage.qml qml/app/SetGeneral.qml qml/app/SetPerformance.qml qml/app/SetUpdate.qml
        qml/app/SetCalibration.qml qml/app/SetLighting.qml qml/app/SetAudio.qml
        qml/app/SetDisplay.qml qml/app/SetConnection.qml qml/app/SetShortcuts.qml qml/app/SetFeedback.qml
        qml/app/SetBattery.qml qml/app/FanCurveEditor.qml qml/app/CcSwatches.qml
    RESOURCES ${XPANEL_ICONS}
)

# Translations: i18n/xpanel_<code>.ts (edit with Qt Linguist) are compiled into the executable as
# resources ":/i18n/xpanel_<code>.qm", so packaging needs no extra files. To add a language: add its
# .ts file here and to kLanguages in src/Translations.cpp, then run the "update_translations" target.
# Maintainers: `cmake --build <dir> --target update_translations` extracts new strings into the .ts files.
if(XPANEL_REQUIRE_TRANSLATIONS)
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
else()
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
endif()
if(Qt6LinguistTools_FOUND)
    set(XPANEL_TS_FILES)
    foreach(lang de es fr it ja ko pt_BR ru zh_CN)
        list(APPEND XPANEL_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/xpanel_${lang}.ts)
    endforeach()
    qt_add_translations(XPanel TS_FILES ${XPANEL_TS_FILES} RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -no-obsolete -locations relative)
    message(STATUS "XPanel: translations enabled (${CMAKE_CURRENT_SOURCE_DIR}/i18n, target update_translations)")
else()
    message(STATUS "XPanel: Qt LinguistTools not found, building without translations (English only)")
endif()

set_target_properties(XPanel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(XPanel PRIVATE XPANEL_VERSION="${PROJECT_VERSION}")
target_link_libraries(XPanel PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Network Qt6::DBus Qt6::Svg)
if(WIN32)
    # Windows (experimental): no console window, app icon, global hotkeys (user32)
    set_target_properties(XPanel PROPERTIES WIN32_EXECUTABLE ON CXX_STANDARD 20) # C++/WinRT coroutines need C++20 on newer MSVC
    target_sources(XPanel PRIVATE packaging/windows/xpanel.rc)
    target_compile_definitions(XPanel PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_sources(XPanel PRIVATE src/UpdateCenterWin.cpp src/BrightnessWin.cpp)
    target_link_libraries(XPanel PRIVATE user32 setupapi shell32 dxva2 wbemuuid ole32 oleaut32)
    target_link_libraries(XPanel PRIVATE gdi32) # gamma ramp (software dimming)
    target_link_libraries(XPanel PRIVATE advapi32) # registry reads in DeviceInfo
    target_sources(XPanel PRIVATE src/NetworkControlWin.cpp src/AudioControlWin.cpp) # WLAN API + WinRT Bluetooth, Core Audio
    target_link_libraries(XPanel PRIVATE wlanapi windowsapp)
    target_link_libraries(XPanel PRIVATE hid xinput secur32) # controllers, user name
    target_link_libraries(XPanel PRIVATE uuid) # CLSID_ShellLink (Game Library Start Menu scan)
    target_sources(XPanel PRIVATE src/SystemControlWin.cpp src/SystemControlWin.h) # volume, radios, power, display, sensors
    target_link_libraries(XPanel PRIVATE pdh powrprof windowsapp)
    target_sources(XPanel PRIVATE src/AmdAdlWin.cpp src/AmdAdlWin.h) # AMD GPU temp/power via ADL (loaded at run time)
    target_include_directories(XPanel PRIVATE third_party/adl/include)
endif()

if(LayerShellQt_FOUND)
    target_link_libraries(XPanel PRIVATE LayerShellQt::Interface)
    target_compile_definitions(XPanel PRIVATE XPANEL_HAVE_LAYERSHELL=1)
    # setWantsToBeOnActiveScreen() is new in Plasma 6.4's layer-shell-qt (Debian 13 has 6.3)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES LayerShellQt::Interface Qt6::Gui)
    check_cxx_source_compiles("#include <LayerShellQt/Window>
        int main() { LayerShellQt::Window *w = nullptr; if (w) w->setWantsToBeOnActiveScreen(true); return 0; }"
        XPANEL_LAYERSHELL_ACTIVE_SCREEN)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(XPANEL_LAYERSHELL_ACTIVE_SCREEN)
        target_compile_definitions(XPanel PRIVATE XPANEL_LAYERSHELL_ACTIVE_SCREEN=1)
    endif()
    message(STATUS "XPanel: LayerShellQt found, Wayland overlay enabled")
else()
    message(STATUS "XPanel: LayerShellQt not found, X11/xdg-shell fallback only")
endif()

if(XPANEL_WITH_XCB AND XPANEL_XCB_INCLUDE_DIR AND XPANEL_XCB_LIBRARY)
    target_include_directories(XPanel PRIVATE ${XPANEL_XCB_INCLUDE_DIR})
    target_link_libraries(XPanel PRIVATE ${XPANEL_XCB_LIBRARY})
    target_compile_definitions(XPanel PRIVATE XPANEL_HAVE_XCB=1)
    message(STATUS "XPanel: libxcb found, Steam Game Mode overlay enabled")
elseif(NOT WIN32)
    message(STATUS "XPanel: no libxcb (headers not found or XPANEL_WITH_XCB=OFF), no Steam Game Mode overlay")
endif()

# Hardware helper: TDP, fan curve, charge limit, lighting (no GUI libraries).
# Linux: root service with the input layer. Windows: ASUS ACPI + HID backend in the user session.
if(WIN32)
    add_executable(xpanel-daemon daemon/main.cpp daemon/Hardware.h daemon/Model.h daemon/HardwareWin.cpp
        daemon/InputLayerStub.h daemon/InputLayerWin.cpp daemon/InputLayerWinHelpers.h
        daemon/AllyMcu.h daemon/AllyMcuWin.h daemon/AllyMcuWin.cpp
        daemon/GyroWin.cpp daemon/GyroWin.h daemon/GyroMath.h daemon/GyroMath.cpp third_party/vigem/src/ViGEmClient.cpp)
    target_include_directories(xpanel-daemon PRIVATE third_party/vigem/include)
    set_target_properties(xpanel-daemon PROPERTIES WIN32_EXECUTABLE ON CXX_STANDARD 20) # C++/WinRT (GyroWin.cpp)
    target_sources(xpanel-daemon PRIVATE packaging/windows/xpanel.rc)
    target_compile_definitions(xpanel-daemon PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xpanel-daemon PRIVATE hid setupapi xinput windowsapp)
else()
    add_executable(xpanel-daemon daemon/main.cpp daemon/Hardware.h daemon/Model.h daemon/Hardware.cpp
        daemon/InputLayer.h daemon/InputLayer.cpp daemon/Imu.h daemon/Imu.cpp daemon/GyroMath.h daemon/GyroMath.cpp)
endif()
set_target_properties(xpanel-daemon PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin AUTOMOC ON)
target_compile_definitions(xpanel-daemon PRIVATE XPANEL_VERSION="${PROJECT_VERSION}")
target_link_libraries(xpanel-daemon PRIVATE Qt6::Core Qt6::Network)

# Developer tool: map Ally X input codes to physical controls (tools/mapper)
if(XPANEL_BUILD_TOOLS AND NOT WIN32)
    add_executable(xpanel-input-probe tools/mapper/probe_main.cpp)
    set_target_properties(xpanel-input-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    qt_add_executable(xpanel-mapper tools/mapper/mapper_main.cpp)
    qt_add_resources(xpanel-mapper mapper_qml PREFIX /mapper BASE tools/mapper
        FILES tools/mapper/Mapper.qml tools/mapper/AllyView.qml)
    set_target_properties(xpanel-mapper PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    target_link_libraries(xpanel-mapper PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
endif()

# Unit tests (ctest); never installed
option(XPANEL_BUILD_TESTS "Build the unit tests" ON)
if(XPANEL_BUILD_TESTS AND NOT WIN32)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install: binaries, desktop entry, icon, systemd unit (ExecStart follows the install prefix).
# Packagers: cmake -DCMAKE_INSTALL_PREFIX=/usr ... && DESTDIR=pkg cmake --install build
install(TARGETS XPanel xpanel-daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32) # Windows packaging: packaging/windows/build-release.ps1 (windeployqt + Inno Setup)
    return()
endif()
install(FILES packaging/xpanel.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES qml/icons/app.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME xpanel.svg)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS daemon/xpanel-daemon.service)
file(READ daemon/xpanel-daemon.service XPANEL_UNIT)
string(REPLACE "/usr/local/bin/" "${CMAKE_INSTALL_FULL_BINDIR}/" XPANEL_UNIT "${XPANEL_UNIT}")
file(WRITE ${CMAKE_BINARY_DIR}/xpanel-daemon.service "${XPANEL_UNIT}")
install(FILES ${CMAKE_BINARY_DIR}/xpanel-daemon.service DESTINATION lib/systemd/system)
install(FILES packaging/xpanel.sysusers DESTINATION lib/sysusers.d RENAME xpanel.conf)
# Steam Game Mode autostart (gamescope sessions skip XDG autostart). Not enabled by default:
#   systemctl --user enable xpanel.service      (or --global for every user)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS packaging/xpanel.service)
file(READ packaging/xpanel.service XPANEL_USER_UNIT)
string(REPLACE "/usr/local/bin/" "${CMAKE_INSTALL_FULL_BINDIR}/" XPANEL_USER_UNIT "${XPANEL_USER_UNIT}")
file(WRITE ${CMAKE_BINARY_DIR}/xpanel.service "${XPANEL_USER_UNIT}")
install(FILES ${CMAKE_BINARY_DIR}/xpanel.service DESTINATION lib/systemd/user)
