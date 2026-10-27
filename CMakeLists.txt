cmake_minimum_required(VERSION 3.21)
project(FloodMonitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5 or newer is required: QLocationPermission / requestPermission and QQmlApplicationEngine::loadFromModule.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Quick WebView Positioning)
qt_standard_project_setup(REQUIRES 6.5)

# Generate qmldir files for QML subdirectories (silences the QTP0004 warning on newer Qt).
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()
if(QT_KNOWN_POLICY_QTP0002)
    qt_policy(SET QTP0002 NEW)
endif()

# 1) Original flood logic: plain C++, no Qt, so it can be tested on its own.
add_library(floodcore STATIC backend/FloodCore.cpp backend/FloodCore.h)
target_include_directories(floodcore PUBLIC backend)

# 2) The app
qt_add_executable(FloodMonitor
    main.cpp
    backend/FloodService.cpp   backend/FloodService.h
    backend/LocationHelper.cpp backend/LocationHelper.h
    backend/LocalApiServer.cpp backend/LocalApiServer.h
)

qt_add_qml_module(FloodMonitor
    URI FloodApp
    VERSION 1.0
    QML_FILES qml/Main.qml
)

# HTML/CSS/JS are compiled into the app and served from ":/frontend/..."
qt_add_resources(FloodMonitor "frontend"
    PREFIX "/"
    FILES
        frontend/index.html
        frontend/css/style.css
        frontend/js/backend.js
        frontend/js/settings.js
        frontend/js/weather.js
        frontend/js/app.js
        frontend/js/export.js
)

target_link_libraries(FloodMonitor PRIVATE
    floodcore Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::WebView Qt6::Positioning)

if(ANDROID)
    # Folder holding AndroidManifest.xml and res/ (see README for the manifest notes)
    set_property(TARGET FloodMonitor PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif()

# Tests for the original logic (desktop builds only)
if(NOT ANDROID)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE floodcore)
    add_test(NAME flood_core COMMAND test_core)
endif()

install(TARGETS FloodMonitor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(ANDROID)
    target_link_libraries(FloodMonitor PRIVATE c++_shared)
endif()