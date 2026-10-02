# Plugin boundary guard for the maintained Qv2ray-Z build.
#
# The current product model deliberately keeps the historical plugin interface
# vocabulary for first-party components while removing the external native
# plugin trust boundary. Run this check only after the complete top-level build
# graph has been configured so later/indirect includes cannot evade it.

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

    # The legacy utils recipe still exists in the source tree until the focused
    # cleanup PR removes it. Because this function runs after the full top-level
    # graph is configured, any direct, quoted, or indirect include that creates
    # the old MODULE target is caught by the target graph itself.
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
