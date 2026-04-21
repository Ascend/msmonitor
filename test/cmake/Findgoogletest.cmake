set(PKG_NAME googletest)
set(DOWNLOAD_PATH "$ENV{MSMONITOR_TOP_DIR}/test/third_party")
set(GIT_TAG "v1.12.x")
set(DIR_NAME "${DOWNLOAD_PATH}/googletest")

if (NOT ${PKG_NAME}_FOUND)

download_opensource_pkg(${PKG_NAME}
    GIT_TAG ${GIT_TAG}
    DOWNLOAD_PATH ${DOWNLOAD_PATH}
)

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake -S . -B build -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=${DIR_NAME}/install -DCMAKE_INSTALL_LIBDIR=${DIR_NAME}/install/lib64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build googletest. ${RESULT}")
endif()

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake --build build --target install --parallel 8
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build googletest. ${RESULT}")
endif()

file(GLOB GTEST_LIB "${DIR_NAME}/install/lib64/libgtest.a")
if (NOT GTEST_LIB)
    message(FATAL_ERROR "Failed to build googletest.")
endif()

set(${PKG_NAME}_LIBRARIES ${GTEST_LIB})
include_directories(${DIR_NAME}/install/include)
set(${PKG_NAME}_FOUND TRUE)

endif()
