set(PKG_NAME boost)
set(DOWNLOAD_PATH "$ENV{MSMONITOR_TOP_DIR}/test/third_party")
set(BOOST_DIR_NAME "${DOWNLOAD_PATH}/boost_1_81_0")

if (NOT ${PKG_NAME}_FOUND)

download_opensource_pkg(${PKG_NAME}
    DOWNLOAD_PATH ${DOWNLOAD_PATH}
)

if (NOT EXISTS ${BOOST_DIR_NAME})
    message(FATAL_ERROR "Failed to download boost.")
endif()

set(BOOST_INCLUDE_DIRS ${BOOST_DIR_NAME})
include_directories(${BOOST_INCLUDE_DIRS})
set(${PKG_NAME}_FOUND TRUE)

endif()
