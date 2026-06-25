cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ENV{MSMONITOR_TOP_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_MODULE_PATH "$ENV{MSMONITOR_TOP_DIR}/test/cmake")

set(PACKAGE "ut" CACHE STRING "Package to build")
set(MODE "all" CACHE STRING "Mode to build")

if(PACKAGE STREQUAL "ut")

    enable_testing()

    include(utils)
    find_package(glog MODULE REQUIRED)
    find_package(googletest MODULE REQUIRED)
    find_package(nlohmannjson MODULE REQUIRED)
    find_package(sqlite3 MODULE REQUIRED)
    find_package(boost MODULE REQUIRED)
    find_package(mockcpp MODULE REQUIRED)

    if(MODE STREQUAL "plugin" OR MODE STREQUAL "all")
        add_subdirectory(ut/plugin/ipc_monitor)
    endif()

endif()
