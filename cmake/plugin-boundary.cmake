# Plugin boundary guard for the maintained Qv2ray-Z build.
#
# The current product model deliberately keeps the historical plugin interface
# vocabulary for first-party components while removing the external native
# plugin trust boundary. Keep these checks close to the active CMake graph so
# every supported/diagnostic configure verifies the invariant.

function(qv2ray_verify_internal_component_boundary source_root)
    set(expected_internal_components
        QvPlugin-BuiltinProtocolSupport
        QvPlugin-BuiltinSubscriptionSupport
        )

    foreach(target_name IN LISTS expected_internal_components)
        if(NOT TARGET ${target_name})
            message(FATAL_ERROR
                "Plugin boundary violation: expected in-process component target '${target_name}' is missing.")
        endif()

        get_target_property(target_type ${target_name} TYPE)
        if(NOT target_type STREQUAL "OBJECT_LIBRARY")
            message(FATAL_ERROR
                "Plugin boundary violation: '${target_name}' must remain an OBJECT_LIBRARY, got '${target_type}'.")
        endif()
    endforeach()

    # This legacy target still has an inert historical MODULE recipe in the
    # source tree. The focused cleanup PR will remove it; until then neither a
    # direct top-level include nor an already-active target is allowed.
    file(READ "${source_root}/CMakeLists.txt" top_level_cmake_source)
    string(FIND "${top_level_cmake_source}"
        "include(src/plugins/utils/QvPlugin-BuiltinUtils.cmake)"
        legacy_utils_include_index)
    if(NOT legacy_utils_include_index EQUAL -1)
        message(FATAL_ERROR
            "Plugin boundary violation: legacy QvPlugin-BuiltinUtils must not be included by the top-level build.")
    endif()
    if(TARGET QvPlugin-BuiltinUtils)
        message(FATAL_ERROR
            "Plugin boundary violation: legacy QvPlugin-BuiltinUtils must not be an active build target.")
    endif()

    # QvPluginHost is the runtime registration point. Prevent accidental
    # reintroduction of native library loading there. This is intentionally
    # scoped to the host instead of globally banning platform library APIs.
    file(READ "${source_root}/src/components/plugins/QvPluginHost.cpp" plugin_host_source)
    foreach(forbidden_marker IN ITEMS QPluginLoader QLibrary LoadLibrary dlopen)
        string(FIND "${plugin_host_source}" "${forbidden_marker}" marker_index)
        if(NOT marker_index EQUAL -1)
            message(FATAL_ERROR
                "Plugin boundary violation: QvPluginHost contains forbidden native-loader marker '${forbidden_marker}'.")
        endif()
    endforeach()
endfunction()
