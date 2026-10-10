cmake_minimum_required(VERSION 3.21)
project(XPanel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Network DBus Svg)
find_package(LayerShellQt QUIET)

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
    src/GameLibrary.h src/GameLibrary.cpp
    src/MediaGallery.h src/MediaGallery.cpp
    src/DesktopTweaks.h src/DesktopTweaks.cpp
)

file(GLOB XPANEL_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/icons/*.svg)

qt_policy(SET QTP0004 NEW)
qt_add_qml_module(XPanel
    URI XPanel
    VERSION 1.0
    NO_RESOURCE_TARGET_PATH
    QML_FILES
        qml/Main.qml
        qml/Tile.qml
        qml/VSlider.qml
        qml/Chooser.qml
        qml/EditPanel.qml
        qml/StatsStrip.qml
        qml/MonitorOsd.qml
        qml/Theme.js
        qml/ControlDefaults.js
        qml/app/AppWindow.qml qml/app/CcPage.qml qml/app/CcButton.qml qml/app/CcSwitch.qml
        qml/app/CcSlider.qml qml/app/CcChoice.qml qml/app/CcRow.qml qml/app/CcSection.qml qml/app/CcField.qml
        qml/app/LibraryPage.qml qml/app/ControlPage.qml qml/app/GalleryPage.qml qml/app/UserPage.qml
        qml/app/SettingsPage.qml qml/app/SetGeneral.qml qml/app/SetPerformance.qml qml/app/SetUpdate.qml
        qml/app/SetCalibration.qml qml/app/SetLighting.qml qml/app/SetAudio.qml
        qml/app/SetDisplay.qml qml/app/SetConnection.qml qml/app/SetShortcuts.qml qml/app/SetFeedback.qml
        qml/app/SetBattery.qml
    RESOURCES ${XPANEL_ICONS}
)

set_target_properties(XPanel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(XPanel PRIVATE XPANEL_VERSION="${PROJECT_VERSION}")
target_link_libraries(XPanel PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Network Qt6::DBus Qt6::Svg)

if(LayerShellQt_FOUND)
    target_link_libraries(XPanel PRIVATE LayerShellQt::Interface)
    target_compile_definitions(XPanel PRIVATE XPANEL_HAVE_LAYERSHELL=1)
    message(STATUS "XPanel: LayerShellQt found, Wayland overlay enabled")
else()
    message(STATUS "XPanel: LayerShellQt not found, X11/xdg-shell fallback only")
endif()

# Root helper: TDP, fan curve, charge limit, lighting (no GUI libraries)
add_executable(xpanel-daemon daemon/main.cpp daemon/Hardware.h daemon/Hardware.cpp
    daemon/InputLayer.h daemon/InputLayer.cpp)
set_target_properties(xpanel-daemon PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin AUTOMOC ON)
target_link_libraries(xpanel-daemon PRIVATE Qt6::Core Qt6::Network)

# Developer tool: map Ally X input codes to physical controls (tools/mapper)
add_executable(xpanel-input-probe tools/mapper/probe_main.cpp)
set_target_properties(xpanel-input-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
qt_add_executable(xpanel-mapper tools/mapper/mapper_main.cpp)
qt_add_resources(xpanel-mapper mapper_qml PREFIX /mapper BASE tools/mapper
    FILES tools/mapper/Mapper.qml tools/mapper/AllyView.qml)
set_target_properties(xpanel-mapper PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(xpanel-mapper PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

install(TARGETS XPanel xpanel-daemon RUNTIME DESTINATION bin)
