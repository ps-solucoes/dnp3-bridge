# TODO: Bump to cmake_minimum_required(VERSION 4.0) when available
cmake_minimum_required(VERSION 3.28)
project(dnp3-bridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies — system packages
# ---------------------------------------------------------------------------
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_program(GRPC_CPP_PLUGIN_PROGRAM grpc_cpp_plugin REQUIRED)

# ---------------------------------------------------------------------------
# Dependencies — FetchContent (opendnp3)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    opendnp3
    GIT_REPOSITORY https://github.com/dnp3/opendnp3.git
    GIT_TAG        3.1.2
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)

set(DNP3_DEMOS OFF CACHE BOOL "" FORCE)
set(DNP3_TESTS OFF CACHE BOOL "" FORCE)
set(DNP3_TLS   OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(opendnp3 nlohmann_json doctest)

# opendnp3 3.1.2 bundles an old ASIO incompatible with C++20+.
# Override the standard at the target level to force C++14 for opendnp3.
set_target_properties(opendnp3 PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)
# Also override for the sub-dependencies that opendnp3 pulls in.
foreach(_tgt ser4cpp exe4cpp)
    if(TARGET ${_tgt})
        set_target_properties(${_tgt} PROPERTIES CXX_STANDARD 14)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Proto compilation
# ---------------------------------------------------------------------------
set(PROTO_SRC "${CMAKE_SOURCE_DIR}/proto/dnp3bridge.proto")
set(PROTO_OUT "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_OUT}")

set(PROTO_GENERATED_SRCS
    "${PROTO_OUT}/dnp3bridge.pb.cc"
    "${PROTO_OUT}/dnp3bridge.pb.h"
    "${PROTO_OUT}/dnp3bridge.grpc.pb.cc"
    "${PROTO_OUT}/dnp3bridge.grpc.pb.h"
)

add_custom_command(
    OUTPUT ${PROTO_GENERATED_SRCS}
    COMMAND protobuf::protoc
        --proto_path=${CMAKE_SOURCE_DIR}/proto
        --cpp_out=${PROTO_OUT}
        --grpc_out=${PROTO_OUT}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_PROGRAM}
        ${PROTO_SRC}
    DEPENDS ${PROTO_SRC}
    COMMENT "Generating protobuf/gRPC sources from dnp3bridge.proto"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Executable target
# ---------------------------------------------------------------------------
add_executable(dnp3-bridge
    src/main.cpp
    src/config/ConfigLoader.cpp
    src/grpc/BridgeServiceImpl.cpp
    src/grpc/GrpcServer.cpp
    src/dnp3/OutstationManager.cpp
    src/dnp3/PointMap.cpp
    src/dnp3/CommandDispatcher.cpp
    src/dnp3/ForwardingCommandHandler.cpp
    src/bridge/Bridge.cpp
    ${PROTO_GENERATED_SRCS}
)

target_include_directories(dnp3-bridge PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${PROTO_OUT}"
)

target_link_libraries(dnp3-bridge PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    opendnp3
    nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(dnp3-bridge-tests
    tests/test_main.cpp
    tests/test_command_dispatcher.cpp
    tests/test_config_loader.cpp
    tests/test_bridge.cpp
    tests/test_point_map.cpp
    # Compile source files under test (excluding main.cpp which has its own main)
    src/config/ConfigLoader.cpp
    src/dnp3/CommandDispatcher.cpp
    src/dnp3/PointMap.cpp
    src/dnp3/OutstationManager.cpp
    src/dnp3/ForwardingCommandHandler.cpp
    src/bridge/Bridge.cpp
    src/grpc/BridgeServiceImpl.cpp
    src/grpc/GrpcServer.cpp
    ${PROTO_GENERATED_SRCS}
)

target_include_directories(dnp3-bridge-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${PROTO_OUT}"
)

target_link_libraries(dnp3-bridge-tests PRIVATE
    doctest::doctest
    gRPC::grpc++
    protobuf::libprotobuf
    opendnp3
    nlohmann_json::nlohmann_json
)

add_test(NAME unit-tests COMMAND dnp3-bridge-tests)
