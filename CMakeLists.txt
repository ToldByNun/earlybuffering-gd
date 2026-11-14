cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(earlybuffer VERSION 1.6.2)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Generated class catalog (eb_class_catalog.hpp + service dependency TU).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(EB_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EB_CLASS_CATALOG "${EB_GENERATED_DIR}/eb_class_catalog.hpp")
set(EB_SERVICE_DEPS "${EB_GENERATED_DIR}/eb_service_dependencies.cpp")
file(MAKE_DIRECTORY "${EB_GENERATED_DIR}")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_class_registry.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${EB_GENERATED_DIR}"
    RESULT_VARIABLE EB_CATALOG_RESULT
)
if (NOT EB_CATALOG_RESULT EQUAL 0)
    message(FATAL_ERROR "EarlyBuffer: class catalog generation failed")
endif()
add_custom_target(earlybuffer_class_catalog
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_class_registry.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${EB_GENERATED_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    BYPRODUCTS "${EB_CLASS_CATALOG}" "${EB_SERVICE_DEPS}"
    COMMENT "EarlyBuffer: generate class catalog"
    VERBATIM
)
add_dependencies(${PROJECT_NAME} earlybuffer_class_catalog)
target_include_directories(${PROJECT_NAME} PRIVATE "${EB_GENERATED_DIR}")
target_sources(${PROJECT_NAME} PRIVATE "${EB_SERVICE_DEPS}")

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

# Desktop ImGui (Windows + macOS only). Android/iOS never fetch or link imgui-cocos.
set(EB_DESKTOP_IMGUI OFF)
if (WIN32)
    set(EB_DESKTOP_IMGUI ON)
elseif (APPLE AND NOT ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS))
    set(EB_DESKTOP_IMGUI ON)
endif()
if (EB_DESKTOP_IMGUI)
    # Draw above early-hook mod menus (we are a settings overlay, not under them).
    set(IMGUI_HOOK_EARLY OFF CACHE BOOL "" FORCE)
    set(IMGUI_VERSION "v1.92.4" CACHE STRING "" FORCE)
    CPMAddPackage("gh:matcool/gd-imgui-cocos#934958c68b1f050a8976d893584fa71368c1df1f")
    target_link_libraries(${PROJECT_NAME} imgui-cocos)
    target_compile_definitions(${PROJECT_NAME} PRIVATE EB_DESKTOP_IMGUI=1)
    message(STATUS "EarlyBuffer: desktop ImGui enabled (gd-imgui-cocos)")
else()
    message(STATUS "EarlyBuffer: desktop ImGui disabled (non-desktop target)")
endif()

# Win32 file dialogs (PauseActionBridge GetOpenFileNameW / GetSaveFileNameW).
# Must stay WIN32-only — never link comdlg32 (or any dialog lib) on Android/NDK,
# macOS, or iOS. Non-Windows uses SaveDropPaths under the mod save dir instead.
if (WIN32)
    # comdlg32: PauseActionBridge file dialogs.
    # psapi: Dev Lab process RAM sampler (GetProcessMemoryInfo).
    target_link_libraries(${PROJECT_NAME} comdlg32 psapi)
elseif (ANDROID)
    message(STATUS "EarlyBuffer: Android build — no Windows dialog libs (comdlg32)")
endif()

# MSVC batch-compiles TUs and can ICE on some Geode/arc headers; one TU per cl.exe.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /we4715)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_UseMultiToolTask false)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_CL_MPCount 1)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror=return-type)
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
