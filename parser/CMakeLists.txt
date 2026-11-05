cmake_minimum_required(VERSION 3.20)

project(ida_dump_parser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(disassembly_parser
    src/DisassemblyParser.cpp
)

target_include_directories(disassembly_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(disassembly_parser PUBLIC cxx_std_20)

add_executable(ida_dump_parser
    src/main.cpp
)

target_link_libraries(ida_dump_parser PRIVATE disassembly_parser)

include(CTest)

if(BUILD_TESTING)
    add_executable(disassembly_parser_tests
        tests/DisassemblyParserTests.cpp
    )

    target_link_libraries(disassembly_parser_tests PRIVATE disassembly_parser)
    add_test(NAME disassembly_parser_tests COMMAND disassembly_parser_tests)
endif()
