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

add_executable(test_DynologUtils
    test_DynologUtils.cpp
    ${MSMONITOR_DYNOLOG_DIR}/dynolog/src/utils.cpp
)

target_include_directories(test_DynologUtils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${MSMONITOR_DYNOLOG_DIR}
)

target_link_libraries(test_DynologUtils PRIVATE
    pthread
    ${glog_LIBRARIES}
    ${googletest_LIBRARIES}
    ${fmt_LIBRARIES}
)

add_test(NAME test_DynologUtils COMMAND test_DynologUtils)

target_compile_options(test_DynologUtils PRIVATE
    -fPIC
    -fstack-protector-all
    -ftrapv
)

target_link_options(test_DynologUtils PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
)

find_package(OpenSSL REQUIRED)

add_executable(test_SimpleJsonServerTls
    test_SimpleJsonServerTls.cpp
    ${MSMONITOR_DYNOLOG_DIR}/dynolog/src/rpc/SimpleJsonServer.cpp
    ${MSMONITOR_DYNOLOG_DIR}/dynolog/src/utils.cpp
)

target_include_directories(test_SimpleJsonServerTls PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${MSMONITOR_DYNOLOG_DIR}
)

target_link_libraries(test_SimpleJsonServerTls PRIVATE
    pthread
    ${glog_LIBRARIES}
    ${googletest_LIBRARIES}
    ${fmt_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_test(NAME test_SimpleJsonServerTls COMMAND test_SimpleJsonServerTls)

target_compile_options(test_SimpleJsonServerTls PRIVATE
    -fPIC
    -fstack-protector-all
    -ftrapv
)

target_link_options(test_SimpleJsonServerTls PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
)
