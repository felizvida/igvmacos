cmake_minimum_required(VERSION 3.24)

project(IgvNativeDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT DEFINED CMAKE_PREFIX_PATH)
  set(CMAKE_PREFIX_PATH "/Users/liux17/miniforge/lib/cmake")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(RUST_PROFILE "release")
  set(RUST_BUILD_ARG "--release")
else()
  set(RUST_PROFILE "debug")
  set(RUST_BUILD_ARG "")
endif()

set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo-target")
set(RUST_LIB_PATH "${RUST_TARGET_DIR}/${RUST_PROFILE}/libigv_native_core.a")

add_custom_command(
  OUTPUT "${RUST_LIB_PATH}"
  COMMAND cargo build
          --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml"
          --target-dir "${RUST_TARGET_DIR}"
          ${RUST_BUILD_ARG}
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
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
  desktop/RustBridge.h
  desktop/SessionTypes.h
)

add_dependencies(igv_native_desktop igv_native_core_build)

target_include_directories(igv_native_desktop PRIVATE "${CMAKE_SOURCE_DIR}/desktop")

target_link_libraries(igv_native_desktop PRIVATE
  Qt5::Widgets
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
