cmake_minimum_required(VERSION 3.16)
project(msmonitor_process LANGUAGES CXX)
find_package(Threads REQUIRED)
# Reuse the collector glog target/libraries, or find an installed glog package.
if(NOT TARGET glog::glog AND NOT glog_LIBRARIES)
    find_package(glog CONFIG REQUIRED)
endif()

add_library(msmonitor_process_core STATIC Processor.cpp JsonlReader.cpp)
# 静态库会被链接进共享对象（如 IPCMonitor_C.so），必须位置无关，
# 否则 aarch64 链接时报 "dangerous relocation: unsupported relocation"。
set_target_properties(msmonitor_process_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(msmonitor_process_core PUBLIC cxx_std_17)
target_include_directories(msmonitor_process_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(msmonitor_process_core PUBLIC Threads::Threads)
if(TARGET glog::glog)
    target_link_libraries(msmonitor_process_core PUBLIC glog::glog)
else()
    target_link_libraries(msmonitor_process_core PUBLIC ${glog_LIBRARIES})
endif()
