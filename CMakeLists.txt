cmake_minimum_required(VERSION 3.16)

project(Sunny VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    LinguistTools
    Network
    WebSockets
)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    LinguistTools
    Network
    WebSockets
)


if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS UiPlugin)
endif()

# KF6 deps required by AeroQt::AeroQt
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6FrameworkIntegration REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Completion REQUIRED)

find_package(AeroQt REQUIRED)
find_package(Qt6Keychain REQUIRED)

set(TS_FILES Sunny_en_US.ts)

set(PROJECT_SOURCES
        main.cpp
        main_window.cpp
        main_window.hpp
        main_window.ui
        api_client.hpp api_client.cpp
        websocket.hpp websocket.cpp
        account_wizard.hpp account_wizard.cpp
        account_wizard.ui
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Sunny
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        account_manager.hpp account_manager.cpp
)
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET Sunny APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(Sunny SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(Sunny
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(Sunny PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
    AeroQt::AeroQt
    Qt6Keychain::Qt6Keychain
)

# Needed so the UIC-generated ui_account_wizard.h can include account_wizard.hpp
# (used for the LoginOptionsPage / TerminalWizardPage promoted pages).
target_include_directories(Sunny PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Sunny)
endif()
set_target_properties(Sunny PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Sunny
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Sunny)
endif()
