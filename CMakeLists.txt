cmake_minimum_required(VERSION 3.11)

project(Kuzu VERSION 0.0.8 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_FIND_PACKAGE_RESOLVE_SYMLINKS TRUE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Detect OS and architecture, copied from DuckDB
set(OS_NAME "unknown")
set(OS_ARCH "amd64")

string(REGEX MATCH "(arm64|aarch64)" IS_ARM "${CMAKE_SYSTEM_PROCESSOR}")
if(IS_ARM)
  set(OS_ARCH "arm64")
elseif(FORCE_32_BIT)
  set(OS_ARCH "i386")
endif()

if(APPLE)
  set(OS_NAME "osx")
endif()
if(WIN32)
  set(OS_NAME "windows")
endif()
if(UNIX AND NOT APPLE)
  set(OS_NAME "linux") # sorry BSD
endif()


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(DEFINED ENV{PYBIND11_PYTHON_VERSION})
    set(PYBIND11_PYTHON_VERSION $ENV{PYBIND11_PYTHON_VERSION})
endif()

if(DEFINED ENV{PYTHON_EXECUTABLE})
    set(PYTHON_EXECUTABLE $ENV{PYTHON_EXECUTABLE})
endif()

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
else ()
    find_program(CCACHE_PROGRAM sccache)
    if (CCACHE_PROGRAM)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
    endif ()
endif ()

set(INSTALL_LIB_DIR
        lib
        CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR
        bin
        CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR
        include
        CACHE PATH "Installation directory for header files")
set(INSTALL_CMAKE_DIR
        ${DEF_INSTALL_CMAKE_DIR}
        CACHE PATH "Installation directory for CMake files")

option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer." FALSE)
option(ENABLE_THREAD_SANITIZER "Enable thread sanitizer." FALSE)
option(ENABLE_UBSAN "Enable undefined behavior sanitizer." FALSE)
option(USE_SYSTEM_ARROW "Use system version of arrow" FALSE)
if(MSVC)
    # Required for M_PI on Windows
    add_compile_definitions(_USE_MATH_DEFINES)
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(ARROW_STATIC PARQUET_STATIC)
    # TODO (bmwinger): Figure out if this can be set automatically by cmake,
    # or at least better integrated with user-specified options
    # For now, hardcode _AMD64_
    # CMAKE_GENERATOR_PLATFORM can be used for visual studio builds, but not for ninja
    add_compile_definitions(_AMD64_)
    # Non-english windows system may use other encodings other than utf-8 (e.g. Chinese use GBK).
    add_compile_options("/utf-8")
endif()
if(CMAKE_BUILD_TYPE MATCHES Release)
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /utf-8")
        string(REGEX REPLACE "/W[3|4]" "/w" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_compile_options(/W0)
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
    endif()
endif()

if(${ENABLE_THREAD_SANITIZER})
    if(MSVC)
        message(FATAL_ERROR "Thread sanitizer is not supported on MSVC")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-common -fpermissive")
    endif()
endif()
if(${ENABLE_ADDRESS_SANITIZER})
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fno-common -fpermissive")
    endif()
endif()
if(${ENABLE_UBSAN})
    if(MSVC)
        message(FATAL_ERROR "Undefined behavior sanitizer is not supported on MSVC")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-common -fpermissive")
    endif()
endif()
option(BUILD_PYTHON_API "Build Python API." TRUE)
option(BUILD_SHELL "Build Interactive Shell" TRUE)
option(BUILD_TESTS "Build C++ and Python tests." FALSE)
option(BUILD_BENCHMARK "Build benchmarks." FALSE)

option(BUILD_LCOV "Build coverage report." FALSE)
if(${BUILD_LCOV})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
endif()

function(link_threads LIBRARY)
    if (CMAKE_VERSION VERSION_LESS "3.1")
        target_link_libraries(${LIBRARY} pthread)
    else ()
        target_link_libraries(${LIBRARY} Threads::Threads)
    endif ()
endfunction()

function(add_kuzu_test TEST_NAME)
    set(SRCS ${ARGN})
    add_executable(${TEST_NAME} ${SRCS})
    target_link_libraries(${TEST_NAME} PRIVATE test_helper test_runner graph_test)
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/test/include)
    include(GoogleTest)
    gtest_discover_tests(${TEST_NAME})
endfunction()

add_definitions(-DKUZU_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}")
add_definitions(-DKUZU_STORAGE_VERSION="${CMAKE_PROJECT_VERSION}")

if (${USE_SYSTEM_ARROW})
    find_package(Arrow REQUIRED)
    find_package(Parquet REQUIRED)
    if (TARGET arrow_shared)
        set(ARROW_LIB arrow_shared)
    else()
        set(ARROW_LIB arrow_static)
    endif()
    if (TARGET parquet_shared)
        set(PARQUET_LIB parquet_shared)
    else()
        set(PARQUET_LIB parquet_static)
    endif()
else()
    if (NOT DEFINED ARROW_INSTALL)
        message(STATUS "Configuring arrow for bundled install")
        set(ARROW_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/arrow-build/arrow/install)
        set(ARROW_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/arrow-build)
        file(MAKE_DIRECTORY ${ARROW_BUILD_DIR})
        execute_process(COMMAND ${CMAKE_COMMAND}
            ${CMAKE_CURRENT_SOURCE_DIR}/external
            -G ${CMAKE_GENERATOR}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DENABLE_ADDRESS_SANITIZER=${ENABLE_ADDRESS_SANITIZER}
            -DENABLE_THREAD_SANITIZER=${ENABLE_THREAD_SANITIZER}
            -DENABLE_UBSAN=${ENABLE_UBSAN}
            -DFORCE_COLORED_OUTPUT=${FORCE_COLORED_OUTPUT}
            WORKING_DIRECTORY ${ARROW_BUILD_DIR}
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${CMAKE_COMMAND} --build ${ARROW_BUILD_DIR} --config ${CMAKE_BUILD_TYPE}
            COMMAND_ERROR_IS_FATAL ANY)
    else()
        message(STATUS "Using arrow at path ${ARROW_INSTALL}")
    endif()
    find_library(ARROW_DEPS_PATH arrow_bundled_dependencies HINTS ${ARROW_INSTALL}/lib ${ARROW_INSTALL}/lib64)
    if(WIN32)
        find_library(PARQUET_PATH parquet_static HINTS ${ARROW_INSTALL}/lib ${ARROW_INSTALL}/lib64)
        find_library(ARROW_PATH arrow_static HINTS ${ARROW_INSTALL}/lib ${ARROW_INSTALL}/lib64)
    else()
        find_library(PARQUET_PATH parquet HINTS ${ARROW_INSTALL}/lib ${ARROW_INSTALL}/lib64)
        find_library(ARROW_PATH arrow HINTS ${ARROW_INSTALL}/lib ${ARROW_INSTALL}/lib64)
    endif()

    add_library(arrow_deps STATIC IMPORTED)
    set_target_properties(arrow_deps PROPERTIES IMPORTED_LOCATION ${ARROW_DEPS_PATH})
    add_library(parquet_lib STATIC IMPORTED)
    set_target_properties(parquet_lib PROPERTIES IMPORTED_LOCATION ${PARQUET_PATH})
    add_library(arrow_lib STATIC IMPORTED)
    set_target_properties(arrow_lib PROPERTIES IMPORTED_LOCATION ${ARROW_PATH})
    include_directories(${ARROW_INSTALL}/include)

    set(ARROW_LIB arrow_lib arrow_deps)
    set(PARQUET_LIB parquet_lib)
endif()

include_directories(src/include)
include_directories(third_party/antlr4_cypher/include)
include_directories(third_party/antlr4_runtime/src)
include_directories(third_party/spdlog)
include_directories(third_party/nlohmann_json)
include_directories(third_party/pyparse)
include_directories(third_party/utf8proc/include)
include_directories(third_party/pybind11/include)
include_directories(third_party/re2/include)
include_directories(third_party/concurrentqueue)

add_subdirectory(third_party)
add_subdirectory(src)
if (${BUILD_TESTS})
    add_subdirectory(test)
elseif (${BUILD_BENCHMARK})
    add_subdirectory(test/test_helper)
endif()
add_subdirectory(tools)
