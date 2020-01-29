cmake_minimum_required(VERSION 3.10)
project(llvm_ss)
SET(CMAKE_CXX_STANDARD 14)

SET(CMAKE_C_FLAGS_DEBUG          "-g")
SET(CMAKE_CXX_FLAGS_DEBUG          "-g")

find_package(LLVM REQUIRED CONFIG)
set(source_files toy.cpp toy.h)

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "The C comiler: ${CMAKE_C_COMPILER}")
message(STATUS "The C++ compiler: ${CMAKE_CXX_COMPILER}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs support core irreader ScalarOpts)
add_executable(toy ${source_files})
target_link_libraries(toy ${llvm_libs})
