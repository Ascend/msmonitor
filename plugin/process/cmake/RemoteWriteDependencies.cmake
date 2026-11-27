include(FetchContent)

function(msmonitor_add_remote_write_dependencies)
    # Snappy's minimum CMake version predates CMP0077.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    # Build the wheel against static, PIC dependencies using the CI toolchain.
    if(NOT OPENSSL_ROOT_DIR)
        if(DEFINED ENV{DEPS_PREFIX})
            set(OPENSSL_ROOT_DIR "$ENV{DEPS_PREFIX}")
        elseif(IS_DIRECTORY "/opt/gcc11-glibc2.17-deps")
            set(OPENSSL_ROOT_DIR "/opt/gcc11-glibc2.17-deps")
        endif()
    endif()
    set(OPENSSL_USE_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_USE_OPENSSL ON)
    set(CURL_CA_FALLBACK ON)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_DISABLE_LDAP ON)
    set(USE_LIBIDN2 OFF)
    set(USE_NGHTTP2 OFF)
    set(CURL_ZLIB OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    FetchContent_Declare(msmonitor_curl
        URL https://curl.se/download/curl-8.23.0.tar.xz
        URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5)

    set(SNAPPY_BUILD_TESTS OFF)
    set(SNAPPY_BUILD_BENCHMARKS OFF)
    set(SNAPPY_INSTALL OFF)
    FetchContent_Declare(msmonitor_snappy
        URL https://codeload.github.com/google/snappy/tar.gz/refs/tags/1.2.2
        URL_HASH SHA256=90f74bc1fbf78a6c56b3c4a082a05103b3a56bb17bca1a27e052ea11723292dc)
    FetchContent_MakeAvailable(msmonitor_curl msmonitor_snappy)
endfunction()
