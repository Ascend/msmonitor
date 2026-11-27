add_process_ut(test_StepTraceExporter ${sqlite3_SOURCES})
target_link_libraries(test_StepTraceExporter PRIVATE msmonitor_trace ${CMAKE_DL_LIBS})
