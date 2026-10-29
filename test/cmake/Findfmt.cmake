set(PKG_NAME fmt)
set(DOWNLOAD_PATH "$ENV{MSMONITOR_TOP_DIR}/test/third_party")
set(GIT_TAG "7.1.3")
set(DIR_NAME "${DOWNLOAD_PATH}/fmt")

if (NOT ${PKG_NAME}_FOUND)

download_opensource_pkg(${PKG_NAME}
    GIT_TAG ${GIT_TAG}
    DOWNLOAD_PATH ${DOWNLOAD_PATH}
)

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake -S . -B build -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=${DIR_NAME}/install -DCMAKE_INSTALL_LIBDIR=lib64 -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build fmt. ${RESULT}")
endif()

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake --build build --target install --parallel 8
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build fmt. ${RESULT}")
endif()

file(GLOB FMT_LIB "${DIR_NAME}/install/lib64/libfmt.a")
if (NOT FMT_LIB)
    message(FATAL_ERROR "Failed to build fmt.")
endif()

set(${PKG_NAME}_LIBRARIES ${FMT_LIB})
include_directories(${DIR_NAME}/install/include)
set(${PKG_NAME}_FOUND TRUE)

endif()
