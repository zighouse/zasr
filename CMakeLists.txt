cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
project(zasr)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE/editor support (LSP, clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Find required packages
find_package(Threads REQUIRED)

# sherpa-onnx paths - use local build from third_party
set(SHERPA_ONNX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx")
set(SHERPA_ONNX_INCLUDE_DIR "${SHERPA_ONNX_ROOT}")
set(SHERPA_ONNX_LIB_DIR "${SHERPA_ONNX_ROOT}/build/lib")
set(ONNX_RUNTIME_LIB_DIR "${SHERPA_ONNX_ROOT}/build/_deps/onnxruntime-src/lib")

# Find dependencies
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(EXISTS "${THIRD_PARTY_DIR}/asio/include/asio.hpp")
    set(ASIO_INCLUDE_DIR "${THIRD_PARTY_DIR}/asio/include")
    message(STATUS "Using local asio: ${ASIO_INCLUDE_DIR}")
elseif(EXISTS "/usr/include/asio.hpp")
    set(ASIO_INCLUDE_DIR "/usr/include")
    message(STATUS "Using system asio")
elseif(EXISTS "/usr/include/asio/asio.hpp")
    set(ASIO_INCLUDE_DIR "/usr/include/asio")
    message(STATUS "Using system asio (in asio/)")
endif()

if(EXISTS "${THIRD_PARTY_DIR}/websocketpp")
    set(WEBSOCKETPP_INCLUDE_DIR "${THIRD_PARTY_DIR}/websocketpp")
    message(STATUS "Using local websocketpp: ${WEBSOCKETPP_INCLUDE_DIR}")
else()
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WEBSOCKETPP websocketpp)
    endif()
    if(NOT WEBSOCKETPP_FOUND)
        find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/config/asio_no_tls.hpp
            PATHS /usr/include /usr/local/include)
        if(WEBSOCKETPP_INCLUDE_DIR)
            set(WEBSOCKETPP_FOUND TRUE)
            message(STATUS "Using system websocketpp")
        endif()
    endif()
endif()

if(EXISTS "${THIRD_PARTY_DIR}/json.hpp")
    set(NLOHMANN_JSON_INCLUDE_DIR "${THIRD_PARTY_DIR}")
    message(STATUS "Using local nlohmann/json")
else()
    if(NOT NLOHMANN_JSON_FOUND)
        find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
            PATHS /usr/include /usr/local/include)
        if(NLOHMANN_JSON_INCLUDE_DIR)
            set(NLOHMANN_JSON_FOUND TRUE)
            message(STATUS "Using system nlohmann/json")
        endif()
    endif()
endif()

# yaml-cpp for configuration file support
if(EXISTS "${THIRD_PARTY_DIR}/yaml-cpp/yaml-cpp")
    set(YAML_CPP_INCLUDE_DIR "${THIRD_PARTY_DIR}")
    message(STATUS "Using local yaml-cpp: ${YAML_CPP_INCLUDE_DIR}")
else()
    find_path(YAML_CPP_INCLUDE_DIR yaml-cpp/yaml.h
        PATHS /usr/include /usr/local/include)
    if(YAML_CPP_INCLUDE_DIR)
        message(STATUS "Using system yaml-cpp")
    endif()
endif()

# Add executable
add_executable(zasr-server
  src/zasr-server-main.cc
  src/zasr-server.cc
  src/zasr-connection.cc
  src/zasr-config.cc
  src/yaml-config.cc
)

# Include directories
target_include_directories(zasr-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SHERPA_ONNX_INCLUDE_DIR}
  ${ASIO_INCLUDE_DIR}
)

if(WEBSOCKETPP_INCLUDE_DIR)
    target_include_directories(zasr-server PRIVATE ${WEBSOCKETPP_INCLUDE_DIR})
elseif(WEBSOCKETPP_INCLUDE_DIRS)
    target_include_directories(zasr-server PRIVATE ${WEBSOCKETPP_INCLUDE_DIRS})
endif()

if(NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(zasr-server PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
elseif(NLOHMANN_JSON_INCLUDE_DIRS)
    target_include_directories(zasr-server PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()

if(YAML_CPP_INCLUDE_DIR)
    target_include_directories(zasr-server PRIVATE ${YAML_CPP_INCLUDE_DIR})
endif()

# Link libraries - use sherpa-onnx all required libraries with link group
target_link_libraries(zasr-server
  "-Wl,--start-group"
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-core.a
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-cxx-api.a
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-c-api.a
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-fst.a
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-fstfar.a
  ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-kaldifst-core.a
  ${SHERPA_ONNX_LIB_DIR}/libkaldi-decoder-core.a
  ${SHERPA_ONNX_LIB_DIR}/libkaldi-native-fbank-core.a
  ${SHERPA_ONNX_LIB_DIR}/libpiper_phonemize.a
  ${SHERPA_ONNX_LIB_DIR}/libespeak-ng.a
  ${SHERPA_ONNX_LIB_DIR}/libucd.a
  ${SHERPA_ONNX_LIB_DIR}/libkissfft-float.a
  ${SHERPA_ONNX_LIB_DIR}/libssentencepiece_core.a
  ${SHERPA_ONNX_LIB_DIR}/libcargs.a
  ${ONNX_RUNTIME_LIB_DIR}/libonnxruntime.a
  "-Wl,--end-group"
  Threads::Threads
  ${CMAKE_DL_LIBS}
  stdc++
  m
  rt
)

# Add standalone asio and websocketpp definitions
target_compile_definitions(zasr-server PRIVATE
  ASIO_STANDALONE
  _WEBSOCKETPP_CPP11_STL_
)

# Install target
install(TARGETS zasr-server DESTINATION bin)
