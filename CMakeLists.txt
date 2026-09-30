cmake_minimum_required(VERSION 3.13.0)

project(Qv2ray)

# ==================================================================================
# Load Custom CMake Modules
# ==================================================================================
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(cmake/Qv2rayCMakePolicy.cmake)
include(cmake/Qv2rayVersion.cmake)
include(cmake/Qv2rayOptions.cmake)
include(cmake/Qv2rayFunctions.cmake)

# ==================================================================================
# Qv2ray Build Info
# ==================================================================================
set(QV2RAY_VERSION_MAJOR 2)
set(QV2RAY_VERSION_MINOR 7)
set(QV2RAY_VERSION_PATCH 0)
set(QV2RAY_VERSION_SUFFIX "-z2")
set(QV2RAY_BUILD_VERSION 7002)

set(QV2RAY_VERSION_STRING "${QV2RAY_VERSION_MAJOR}.${QV2RAY_VERSION_MINOR}.${QV2RAY_VERSION_PATCH}${QV2RAY_VERSION_SUFFIX}")

message(STATUS "")
message(STATUS "Qv2ray Version: ${QV2RAY_VERSION_STRING}")
message(STATUS "Qv2ray Build Version: ${QV2RAY_BUILD_VERSION}")

# ==================================================================================
# Licence
# ==================================================================================
message(STATUS "|-------------------------------------------------|")
message(STATUS "| Qv2ray-Z, a community-maintained Xray Qt client.|")
message(STATUS "| Licenced under GPLv3                            |")
message(STATUS "|                                                 |")
message(STATUS "| You may only use this program to the extent     |")
message(STATUS "| permitted by local law.                         |")
message(STATUS "|                                                 |")
message(STATUS "| See: https://www.gnu.org/licenses/gpl-3.0.html  |")
message(STATUS "|-------------------------------------------------|")
message(STATUS "| Project: https://github.com/Zhehao-w/Qv2ray-Z   |")
message(STATUS "| Welcome to contribute!                          |")
message(STATUS "|-------------------------------------------------|")
message(STATUS "")

# ==================================================================================
# Default Build Type
# ==================================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

# ==================================================================================
# Compiler Options
# ==================================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ==================================================================================
# Common Options
# ==================================================================================
option(QV2RAY_AUTO_DEPLOY "Automatically deploy required files after build." ON)
option(QV2RAY_USE_V5_CORE "Use V2Ray core v5 configuration." ON)
option(QV2RAY_HAS_BUILTIN_PLUGINS "Build builtin plugins." ON)
option(QV2RAY_HAS_BUILTIN_THEMES "Build builtin themes." ON)
option(QV2RAY_HAS_SINGLEAPPLICATION "Use SingleApplication." ON)
option(QV2RAY_QT6 "Build against Qt 6." OFF)
option(QV2RAY_USE_QML "Use QML UI." OFF)
option(QV2RAY_USE_QWIDGET "Use QWidget UI." ON)
option(QV2RAY_DISABLE_AUTO_UPDATE "Disable built-in update checks." ON)
option(QV2RAY_EMBED_TRANSLATIONS "Embed translations into executable." OFF)

if(QV2RAY_QT6)
    cmake_policy(SET CMP0072 NEW)
    set(QV_QT_MAJOR_VERSION 6)
    set(QV_QT_MINOR_VERSION 0)
    set(QV_QT_LIBNAME Qt6)
    add_definitions(-DQV2RAY_QT6=1)
else()
    set(QV_QT_MAJOR_VERSION 5)
    set(QV_QT_MINOR_VERSION 11)
    set(QV_QT_LIBNAME Qt5)
endif()

# ==================================================================================
# Default Core/Assets Path
# ==================================================================================
if(QV2RAY_DEFAULT_VCORE_PATH AND NOT QV2RAY_DEFAULT_VCORE_PATH STREQUAL "unset")
    add_definitions(-DQV2RAY_DEFAULT_VCORE_PATH="${QV2RAY_DEFAULT_VCORE_PATH}")
endif()
if(QV2RAY_DEFAULT_VASSETS_PATH AND NOT QV2RAY_DEFAULT_VASSETS_PATH STREQUAL "unset")
    add_definitions(-DQV2RAY_DEFAULT_VASSETS_PATH="${QV2RAY_DEFAULT_VASSETS_PATH}")
