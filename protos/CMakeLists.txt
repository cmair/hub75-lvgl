cmake_minimum_required(VERSION 3.13)

include(FetchContent)

FetchContent_Declare(
  nanopb
  GIT_REPOSITORY https://github.com/nanopb/nanopb
  GIT_TAG        dev_1.0
  FIND_PACKAGE_ARGS
)

FetchContent_MakeAvailable(nanopb)

include(${nanopb_SOURCE_DIR}/extra/FindNanopb.cmake)
find_package(Nanopb REQUIRED)

NANOPB_GENERATE_CPP(TARGET proto RELPATH proto
	panel.proto
)

target_link_libraries(proto)
