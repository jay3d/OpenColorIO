# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the OpenColorIO Project.

set(SOURCES
    main.cpp
)

add_executable(ociomakeclf ${SOURCES})

if(MSVC)
    set(PLATFORM_COMPILE_OPTIONS "${PLATFORM_COMPILE_OPTIONS};/wd4996")
endif()

set_target_properties(ociomakeclf PROPERTIES 
    COMPILE_OPTIONS "${PLATFORM_COMPILE_OPTIONS}"
    LINK_OPTIONS "${PLATFORM_LINK_OPTIONS}"
)

target_link_libraries(ociomakeclf 
    PRIVATE 
        apputils
        OpenColorIO
        utils::strings
)

install(TARGETS ociomakeclf
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