endif()

# ==================================================================================
# Embed Translations, Translation Search Path
# ==================================================================================
set(QV2RAY_TRANSLATION_PATH "unset" CACHE STRING "Qv2ray translations path")
if(QV2RAY_TRANSLATION_PATH AND NOT QV2RAY_TRANSLATION_PATH STREQUAL "unset")
    add_definitions(-DQV2RAY_TRANSLATION_PATH="${QV2RAY_TRANSLATION_PATH}")
endif()
if(QV2RAY_EMBED_TRANSLATIONS)
    add_definitions(-DQV2RAY_EMBED_TRANSLATIONS)
    configure_file(translations/translations.qrc ${CMAKE_BINARY_DIR} COPYONLY)
    set(QV2RAY_EMBED_TRANSLATION_QRC ${CMAKE_BINARY_DIR}/translations.qrc)
endif()

# ==================================================================================
# Disable Auto Update
# ==================================================================================
if(QV2RAY_DISABLE_AUTO_UPDATE)
    add_definitions(-DDISABLE_AUTO_UPDATE)
endif()

# ==================================================================================
# Qv2ray Build Info
# ==================================================================================
if(QV2RAY_BUILD_INFO)
    set(_QV2RAY_BUILD_INFO_STR_ "${QV2RAY_BUILD_INFO}")
elseif(DEFINED ENV{_QV2RAY_BUILD_INFO_})
    set(_QV2RAY_BUILD_INFO_STR_ "$ENV{_QV2RAY_BUILD_INFO_}")
else()
    set(_QV2RAY_BUILD_INFO_STR_ "Qv2ray-Z from manual build")
endif()

if(QV2RAY_BUILD_EXTRA_INFO)
    set(_QV2RAY_BUILD_EXTRA_INFO_STR_ "${QV2RAY_BUILD_EXTRA_INFO}")
elseif(DEFINED ENV{_QV2RAY_BUILD_EXTRA_INFO_})
    set(_QV2RAY_BUILD_EXTRA_INFO_STR_ "$ENV{_QV2RAY_BUILD_EXTRA_INFO_}")
else()
    set(_QV2RAY_BUILD_EXTRA_INFO_STR_ "${QV2RAY_VERSION_STRING}:${QV2RAY_BUILD_VERSION}")
endif()

set(QV2RAY_BUILD_INFO ${_QV2RAY_BUILD_INFO_STR_})
set(QV2RAY_BUILD_EXTRA_INFO ${_QV2RAY_BUILD_EXTRA_INFO_STR_})

add_definitions(-D_QV2RAY_BUILD_INFO_STR_="${_QV2RAY_BUILD_INFO_STR_}")
add_definitions(-D_QV2RAY_BUILD_EXTRA_INFO_STR_="${_QV2RAY_BUILD_EXTRA_INFO_STR_}")
message("Qv2ray Version: ${_QV2RAY_BUILD_INFO_STR_} - ${_QV2RAY_BUILD_EXTRA_INFO_STR_}")

# ==================================================================================
# 3rdparty Sources
# ==================================================================================
include(cmake/libuv.cmake)
include(cmake/libcurl.cmake)
include(cmake/libsemver.cmake)
include(cmake/backend.cmake)
include(3rdparty/QJsonStruct/QJsonStruct.cmake)

# ==================================================================================
# Qv2ray Base, Qt Libraries, Qv2ray GUI Libraries, libThreads
# ==================================================================================
find_package(Threads REQUIRED)
find_package(${QV_QT_LIBNAME} ${QV_QT_MAJOR_VERSION}.${QV_QT_MINOR_VERSION} COMPONENTS Core Network REQUIRED)
list(APPEND QV2RAY_QT_LIBS ${QV_QT_LIBNAME}::Core ${QV_QT_LIBNAME}::Network)

