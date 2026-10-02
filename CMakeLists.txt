cmake_minimum_required(VERSION 3.10.1)

file(STRINGS "${CMAKE_SOURCE_DIR}/makespec/VERSION" QV2RAY_VERSION)
file(STRINGS "${CMAKE_SOURCE_DIR}/makespec/BUILDVERSION" QV2RAY_BUILD_VERSION)

set(QV2RAY_VERSION_STRING "${QV2RAY_VERSION}")
project(qv2ray)

macro(QVLOG MSG)
    set(QV2RAY_BUILD_STATS "${QV2RAY_BUILD_STATS}${MSG}: ${${MSG}}\r\n")
endmacro()

set(VERSION_LIST ${QV2RAY_VERSION})
string(REPLACE "." ";" VERSION_LIST ${VERSION_LIST})
separate_arguments(VERSION_LIST)

list(GET VERSION_LIST 0 CMAKE_PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 CMAKE_PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 CPACK_PACKAGE_VERSION_PATCH)

add_definitions(-DQV2RAY_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR})
add_definitions(-DQV2RAY_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR})
add_definitions(-DQV2RAY_VERSION_BUGFIX=${CPACK_PACKAGE_VERSION_PATCH})
add_definitions(-DQV2RAY_VERSION_BUILD=${QV2RAY_BUILD_VERSION})
add_definitions(-DQV2RAY_VERSION_STRING="${QV2RAY_VERSION_STRING}")

set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS true)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qv2ray-Z is maintained as a Windows x64 / MSVC QWidget application. Linux
# remains buildable only as a diagnostic target used by manual CI.
if(ANDROID OR APPLE)
    message(FATAL_ERROR "Qv2ray-Z supports Windows x64 only; Linux is retained only for diagnostic builds.")
endif()

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "Qv2ray-Z Windows builds require MSVC.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Qv2ray-Z Windows builds require x64.")
    endif()

    set(CMAKE_CXX_EXTENSIONS OFF)
    include(cmake/versioninfo/generate_product_version.cmake)
    generate_product_version(
        QV2RAY_RC
        NAME               "Qv2ray-Z"
        BUNDLE             "Qv2ray-Z"
        ICON               "${CMAKE_SOURCE_DIR}/assets/icons/qv2ray.ico"
        VERSION_MAJOR      ${CMAKE_PROJECT_VERSION_MAJOR}
        VERSION_MINOR      ${CMAKE_PROJECT_VERSION_MINOR}
        VERSION_PATCH      ${CPACK_PACKAGE_VERSION_PATCH}
        VERSION_REVISION   ${QV2RAY_BUILD_VERSION}
        COMPANY_NAME       "Qv2ray-Z community; based on Qv2ray"
        COMPANY_COPYRIGHT  "Copyright (c) 2019-2021 Qv2ray Development Group; Qv2ray-Z contributors"
        ORIGINAL_FILENAME  "qv2ray.exe"
        INTERNAL_NAME      "qv2ray"
        FILE_DESCRIPTION   "Qv2ray-Z Main Application"
        )
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)
    add_compile_options("/utf-8")
    add_compile_options("/std:c++17")
    add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
    set(GUI_TYPE WIN32)
    set(QV2RAY_PLATFORM_LIBS_PREFIX ${CMAKE_SOURCE_DIR}/libs/x64-windows/)
    include(cmake/platforms/prefixes.cmake)
elseif(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "Unsupported Qv2ray-Z build platform.")
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.17.0")
    cmake_policy(SET CMP0100 NEW)
endif()

message(" ")
message("Qv2ray Version: ${QV2RAY_VERSION_STRING}")
message("Qv2ray Build Version: ${QV2RAY_BUILD_VERSION}")
message("|-------------------------------------------------|")
message("| Qv2ray-Z, a community-maintained Xray Qt client.|")
message("| Licenced under GPLv3                            |")
message("|                                                 |")
message("| You may only use this program to the extent     |")
message("| permitted by local law.                         |")
message("|                                                 |")
message("| See: https://www.gnu.org/licenses/gpl-3.0.html  |")
message("|-------------------------------------------------|")
message("| Project: https://github.com/Zhehao-w/Qv2ray-Z   |")
message("| Welcome to contribute!                          |")
message("|-------------------------------------------------|")
message(" ")

# ==================================================================================
# Qv2ray compile arguments
# ==================================================================================
option(QV2RAY_AUTO_DEPLOY "Automatically run deploy command after build" ON)
QVLOG(QV2RAY_AUTO_DEPLOY)

option(BUILD_TESTING "Build Tests" OFF)
QVLOG(BUILD_TESTING)

set(QV2RAY_DEFAULT_VASSETS_PATH "unset" CACHE STRING "Default Xray assets path")
QVLOG(QV2RAY_DEFAULT_VASSETS_PATH)
set(QV2RAY_DEFAULT_VCORE_PATH "unset" CACHE STRING "Default Xray core path")
QVLOG(QV2RAY_DEFAULT_VCORE_PATH)

option(QV2RAY_USE_V5_CORE "Use current Xray/V2Ray v5 API layout" ON)
QVLOG(QV2RAY_USE_V5_CORE)

# Maintained frontend/tooling choices are fixed instead of exposing dead build
# combinations (Android, QML, CLI, Qt6, MinGW, x86, external providers).
set(QV2RAY_USE_QWIDGET ON)
set(QV2RAY_SINGLEAPPLICATION_PROVIDER "module")
set(QV2RAY_QNODEEDITOR_PROVIDER "module")
set(QV_QT_MAJOR_VERSION 5)
set(QV_QT_MINOR_VERSION 11)
set(QV_QT_LIBNAME Qt5)

