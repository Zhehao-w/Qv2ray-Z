function(ADD_QV2RAY_TEST TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} catch.hpp)
    target_include_directories(${TEST_NAME}
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        )
    target_link_libraries(
        ${TEST_NAME}
        PRIVATE
        $<$<TARGET_EXISTS:qv2ray_baselib>:qv2ray_baselib>
        )

    add_test(NAME QV2RAY_TEST_${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

ADD_QV2RAY_TEST(parse_ss_url src/core/connection/TestParseSS.cpp)
ADD_QV2RAY_TEST(parse_vmess_url src/core/connection/TestParseVmess.cpp)
ADD_QV2RAY_TEST(parse_vless_url src/core/connection/TestParseVLESS.cpp)
target_sources(parse_vless_url PRIVATE
    src/core/connection/TestVLESSEncryption.cpp
    src/core/connection/TestVLESSOpaqueQuery.cpp
    src/core/connection/TestVLESSRepresentation.cpp
    src/core/connection/TestXHTTP.cpp
    src/core/connection/TestFinalMask.cpp
    src/core/connection/TestStreamTransportMapping.cpp
    src/core/connection/TestRemovedQuicTransport.cpp
    src/core/connection/TestRemovedHttpTransport.cpp
    src/core/connection/TestRemovedKcpLegacyFields.cpp
    src/core/connection/VLESSOutboundSerializerTestHelper.cpp
    src/core/connection/VLESSOutboundSerializerTestHelper.hpp
    ${CMAKE_SOURCE_DIR}/src/plugins/protocols/core/OutboundHandler.cpp)
target_include_directories(parse_vless_url PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin-interface
    ${CMAKE_SOURCE_DIR}/src/plugins/common
    ${CMAKE_SOURCE_DIR}/src/plugins/protocols/core)
ADD_QV2RAY_TEST(generation src/core/connection/TestGeneration.cpp)
target_sources(generation PRIVATE
    src/core/connection/TestTlsPinPersistence.cpp
    src/core/connection/TestTlsPinRuntimeSafety.cpp)
ADD_QV2RAY_TEST(qjsonio libs/QJsonStruct/QJsonIO.cpp)
ADD_QV2RAY_TEST(settings_compatibility src/core/settings/TestSettingsCompatibility.cpp)
ADD_QV2RAY_TEST(realping src/components/latency/TestRealPing.cpp)
ADD_QV2RAY_TEST(kernel_path_resolver src/core/kernel/TestKernelPathResolver.cpp)
ADD_QV2RAY_TEST(kernel_process_lifecycle src/core/kernel/TestKernelProcessLifecycle.cpp)
ADD_QV2RAY_TEST(threading_hygiene src/core/kernel/TestThreadingHygiene.cpp)
ADD_QV2RAY_TEST(windows_integration_safety src/components/proxy/TestWindowsIntegrationSafety.cpp)
ADD_QV2RAY_TEST(data_safety src/core/handler/TestDataSafety.cpp)
target_sources(data_safety PRIVATE
    src/core/handler/TestRoutingPersistence.cpp
    src/ui/widgets/windows/TestGroupManagerSafety.cpp)
ADD_QV2RAY_TEST(connection_context_resolver src/core/handler/TestConnectionContextResolver.cpp)
ADD_QV2RAY_TEST(http_request_safety src/utils/TestHTTPRequestHelper.cpp)
ADD_QV2RAY_TEST(crash_diagnostic_safety src/utils/TestDiagnosticSafety.cpp)

# This target compiles most of the UI again. Keep it out of regular commit CI and
# enable it only for explicit UI validation.
option(QV2RAY_BUILD_ROUTING_UI_SAFETY "Build the full routing UI safety regression target" OFF)
if(QV2RAY_BUILD_ROUTING_UI_SAFETY)
    find_package(Qt5 REQUIRED COMPONENTS Test)
    ADD_QV2RAY_TEST(routing_ui_safety src/ui/widgets/TestRoutingUiSafety.cpp)
    target_sources(routing_ui_safety PRIVATE
        ${QV2RAY_UI_SOURCES}
        ${QV2RAY_PLATFORM_SOURCES}
        ${QV2RAY_QRC_RESOURCES})
    target_include_directories(routing_ui_safety PRIVATE ${QNODEEDITOR_INCLUDE_PATH})
    target_link_libraries(routing_ui_safety PRIVATE
        Qt5::Test ${QNODEEDITOR_LIBRARY} ${QV2RAY_PLATFORM_LIBS} ${QV2RAY_QRENCODE_LIBRARY})
    set_tests_properties(QV2RAY_TEST_routing_ui_safety PROPERTIES TIMEOUT 60)
    if(NOT WIN32)
        set_tests_properties(QV2RAY_TEST_routing_ui_safety PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
