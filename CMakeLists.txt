cmake_minimum_required(VERSION 3.24)

project(IgvNativeDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

find_program(CARGO_EXECUTABLE NAMES cargo REQUIRED)

if(NOT CMAKE_PREFIX_PATH AND NOT Qt5_DIR AND NOT Qt6_DIR)
  find_program(QMAKE_EXECUTABLE NAMES qmake qmake6 qmake-qt5 qmake-qt6)
  if(QMAKE_EXECUTABLE)
    execute_process(
      COMMAND "${QMAKE_EXECUTABLE}" -query QT_INSTALL_PREFIX
      OUTPUT_VARIABLE QT_INSTALL_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )

    if(QT_INSTALL_PREFIX)
      list(PREPEND CMAKE_PREFIX_PATH "${QT_INSTALL_PREFIX}/lib/cmake")
    endif()
  endif()
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(RUST_BUILD_ARGS)
set(RUST_PROFILE "debug")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  list(APPEND RUST_BUILD_ARGS "--release")
  set(RUST_PROFILE "release")
endif()

file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.rs"
)

set(RUST_DEPENDENCIES
  "${CMAKE_SOURCE_DIR}/Cargo.toml"
  ${RUST_SOURCES}
)

if(EXISTS "${CMAKE_SOURCE_DIR}/Cargo.lock")
  list(APPEND RUST_DEPENDENCIES "${CMAKE_SOURCE_DIR}/Cargo.lock")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/build.rs")
  list(APPEND RUST_DEPENDENCIES "${CMAKE_SOURCE_DIR}/build.rs")
endif()

set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo-target")
set(RUST_LIB_PATH "${RUST_TARGET_DIR}/${RUST_PROFILE}/libigv_native_core.a")

add_custom_command(
  OUTPUT "${RUST_LIB_PATH}"
  COMMAND "${CARGO_EXECUTABLE}" build
          --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
          --target-dir "${RUST_TARGET_DIR}"
          ${RUST_BUILD_ARGS}
  DEPENDS ${RUST_DEPENDENCIES}
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building Rust core"
  COMMAND_EXPAND_LISTS
  USES_TERMINAL
  VERBATIM
)

add_custom_target(igv_native_core_build ALL DEPENDS "${RUST_LIB_PATH}")

add_executable(igv_native_desktop MACOSX_BUNDLE
  desktop/main.cpp
  desktop/MainWindow.cpp
  desktop/MainWindow.h
  desktop/GenomeCanvas.cpp
  desktop/GenomeCanvas.h
  desktop/SessionParsers.cpp
  desktop/SessionParsers.h
  desktop/RustBridge.h
  desktop/SessionTypes.h
)

add_dependencies(igv_native_desktop igv_native_core_build)

target_include_directories(igv_native_desktop PRIVATE "${CMAKE_SOURCE_DIR}/desktop")

target_link_libraries(igv_native_desktop PRIVATE
  Qt${QT_VERSION_MAJOR}::Widgets
  "${RUST_LIB_PATH}"
)

if(APPLE)
  target_link_libraries(igv_native_desktop PRIVATE
    "-framework CoreFoundation"
    "-framework Security"
    "-framework SystemConfiguration"
    "-framework CoreServices"
    "-framework Foundation"
  )
endif()

add_executable(igv_session_parser_tests
  tests/session_parsers_test.cpp
  desktop/SessionParsers.cpp
  desktop/SessionParsers.h
  desktop/SessionTypes.h
)

target_include_directories(igv_session_parser_tests PRIVATE "${CMAKE_SOURCE_DIR}/desktop")

target_link_libraries(igv_session_parser_tests PRIVATE
  Qt${QT_VERSION_MAJOR}::Widgets
)

add_test(NAME igv_session_parser_tests COMMAND igv_session_parser_tests)
