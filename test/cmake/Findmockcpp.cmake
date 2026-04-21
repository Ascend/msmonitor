set(PKG_NAME mockcpp)
set(DOWNLOAD_PATH "$ENV{MSMONITOR_TOP_DIR}/test/third_party")
set(GIT_TAG "ascend_mindstudio_mockcpp_master")
set(DIR_NAME "${DOWNLOAD_PATH}/mockcpp")

if (NOT ${PKG_NAME}_FOUND)

download_opensource_pkg(${PKG_NAME}
    GIT_TAG ${GIT_TAG}
    DOWNLOAD_PATH ${DOWNLOAD_PATH}
)

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake -S . -B build -G "Unix Makefiles" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=${DIR_NAME}/install -DCMAKE_INSTALL_LIBDIR=${DIR_NAME}/install/lib -DBUILD_32_BIT_TARGET_BY_64_BIT_COMPILER=OFF
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build mockcpp. ${RESULT}")
endif()

execute_process(
    WORKING_DIRECTORY ${DIR_NAME}
    COMMAND cmake --build build --target install --parallel 8
    RESULT_VARIABLE RESULT
)
if (NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build mockcpp. ${RESULT}")
endif()

file(GLOB MOCKCPP_LIB "${DIR_NAME}/install/lib/libmockcpp.a")
if (NOT MOCKCPP_LIB)
    message(FATAL_ERROR "Failed to build mockcpp.")
endif()

set(${PKG_NAME}_LIBRARIES ${MOCKCPP_LIB})
include_directories(${DIR_NAME}/install/include)
set(${PKG_NAME}_FOUND TRUE)

endif()
