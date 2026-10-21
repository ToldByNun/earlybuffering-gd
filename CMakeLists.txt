cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(frirecracker VERSION 1.0.0)

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
if (WIN32)
    target_link_libraries(${PROJECT_NAME} comdlg32)
endif()

# MSVC batch-compiles TUs and can ICE on some Geode/arc headers; one TU per cl.exe.
if (MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_UseMultiToolTask false)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VS_GLOBAL_CL_MPCount 1)
endif()

setup_geode_mod(${PROJECT_NAME})
