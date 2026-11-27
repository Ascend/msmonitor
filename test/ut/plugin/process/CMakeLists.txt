set(MSMONITOR_BUILD_TRACE ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../plugin/process" process_core)

# Threads::Threads 是 IMPORTED target，只对 find_package 所在目录及其子目录可见，
# process 子目录里的 find_package 传不到本目录，需要在 UT 目录再查找一次。
find_package(Threads REQUIRED)

function(add_process_ut TEST_NAME)
    add_executable(${TEST_NAME} "${TEST_NAME}.cpp" ${ARGN})

    # msmonitor_process_core 以 PUBLIC 方式带出 process 头文件目录、Threads 与 glog
    target_link_libraries(${TEST_NAME} PRIVATE
        msmonitor_process_core
        ${googletest_LIBRARIES}
        Threads::Threads
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 15)

    if(ENABLE_UT_COVERAGE)
        target_compile_options(${TEST_NAME} PRIVATE
            --coverage -O0 -g -fprofile-abs-path -fprofile-update=atomic
        )
        target_link_options(${TEST_NAME} PRIVATE --coverage)
    endif()
endfunction()

if(ENABLE_UT_COVERAGE)
    target_compile_options(msmonitor_process_core PRIVATE
        --coverage -O0 -g -fprofile-abs-path -fprofile-update=atomic
    )
endif()

# Processor/JsonlReader 的 UT 链接静态库；Rank 文件名解析只用到头文件。
add_process_ut(test_Processor ${sqlite3_SOURCES})
target_link_libraries(test_Processor PRIVATE msmonitor_trace ${CMAKE_DL_LIBS})
add_process_ut(test_ParseRankFileName)
add_subdirectory(metric)
add_subdirectory(trace)