cmake_policy(SET CMP0071 NEW)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(QV2RAY_USE_QML)
    find_package(${QV_QT_LIBNAME} ${QV_QT_MAJOR_VERSION}.${QV_QT_MINOR_VERSION} COMPONENTS Qml Quick Widgets Svg QuickControls2 Gui REQUIRED)
    list(APPEND QV2RAY_QT_LIBS
        ${QV_QT_LIBNAME}::Quick
        ${QV_QT_LIBNAME}::Qml
        ${QV_QT_LIBNAME}::Widgets
        ${QV_QT_LIBNAME}::Svg
        ${QV_QT_LIBNAME}::QuickControls2
        ${QV_QT_LIBNAME}::Gui)
    set(_QV2RAY_HAS_GUI_INTERNAL_ ON)
elseif(QV2RAY_USE_QWIDGET)
    find_package(${QV_QT_LIBNAME} ${QV_QT_MAJOR_VERSION}.${QV_QT_MINOR_VERSION} COMPONENTS Widgets Svg Gui REQUIRED)
    list(APPEND QV2RAY_QT_LIBS
        ${QV_QT_LIBNAME}::Widgets
        ${QV_QT_LIBNAME}::Svg
        ${QV_QT_LIBNAME}::Gui)
    set(_QV2RAY_HAS_GUI_INTERNAL_ ON)
else()
endif()

# Qv2ray baselib sources
include(cmake/components/qv2ray-base.cmake)
include(src/plugin-interface/QvPluginInterface.cmake)

if(QV2RAY_HAS_SINGLEAPPLICATION)
    include(cmake/singleapplication.cmake)
else()
    add_definitions(-DQV2RAY_NO_SINGLEAPPLICATON)
endif()

add_library(qv2ray_baselib STATIC
    ${QV2RAY_BASE_SOURCES}
    ${SINGLEAPPLICATION_SOURCES}
    ${QVPLUGIN_INTERFACE_HEADERS}
    ${LIBSEMVER_SOURCES}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${API_GRPC_SRCS}
    ${API_PROTO_SRCS}
    )

target_link_libraries(qv2ray_baselib
    ${QV2RAY_QT_LIBS}
    ${QV2RAY_BACKEND_LIBRARY}
    ${Protobuf_LIBRARIES}
    ${LibUV_LIBRARIES}
    ${CURL_LIBRARIES}
    ${SINGLEAPPLICATION_LIBRARY}
    Threads::Threads
    )

target_include_directories(qv2ray_baselib PUBLIC
    ${CMAKE_BINARY_DIR}
    ${QV2RAY_BASEDIR_COMPONENTS}
    ${LibUV_INCLUDE_DIR}
    ${SINGLEAPPLICATION_DIR}
    ${CURL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Protobuf_INCLUDE_DIRS}
    )

if(QV2RAY_USE_V5_CORE)
    message("-- Use V2Ray Core V5")
    target_compile_definitions(qv2ray_baselib PUBLIC QV2RAY_USE_V5_CORE)
endif()

# ==================================================================================
# Plugin Interfaces and Implementations
# ==================================================================================
if(QV2RAY_HAS_BUILTIN_PLUGINS)
    add_definitions(-DQV2RAY_HAS_BUILTIN_PLUGINS)
    include(src/plugins/protocols/QvPlugin-BuiltinProtocolSupport.cmake)
    include(src/plugins/subscription-adapters/QvPlugin-BuiltinSubscriptionAdapters.cmake)
endif()

# ==================================================================================
# Themes
# ==================================================================================
if(QV2RAY_HAS_BUILTIN_THEMES)
    include(src/plugins/theme/QvPlugin-BuiltinThemeSupport.cmake)
endif()

# ==================================================================================
# Main Executable
# ==================================================================================
include(cmake/components/qv2ray-gui.cmake)

if(QV2RAY_USE_QML)
    qt_add_resources(QV2RAY_RESOURCES resources.qrc resources.new.qrc)
else()
    qt5_add_resources(QV2RAY_RESOURCES resources.qrc resources.new.qrc)
endif()

add_executable(qv2ray
    src/main.cpp
    ${QV2RAY_GUI_SOURCES}
    ${QV2RAY_RESOURCES}
    ${QV2RAY_EMBED_TRANSLATION_QRC}
    )

target_link_libraries(qv2ray
    qv2ray_baselib
    ${QV2RAY_EXEC_LINK_LIBS}
    )

target_include_directories(qv2ray PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

# ==================================================================================
# Tests
# ==================================================================================
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

# ==================================================================================
# Install
# ==================================================================================
include(cmake/install.cmake)
