if(WIN32)
    list(APPEND QV2RAY_PLATFORM_SOURCES
        ${CMAKE_SOURCE_DIR}/src/platform/windows/AppContainerLoopback.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/windows/AppContainerLoopback.hpp
        ${CMAKE_SOURCE_DIR}/src/ui/widgets/windows/w_AppContainerLoopback.cpp
        ${CMAKE_SOURCE_DIR}/src/ui/widgets/windows/w_AppContainerLoopback.hpp
        )
    list(APPEND QV2RAY_PLATFORM_LIBS advapi32 ole32 shell32 shlwapi userenv)
endif()
