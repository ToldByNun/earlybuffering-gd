cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(earlybuffer VERSION 1.3.2)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# GDR / GDR2 replay format (header-only)
set(LIBGDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
CPMAddPackage("gh:maxnut/GDReplayFormat#d8d2f0b1193a46cdf47b413dc832616dd052b6a9")
target_link_libraries(${PROJECT_NAME} libGDR)

# Win32 file dialogs (PauseActionBridge GetOpenFileNameW / GetSaveFileNameW).
# Must stay WIN32-only — never link comdlg32 (or any dialog lib) on Android/NDK,
# macOS, or iOS. Non-Windows uses SaveDropPaths under the mod save dir instead.
if (WIN32)
    target_link_libraries(${PROJECT_NAME} comdlg32)
elseif (ANDROID)
    message(STATUS "EarlyBuffer: Android build — no Windows dialog libs (comdlg32)")
endif()

# MSVC batch-compiles TUs and can ICE on some Geode/arc headers; one TU per cl.exe.
if (MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_UseMultiToolTask false)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_CL_MPCount 1)
endif()

# GeodeBindings GeneratedSource.cpp uses FMOD_* types without including fmod.h.
# On iOS (and sometimes other Clang targets) that fails with "unknown type name FMOD_RESULT".
# Force-include Geode's FMOD headers into GeodeBindings before those TUs compile.
if (TARGET GeodeBindings)
    set(_EB_FMOD_H "$ENV{GEODE_SDK}/loader/include/Geode/fmod/fmod.h")
    if (EXISTS "${_EB_FMOD_H}")
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
            target_compile_options(GeodeBindings PRIVATE "-include${_EB_FMOD_H}")
        elseif (MSVC)
            target_compile_options(GeodeBindings PRIVATE "/FI${_EB_FMOD_H}")
        endif()
        message(STATUS "EarlyBuffer: force-including FMOD headers for GeodeBindings")
    endif()
endif()

setup_geode_mod(${PROJECT_NAME})

# iOS: unsigned .ios.dylib fails Geode dlopen with "missing code signature"
# (dyld may also report "no such file" as the first tried path). Ad-hoc sign
# after link so the packaged binary always carries LC_CODE_SIGNATURE.
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    find_program(EB_LDID ldid)
    find_program(EB_CODESIGN codesign)
    if (EB_LDID)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${EB_LDID}" -S "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "EarlyBuffer: ad-hoc sign iOS dylib (ldid)"
            VERBATIM
        )
    elseif (EB_CODESIGN)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${EB_CODESIGN}" -f -s - "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "EarlyBuffer: ad-hoc sign iOS dylib (codesign)"
            VERBATIM
        )
    else()
        message(WARNING
            "EarlyBuffer: no ldid/codesign on PATH — iOS dylib may fail "
            "Geode dlopen with 'missing code signature'. Install ldid or "
            "Xcode command-line tools."
        )
    endif()
endif()
