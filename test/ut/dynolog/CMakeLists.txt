cmake_minimum_required(VERSION 3.16)

set(MSMONITOR_DYNOLOG_DIR "$ENV{MSMONITOR_TOP_DIR}/dynolog_npu")

add_executable(test_LibkinetoConfigManager
    test_LibkinetoConfigManager.cpp
    ${MSMONITOR_DYNOLOG_DIR}/dynolog/src/LibkinetoConfigManager.cpp
)

target_include_directories(test_LibkinetoConfigManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${MSMONITOR_DYNOLOG_DIR}
)

target_link_libraries(test_LibkinetoConfigManager PRIVATE
    pthread
    ${glog_LIBRARIES}
    ${googletest_LIBRARIES}
    ${fmt_LIBRARIES}
)

add_test(NAME test_LibkinetoConfigManager COMMAND test_LibkinetoConfigManager)

target_compile_options(test_LibkinetoConfigManager PRIVATE
    -fPIC
    -fstack-protector-all
    -ftrapv
)

target_link_options(test_LibkinetoConfigManager PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
)
