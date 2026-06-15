# Generate Python protobuf bindings for panel.proto and nanopb.proto

# Find protoc compiler
find_program(PROTOC protoc REQUIRED)

# Set paths
set(NANOPB_PROTO_DIR "${CMAKE_SOURCE_DIR}/build/_deps/nanopb-src/generator/proto")
set(PANEL_PROTO_DIR "${CMAKE_SOURCE_DIR}/protos")
set(PYTHON_OUT_DIR "${CMAKE_SOURCE_DIR}/util")

# Generate nanopb_pb2.py
add_custom_command(
    OUTPUT "${PYTHON_OUT_DIR}/nanopb_pb2.py"
    COMMAND ${PROTOC}
        --proto_path=${NANOPB_PROTO_DIR}
        --python_out=${PYTHON_OUT_DIR}
        ${NANOPB_PROTO_DIR}/nanopb.proto
    DEPENDS ${NANOPB_PROTO_DIR}/nanopb.proto
    COMMENT "Generating nanopb_pb2.py"
    VERBATIM
)

# Generate panel_pb2.py
add_custom_command(
    OUTPUT "${PYTHON_OUT_DIR}/panel_pb2.py"
    COMMAND ${PROTOC}
        --proto_path=${PANEL_PROTO_DIR}
        --proto_path=${NANOPB_PROTO_DIR}
        --python_out=${PYTHON_OUT_DIR}
        ${PANEL_PROTO_DIR}/panel.proto
    DEPENDS ${PANEL_PROTO_DIR}/panel.proto "${PYTHON_OUT_DIR}/nanopb_pb2.py"
    COMMENT "Generating panel_pb2.py"
    VERBATIM
)

# Create a custom target to ensure protobuf files are generated
add_custom_target(protobuf_python ALL
    DEPENDS
        "${PYTHON_OUT_DIR}/nanopb_pb2.py"
        "${PYTHON_OUT_DIR}/panel_pb2.py"
)


# Define path to partition definition files
set(PARTITION_JSON "${CMAKE_CURRENT_SOURCE_DIR}/partitions.json")
set(PARTITION_UF2  "${CMAKE_CURRENT_BINARY_DIR}/partitions.uf2")

add_custom_command(
    OUTPUT ${PARTITION_UF2}
    COMMAND picotool partition create ${PARTITION_JSON} ${PARTITION_UF2}
    DEPENDS ${PARTITION_JSON}
    COMMENT "Creating partition table: partitions.uf2"
)

add_custom_target(generate_partitions ALL DEPENDS ${PARTITION_UF2})
