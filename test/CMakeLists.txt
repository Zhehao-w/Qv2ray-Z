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
    src/core/connection/TestXHTTP.cpp
    src/core/connection/TestFinalMask.cpp
    src/core/connection/TestStreamTransportMapping.cpp
    src/core/connection/VLESSOutboundSerializerTestHelper.cpp
    src/core/connection/VLESSOutboundSerializerTestHelper.hpp
    ${CMAKE_SOURCE_DIR}/src/plugins/protocols/core/OutboundHandler.cpp)
target_include_directories(parse_vless_url PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin-interface
    ${CMAKE_SOURCE_DIR}/src/plugins/common
    ${CMAKE_SOURCE_DIR}/src/plugins/protocols/core)
ADD_QV2RAY_TEST(generation src/core/connection/TestGeneration.cpp)
ADD_QV2RAY_TEST(qjsonio libs/QJsonStruct/QJsonIO.cpp)
ADD_QV2RAY_TEST(settings_compatibility src/core/settings/TestSettingsCompatibility.cpp)
ADD_QV2RAY_TEST(realping src/components/latency/TestRealPing.cpp)
ADD_QV2RAY_TEST(kernel_path_resolver src/core/kernel/TestKernelPathResolver.cpp)
ADD_QV2RAY_TEST(kernel_process_lifecycle src/core/kernel/TestKernelProcessLifecycle.cpp)
ADD_QV2RAY_TEST(threading_hygiene src/core/kernel/TestThreadingHygiene.cpp)
ADD_QV2RAY_TEST(windows_integration_safety src/components/proxy/TestWindowsIntegrationSafety.cpp)
ADD_QV2RAY_TEST(data_safety src/core/handler/TestDataSafety.cpp)
ADD_QV2RAY_TEST(connection_context_resolver src/core/handler/TestConnectionContextResolver.cpp)
ADD_QV2RAY_TEST(http_request_safety src/utils/TestHTTPRequestHelper.cpp)
ADD_QV2RAY_TEST(crash_diagnostic_safety src/utils/TestDiagnosticSafety.cpp)