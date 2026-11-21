add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../plugin/process" process_core)

add_executable(test_Processor test_Processor.cpp)
target_link_libraries(test_Processor PRIVATE msmonitor_process_core ${googletest_LIBRARIES})
add_test(NAME test_Processor COMMAND test_Processor)
set_tests_properties(test_Processor PROPERTIES TIMEOUT 15)
