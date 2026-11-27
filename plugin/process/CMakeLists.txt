cmake_minimum_required(VERSION 3.16)
project(msmonitor_process LANGUAGES CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)

option(MSMONITOR_BUILD_PROCESS_RUNNER "Build the complete process runtime" OFF)
option(MSMONITOR_BUILD_REMOTE_WRITE "Build the HTTP Remote Write transport" OFF)
option(MSMONITOR_BUILD_TRACE "Build Chrome Trace and MindStudio DB export" OFF)

# Reuse the collector glog target/libraries, or find an installed glog package.
if(NOT TARGET glog::glog AND NOT glog_LIBRARIES)
    find_package(glog CONFIG REQUIRED)
endif()

add_library(msmonitor_process_core STATIC Processor.cpp JsonlReader.cpp StepDataAnalyser.cpp StepAnalysis.cpp
    metric/DurationStats.cpp metric/StepMetricExporter.cpp)
target_compile_features(msmonitor_process_core PUBLIC cxx_std_17)
target_include_directories(msmonitor_process_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(msmonitor_process_core PUBLIC Threads::Threads)
if(TARGET glog::glog)
    target_link_libraries(msmonitor_process_core PUBLIC glog::glog)
else()
    target_link_libraries(msmonitor_process_core PUBLIC ${glog_LIBRARIES})
endif()

if(NOT TARGET nlohmann_json::nlohmann_json AND NOT nlohmannjson_FOUND)
    find_package(nlohmann_json 3.12.0 CONFIG REQUIRED)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(msmonitor_process_core PUBLIC nlohmann_json::nlohmann_json)
endif()

if(MSMONITOR_BUILD_PROCESS_RUNNER OR MSMONITOR_BUILD_REMOTE_WRITE)
    add_library(msmonitor_remote_write STATIC metric/PrometheusRemoteWrite.cpp metric/StepMetricReporter.cpp)
    if(MSMONITOR_BUILD_PROCESS_RUNNER)
        include(cmake/RemoteWriteDependencies.cmake)
        msmonitor_add_remote_write_dependencies()
        target_link_libraries(msmonitor_remote_write PRIVATE CURL::libcurl snappy)
    else()
        find_package(CURL REQUIRED)
        find_path(SNAPPY_INCLUDE_DIR snappy.h REQUIRED)
        find_library(SNAPPY_LIBRARY snappy REQUIRED)
        target_include_directories(msmonitor_remote_write PRIVATE "${SNAPPY_INCLUDE_DIR}")
        target_link_libraries(msmonitor_remote_write PRIVATE CURL::libcurl ${SNAPPY_LIBRARY})
    endif()
    target_link_libraries(msmonitor_remote_write PUBLIC msmonitor_process_core)
endif()

if(MSMONITOR_BUILD_PROCESS_RUNNER OR MSMONITOR_BUILD_TRACE)
    set(MSMONITOR_SQLITE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite-amalgamation-3500300/sqlite3.c"
        CACHE FILEPATH "SQLite amalgamation for the trace exporter")
    add_library(msmonitor_trace STATIC trace/StepTraceExporter.cpp trace/ChromeTraceWriter.cpp
        trace/MindStudioDbWriter.cpp ../ipc_monitor/db/DataBase.cpp)
    target_include_directories(msmonitor_trace PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ipc_monitor")
    target_link_libraries(msmonitor_trace PUBLIC msmonitor_process_core)

    # The wheel and repository tests already compile SQLite in their final target.
    if(NOT sqlite3_SOURCES AND EXISTS "${MSMONITOR_SQLITE_SOURCE}")
        enable_language(C)
        target_sources(msmonitor_trace PRIVATE "${MSMONITOR_SQLITE_SOURCE}")
        get_filename_component(MSMONITOR_SQLITE_INCLUDE "${MSMONITOR_SQLITE_SOURCE}" DIRECTORY)
        target_include_directories(msmonitor_trace PRIVATE "${MSMONITOR_SQLITE_INCLUDE}")
        set_source_files_properties("${MSMONITOR_SQLITE_SOURCE}" PROPERTIES COMPILE_DEFINITIONS SQLITE_DEFAULT_MEMSTATUS=0)
        target_link_libraries(msmonitor_trace PRIVATE ${CMAKE_DL_LIBS})
    elseif(NOT sqlite3_SOURCES)
        find_package(SQLite3 REQUIRED)
        target_link_libraries(msmonitor_trace PRIVATE SQLite::SQLite3)
    endif()
endif()

if(MSMONITOR_BUILD_PROCESS_RUNNER)
    add_library(msmonitor_process STATIC ProcessRunner.cpp)
    target_compile_options(msmonitor_process PRIVATE -fstack-protector-all -ftrapv)
    target_link_libraries(msmonitor_process
        PUBLIC msmonitor_process_core
        PRIVATE msmonitor_trace msmonitor_remote_write)
endif()
