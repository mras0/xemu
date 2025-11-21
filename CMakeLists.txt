cmake_minimum_required(VERSION 3.14)
project(xemu C CXX)

if (MSVC)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /Zc:preprocessor /permissive- /Zi /Zo /sdl /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201") # C4201: nonstandard extension used: nameless struct/union
    if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /JMC")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
    add_definitions("-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23 -Wall -Wextra -pedantic -g")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-unused-const-variable")
    endif()
endif()


if (WIN32)
    add_definitions("-DWIN32 -D_WIN32 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x600")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(ZLIB)

if (NOT ZLIB_FOUND)
    set(ZLIB_DIR zlib-1.3.1)
    add_library(zlib
        ${ZLIB_DIR}/adler32.c
        ${ZLIB_DIR}/crc32.c
        ${ZLIB_DIR}/deflate.c
        ${ZLIB_DIR}/gzclose.c
        ${ZLIB_DIR}/gzlib.c
        ${ZLIB_DIR}/gzread.c
        ${ZLIB_DIR}/gzwrite.c
        ${ZLIB_DIR}/inffast.c
        ${ZLIB_DIR}/inflate.c
        ${ZLIB_DIR}/inftrees.c
        ${ZLIB_DIR}/trees.c
        ${ZLIB_DIR}/zutil.c
        ${ZLIB_DIR}/zlib.h
        )
    set(ZLIB_INCLUDE_DIR ${ZLIB_DIR})
    set(ZLIB_LIBRARY zlib)
endif()

if (WIN32)
    set(GUI_SRC gui_win32.cpp)
else()
    set(GUI_SRC gui_null.cpp)
endif()

set(OPCODE_TABLES
    opcodes_8086.cpp
    opcodes_80386.cpp
    )

add_executable(xemu
    main.cpp
    fileio.cpp fileio.h
    address.cpp address.h
    util.cpp util.h
    decode.cpp decode.h
    opcodes.cpp opcodes.h
    cpu.cpp cpu.h
    cpu_descriptor.cpp cpu_descriptor.h
    cpu_exception.cpp cpu_exception.h
    cpu_flags.cpp cpu_flags.h
    cpu_registers.cpp cpu_registers.h
    system_bus.cpp system_bus.h
    debugger.cpp debugger.h
    ${GUI_SRC} gui.h
    gzstream.cpp gzstream.h
    # Tests
    test_moo.cpp
    # Devices
    devs/dma_handler.h
    devs/cga.cpp devs/cga.h devs/cga_font.h
    devs/i8259a_pic.cpp devs/i8259a_pic.h
    devs/i8253_pit.cpp devs/i8253_pit.h
    devs/nec765_floppy_controller.cpp devs/nec765_floppy_controller.h
    devs/i8237a_dma_controller.cpp devs/i8237a_dma_controller.h
    # Automatically generated
    opcode_types.cpp opcode_types.h
    ${OPCODE_TABLES}
    )

target_link_libraries(xemu ${ZLIB_LIBRARY})
target_include_directories(xemu PRIVATE ${ZLIB_INCLUDE_DIR})

if (NOT MSVC)
    set_source_files_properties(${OPCODE_TABLES} PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers -Wno-missing-braces")
endif()
