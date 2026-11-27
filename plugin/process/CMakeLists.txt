cmake_minimum_required(VERSION 3.16)
project(msmonitor_process LANGUAGES CXX)
find_package(Threads REQUIRED)
# Reuse the collector glog target/libraries, or find an installed glog package.
if(NOT TARGET glog::glog AND NOT glog_LIBRARIES)
    find_package(glog CONFIG REQUIRED)
endif()

add_library(msmonitor_process_core STATIC Processor.cpp JsonlReader.cpp StepDataAnalyser.cpp StepAnalysis.cpp
    metric/DurationStats.cpp metric/StepMetricExporter.cpp)
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

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(msmonitor_process_core PUBLIC nlohmann_json::nlohmann_json)
elseif(nlohmannjson_FOUND)
    # The parent Findnlohmannjson module already provides the include directory.
else()
    find_package(nlohmann_json 3.12.0 CONFIG REQUIRED)
    target_link_libraries(msmonitor_process_core PUBLIC nlohmann_json::nlohmann_json)
endif()

option(MSMONITOR_BUILD_REMOTE_WRITE "Build the HTTP Remote Write transport" OFF)
if(MSMONITOR_BUILD_REMOTE_WRITE)
    find_package(CURL REQUIRED)
    find_path(SNAPPY_INCLUDE_DIR snappy.h REQUIRED)
    find_library(SNAPPY_LIBRARY snappy REQUIRED)
    add_library(msmonitor_remote_write STATIC metric/PrometheusRemoteWrite.cpp metric/StepMetricReporter.cpp)
    set_target_properties(msmonitor_remote_write PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(msmonitor_remote_write PUBLIC cxx_std_17)
    target_include_directories(msmonitor_remote_write PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${SNAPPY_INCLUDE_DIR}")
    target_link_libraries(msmonitor_remote_write PUBLIC msmonitor_process_core PRIVATE CURL::libcurl ${SNAPPY_LIBRARY})
endif()

option(MSMONITOR_BUILD_TRACE "Build Chrome Trace and MindStudio DB export" OFF)
if(MSMONITOR_BUILD_TRACE)
    set(MSMONITOR_SQLITE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite-amalgamation-3500300/sqlite3.c"
        CACHE FILEPATH "SQLite amalgamation for the trace exporter")
    if(NOT sqlite3_SOURCES AND EXISTS "${MSMONITOR_SQLITE_SOURCE}")
        enable_language(C)
        add_library(msmonitor_trace_sqlite STATIC "${MSMONITOR_SQLITE_SOURCE}")
        get_filename_component(MSMONITOR_SQLITE_INCLUDE "${MSMONITOR_SQLITE_SOURCE}" DIRECTORY)
        target_include_directories(msmonitor_trace_sqlite PUBLIC "${MSMONITOR_SQLITE_INCLUDE}")
        target_compile_definitions(msmonitor_trace_sqlite PRIVATE SQLITE_DEFAULT_MEMSTATUS=0)
        set_target_properties(msmonitor_trace_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(msmonitor_trace_sqlite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        set(MSMONITOR_TRACE_SQLITE_TARGET msmonitor_trace_sqlite)
    elseif(NOT sqlite3_SOURCES)
        find_package(SQLite3 REQUIRED)
        set(MSMONITOR_TRACE_SQLITE_TARGET SQLite::SQLite3)
    endif()
    add_library(msmonitor_trace STATIC trace/StepTraceExporter.cpp trace/ChromeTraceWriter.cpp
        trace/MindStudioDbWriter.cpp ../ipc_monitor/db/DataBase.cpp)
    set_target_properties(msmonitor_trace PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(msmonitor_trace PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ipc_monitor")
    target_link_libraries(msmonitor_trace PUBLIC msmonitor_process_core
        PRIVATE ${MSMONITOR_TRACE_SQLITE_TARGET})
endif()