# The in-app updater is retired in every maintained build. Keep the legacy
# feature define so the existing Preferences UI cannot expose inert settings.
add_definitions(-DDISABLE_AUTO_UPDATE)

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
# Build info
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
# Third-party and generated sources
# ==================================================================================
include(cmake/libuv.cmake)
include(cmake/libcurl.cmake)
include(cmake/backend.cmake)
include(3rdparty/QJsonStruct/QJsonStruct.cmake)

find_package(Threads REQUIRED)
find_package(Qt5 ${QV_QT_MAJOR_VERSION}.${QV_QT_MINOR_VERSION} COMPONENTS Core Network Widgets Svg Gui REQUIRED)
list(APPEND QV2RAY_QT_LIBS Qt5::Core Qt5::Network Qt5::Widgets Qt5::Svg Qt5::Gui)

cmake_policy(SET CMP0071 NEW)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ==================================================================================
# Base library
# ==================================================================================
include(cmake/components/qv2ray-base.cmake)
include(src/plugin-interface/QvPluginInterface.cmake)
include(src/plugin-interface/QvGUIPluginInterface.cmake)
include(cmake/singleapplication.cmake)

add_library(qv2ray_baselib STATIC
    ${QV2RAY_BASE_SOURCES}
    ${SINGLEAPPLICATION_SOURCES}
    ${QVPLUGIN_INTERFACE_HEADERS}
    ${QVGUIPLUGIN_INTERFACE_HEADERS}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${API_GRPC_SRCS}
    ${API_PROTO_SRCS}
    )

target_link_libraries(qv2ray_baselib
    ${QV2RAY_QT_LIBS}
    ${QV2RAY_BACKEND_LIBRARY}
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
    message("-- Use V2Ray/Xray Core V5 API layout")
    target_compile_definitions(qv2ray_baselib PUBLIC QV2RAY_USE_V5_CORE)
endif()

# First-party protocol and subscription implementations are linked into the
# process. They no longer create or install native plugin DLLs.
include(src/plugins/protocols/QvPlugin-BuiltinProtocolSupport.cmake)
include(src/plugins/subscription-adapters/QvPlugin-BuiltinSubscriptionAdapters.cmake)

# ==================================================================================
# QWidget frontend
# ==================================================================================
add_definitions(-DQV2RAY_GUI -DQV2RAY_GUI_QWIDGETS)
include(cmake/qrencode.cmake)
include(cmake/components/qv2ray-ui.cmake)
list(APPEND QV2RAY_UI_SOURCES ${QV2RAY_UI_COMMON_SOURCES})

include(cmake/components/qv2ray-ui-widget.cmake)
list(APPEND QV2RAY_UI_SOURCES ${QV2RAY_UI_WIDGET_SOURCES})

include(cmake/qnodeeditor.cmake)
list(APPEND QV2RAY_QRC_RESOURCES ${QNODEEDITOR_QRC_RESOURCES})

include(cmake/components/qv2ray-platform.cmake)

list(APPEND QV2RAY_QRC_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources.qrc
    ${CMAKE_SOURCE_DIR}/resources.new.qrc
    )

set(QV2RAY_FULL_SOURCES
    ${QV2RAY_RC}
    ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
    ${QV2RAY_UI_SOURCES}
    ${QV2RAY_PLATFORM_SOURCES}
    ${QV2RAY_QRC_RESOURCES}
    )

QVLOG(QV2RAY_QRC_RESOURCES)
add_executable(qv2ray ${GUI_TYPE} ${QV2RAY_FULL_SOURCES})

target_include_directories(qv2ray PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
    ${QNODEEDITOR_INCLUDE_PATH}
    ${Protobuf_INCLUDE_DIRS}
    )

target_link_libraries(qv2ray PUBLIC
    qv2ray_baselib
    ${QNODEEDITOR_LIBRARY}
    ${QV2RAY_PLATFORM_LIBS}
    ${QV2RAY_QRENCODE_LIBRARY}
    )

# ==================================================================================
# Unit tests
# ==================================================================================
if(BUILD_TESTING)
    include(CTest)
    add_subdirectory(test)
endif()

# ==================================================================================
# Installation / diagnostic platform packaging
# ==================================================================================
if(UNIX AND NOT APPLE AND NOT WIN32)
    include(cmake/platforms/linux.cmake)
elseif(WIN32)
    include(cmake/platforms/windows.cmake)
endif()

# ==================================================================================
# Build statistics
# ==================================================================================
get_target_property(QV2RAY_BASE_LINK_LIBS qv2ray_baselib LINK_LIBRARIES)
get_target_property(QV2RAY_EXEC_LINK_LIBS qv2ray LINK_LIBRARIES)
QVLOG(QV2RAY_BUILD_VERSION)
QVLOG(QV2RAY_VERSION_STRING)
QVLOG(CMAKE_INSTALL_PREFIX)
QVLOG(CMAKE_BINARY_DIR)
QVLOG(QV2RAY_BASE_LINK_LIBS)
QVLOG(QV2RAY_EXEC_LINK_LIBS)
message("")
message("======== Qv2ray Build Statistics ========")
message("${QV2RAY_BUILD_STATS}")
message("=========================================")
message("")

# ==================================================================================
# Plugin boundary validation — keep this as the final top-level graph check.
# ==================================================================================
include(cmake/plugin-boundary.cmake)
qv2ray_verify_internal_component_boundary("${CMAKE_SOURCE_DIR}")
