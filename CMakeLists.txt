cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CTR_NATIVE_VERSION "2.1.1")
option(CTR_CSEQ_LIBERATION "Enable native CSEQ sample liberation and external CSEQ packages" ON)
option(CTR_PRACTICE_FASTSTART "Build special practice fast-start mode (HUD on, no race intro/countdown)" OFF)
option(CTR_CSEQ_TEST_BUILD "Legacy CSEQ diagnostic build; implies liberation + practice fast-start" OFF)
option(CTR_LTO "Enable GCC link-time optimization for release/performance builds" ON)
option(CTR_CPU_NATIVE "Tune ctr_native for the build machine CPU (non-portable test/release variant)" OFF)
set(CTR_PGO_MODE "OFF" CACHE STRING "Profile-guided optimization mode: OFF, GENERATE, or USE")
set_property(CACHE CTR_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(CTR_PGO_DIR "C:/CTR_PGO/v2_0_8" CACHE PATH "Directory used for GCC PGO data")

if(CTR_CSEQ_TEST_BUILD)
    set(CTR_CSEQ_LIBERATION ON)
    set(CTR_PRACTICE_FASTSTART ON)
endif()

if(NOT CMAKE_C_COMPILER_TARGET MATCHES "i.86" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "i.86")
    set(CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "-m32 ${CMAKE_EXE_LINKER_FLAGS}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    string(TIMESTAMP CTR_NATIVE_BUILD_TIMESTAMP "%Y%m%d%H%M%S" UTC)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty-${CTR_NATIVE_BUILD_TIMESTAMP}")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse -Wno-error -g")

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    BUILD=926
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
if(CTR_CSEQ_LIBERATION)
    target_compile_definitions(ctr_native PRIVATE CTR_CSEQ_LIBERATION)
endif()
if(CTR_PRACTICE_FASTSTART)
    target_compile_definitions(ctr_native PRIVATE CTR_PRACTICE_FASTSTART)
endif()
if(CTR_CSEQ_TEST_BUILD)
    target_compile_definitions(ctr_native PRIVATE CTR_CSEQ_TEST_BUILD)
endif()
# Keep the legacy unity build on O2 globally. Hot renderer paths use
# low-level fast helpers without exposing PS1 layout code to aggressive O3 UB.
target_compile_options(ctr_native PRIVATE -O3 -fno-strict-aliasing -fno-delete-null-pointer-checks)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ctr_native PRIVATE -Wenum-conversion -Wenum-compare -Wswitch-enum -Wformat -Wparentheses)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
    if(CTR_CPU_NATIVE)
        target_compile_options(ctr_native PRIVATE -march=native -mtune=native)
    endif()
    if(CTR_LTO)
        target_compile_options(ctr_native PRIVATE -flto=6)
        # Keep the executable 32-bit, but let GCC optimize across the final link.
        # This measured faster than the previous release build without changing
        # CTR's PS1-shaped data layouts or pointer width.
        target_link_options(ctr_native PRIVATE -flto=6)
    endif()

    # Optional release-training pass. GENERATE records real branch/call
    # behavior from the automated Stress Test; USE feeds that profile back
    # into GCC/LTO. No relaxed floating-point rules or ISA-specific code are
    # required, so the trained executable remains a normal portable x86 build.
    if(CTR_PGO_MODE STREQUAL "GENERATE")
        target_compile_options(ctr_native PRIVATE "-fprofile-generate=${CTR_PGO_DIR}")
        target_link_options(ctr_native PRIVATE "-fprofile-generate=${CTR_PGO_DIR}")
        target_compile_definitions(ctr_native PRIVATE CTR_PGO_TRAINING=1 CTR_PGO_GENERATE=1)
    elseif(CTR_PGO_MODE STREQUAL "USE")
        target_compile_options(ctr_native PRIVATE "-fprofile-use=${CTR_PGO_DIR}" -fprofile-correction -fprofile-partial-training)
        target_link_options(ctr_native PRIVATE "-fprofile-use=${CTR_PGO_DIR}")
        # Keep the same CFG as the training binary so GCC can consume every
        # collected counter without shape mismatches. This hook is dormant
        # unless the developer creates debug/perf.autorun.
        target_compile_definitions(ctr_native PRIVATE CTR_PGO_TRAINING=1 CTR_PGO_GENERATE=1)
        target_link_libraries(ctr_native gcov)
    endif()

    ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
endif()
target_link_libraries(ctr_native SDL3::SDL3 vorbisfile vorbis ogg)
target_link_options(ctr_native PRIVATE -static-libgcc)
if(WIN32)
    target_link_options(ctr_native PRIVATE -static)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # Still a 32-bit executable; this only allows the process to use the
        # full user-mode address space available on 64-bit Windows.
        target_link_options(ctr_native PRIVATE -Wl,--large-address-aware)
    endif()
endif()
