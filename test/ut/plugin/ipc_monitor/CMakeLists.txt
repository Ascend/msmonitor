cmake_minimum_required(VERSION 3.16)

option(ENABLE_UT_COVERAGE "Instrument C++ UTs for gcov/lcov coverage" OFF)
if(ENABLE_UT_COVERAGE AND
   (NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    message(FATAL_ERROR "ENABLE_UT_COVERAGE requires GCC and a matching gcov")
endif()

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

set(IPC_DIR "$ENV{MSMONITOR_TOP_DIR}/plugin/ipc_monitor")
file(GLOB SECUREC_SOURCES "$ENV{MSMONITOR_TOP_DIR}/plugin/third_party/securec/src/*.c")
file(GLOB IPC_RUNTIME_SOURCES
    "$ENV{MSMONITOR_TOP_DIR}/plugin/stub/*.cpp"
    "${IPC_DIR}/*.cpp"
    "${IPC_DIR}/*/*.cpp"
)
list(APPEND IPC_RUNTIME_SOURCES ${SECUREC_SOURCES} ${sqlite3_SOURCES})
set(IPC_UTIL_SOURCES "${IPC_DIR}/utils/utils.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")

function(add_ipc_ut TEST_NAME)
    add_executable(${TEST_NAME} "${TEST_NAME}.cpp" ${ARGN})

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

    if(ENABLE_UT_COVERAGE)
        target_compile_options(${TEST_NAME} PRIVATE
            --coverage -O0 -g -fprofile-abs-path -fprofile-update=atomic
        )
        target_link_options(${TEST_NAME} PRIVATE --coverage)
    endif()

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

endfunction()

# Register each UT and its dependencies in one place. Monitor and metric tests
# share a runtime source group because these modules depend on one another.
add_ipc_ut(test_DynoLogNpuMonitor ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_RingBuffer)
add_ipc_ut(test_RotateLogger "${IPC_DIR}/jsonl/RotateLogger.cpp" ${IPC_UTIL_SOURCES})
add_ipc_ut(test_TimerTask)
add_ipc_ut(test_DBConnection "${IPC_DIR}/db/Connection.cpp" ${sqlite3_SOURCES})
add_ipc_ut(test_DcmiRingBuffer)
add_ipc_ut(test_DcmiApiLoader "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_DcmiMetricRegistry "${IPC_DIR}/dcmi/DcmiMetricRegistry.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_DcmiCollector ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_DcmiMonitorSession ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_NpuIpcClient "${IPC_DIR}/NpuIpcClient.cpp" ${IPC_UTIL_SOURCES} ${SECUREC_SOURCES})
add_ipc_ut(test_MetricProcessBase ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_MetricManager ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_InputParser "${IPC_DIR}/InputParser.cpp" "${IPC_DIR}/utils/utils.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_DBRunner "${IPC_DIR}/db/DBRunner.cpp" "${IPC_DIR}/db/Connection.cpp" ${sqlite3_SOURCES}
           "${IPC_DIR}/utils/utils.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_DataBase "${IPC_DIR}/db/DataBase.cpp")
add_ipc_ut(test_JsonlDataDumper "${IPC_DIR}/jsonl/JsonlDataDumper.cpp" "${IPC_DIR}/jsonl/RotateLogger.cpp"
           "${IPC_DIR}/utils/utils.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_DcmiMetrics "${IPC_DIR}/dcmi/DcmiApiLoader.cpp" "${IPC_DIR}/utils/utils.cpp")
add_ipc_ut(test_Utils "${IPC_DIR}/utils/utils.cpp" "${IPC_DIR}/dcmi/DcmiApiLoader.cpp")
add_ipc_ut(test_MetricActivity ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_DBProcessManager ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_JsonlProcessManager ${IPC_RUNTIME_SOURCES})
add_ipc_ut(test_MonitorProcessManager ${IPC_RUNTIME_SOURCES})
