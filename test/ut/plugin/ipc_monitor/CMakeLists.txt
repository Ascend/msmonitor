cmake_minimum_required(VERSION 3.16)

set(TEST_SOURCES
    test_DynoLogNpuMonitor.cpp
    test_RingBuffer.cpp
    test_RotateLogger.cpp
    test_TimerTask.cpp
    test_DBConnection.cpp
)

find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development)
include_directories(${Python_INCLUDE_DIRS})

execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE PYBIND11_GET_DIR_RESULT
)

if(NOT PYBIND11_GET_DIR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to get pybind11 CMake directory. Python error code: ${PYBIND11_GET_DIR_RESULT}")
endif()

set(pybind11_DIR "${PYBIND11_CMAKE_DIR}")
find_package(pybind11 REQUIRED)

file(GLOB IPC_SOURCES
    $ENV{MSMONITOR_TOP_DIR}/plugin/stub/*.cpp
    $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor/*.cpp
    $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor/*/*.cpp
    $ENV{MSMONITOR_TOP_DIR}/plugin/third_party/securec/src/*.c
    $ENV{MSMONITOR_TOP_DIR}/test/third_party/sqlite-amalgamation-3500300/sqlite3.c
)

foreach(TEST_SOURCE ${TEST_SOURCES})

    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})

    target_sources(${TEST_NAME} PRIVATE ${IPC_SOURCES})

    target_link_libraries(${TEST_NAME} PRIVATE
        dl
        pthread
        pybind11::module
        ${Python_LIBRARIES}
        ${glog_LIBRARIES}
        ${googletest_LIBRARIES}
        ${mockcpp_LIBRARIES}
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    target_compile_options(${TEST_NAME} PRIVATE
        -fPIC
        -fstack-protector-all
        -ftrapv
    )

    target_link_options(${TEST_NAME} PRIVATE
        -Wl,-z,relro,-z,now,-z,noexecstack
    )

    target_include_directories(${TEST_NAME} PRIVATE
        $ENV{MSMONITOR_TOP_DIR}
        $ENV{MSMONITOR_TOP_DIR}/plugin
        $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor
        $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor/utils
        $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor/metric
        $ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor/mspti_monitor
        $ENV{MSMONITOR_TOP_DIR}/plugin/third_party/securec/include
        $ENV{MSMONITOR_TOP_DIR}/plugin/third_party/nlohmann-json/include
    )

endforeach()
