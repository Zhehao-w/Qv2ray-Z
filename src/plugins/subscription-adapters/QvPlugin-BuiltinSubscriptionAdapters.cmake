set(SUBSCRIPTION_PLUGIN_TARGET QvPlugin-BuiltinSubscriptionSupport)

set(QVPLUGIN_INTERFACE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/plugin-interface)

include(${CMAKE_SOURCE_DIR}/src/plugin-interface/QvPluginInterface.cmake)
include(${CMAKE_SOURCE_DIR}/src/plugin-interface/QvGUIPluginInterface.cmake)

set(BUILTIN_SUBSCRIPTION_PLUGIN_SOURCES
    ${QVPLUGIN_INTERFACE_HEADERS}
    ${QVGUIPLUGIN_INTERFACE_HEADERS}
    ${CMAKE_CURRENT_LIST_DIR}/BuiltinSubscriptionAdapter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/BuiltinSubscriptionAdapter.hpp
    ${CMAKE_CURRENT_LIST_DIR}/core/SubscriptionAdapter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/core/SubscriptionAdapter.hpp
    )
list(APPEND PLUGIN_TRANSLATION_SOURCES ${BUILTIN_SUBSCRIPTION_PLUGIN_SOURCES})

# Subscription parsing is first-party functionality. Build it as in-process
# object code instead of a native runtime-loaded plugin.
add_library(${SUBSCRIPTION_PLUGIN_TARGET} OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/resx.qrc
    ${BUILTIN_SUBSCRIPTION_PLUGIN_SOURCES}
    )

target_include_directories(${SUBSCRIPTION_PLUGIN_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(${SUBSCRIPTION_PLUGIN_TARGET} PRIVATE ${QVPLUGIN_INTERFACE_INCLUDE_DIR})
target_include_directories(${SUBSCRIPTION_PLUGIN_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(${SUBSCRIPTION_PLUGIN_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)

target_link_libraries(${SUBSCRIPTION_PLUGIN_TARGET}
    ${QV_QT_LIBNAME}::Core
    ${QV_QT_LIBNAME}::Gui
    ${QV_QT_LIBNAME}::Widgets)

target_sources(qv2ray_baselib PRIVATE $<TARGET_OBJECTS:${SUBSCRIPTION_PLUGIN_TARGET}>)
