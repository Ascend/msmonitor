/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2025 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * -------------------------------------------------------------------------
 */
// Copyright (c) Meta Platforms Inc. and affiliates
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "dynolog/src/rpc/SimpleJsonServer.h"

#include <arpa/inet.h>
#include <fmt/format.h>
#include <glog/logging.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <string>

#include "dynolog/src/utils.h"

constexpr int CLIENT_QUEUE_LEN = 50;
constexpr int MAX_MESSAGE_LEN = 2 * 4096;  // twice the maximum linux path length
constexpr int SOCKET_TIMEOUT_SEC = 3;

const std::string NO_CERTS_MODE = "NO_CERTS";
DEFINE_string(certs_dir, NO_CERTS_MODE, "TLS crets dir");

namespace dynolog
{
constexpr size_t MIN_RSA_KEY_LENGTH = 3072;
constexpr char BACKSPACE_ASCII = 8;
constexpr char DEL_ASCII = 127;
constexpr long long SECONDS_PER_DAY = 24LL * 60 * 60;
constexpr long long CRL_WARN_THRESHOLD_SECONDS = 7LL * SECONDS_PER_DAY;
constexpr time_t CRL_WARN_INTERVAL_SECONDS = 60 * 60;

SimpleJsonServerBase::SimpleJsonServerBase(int port) : port_(port)
{
    if (FLAGS_certs_dir != NO_CERTS_MODE && !PathUtils::DirPathCheck(FLAGS_certs_dir))
    {
        throw std::runtime_error("--certs-dir must be a valid directory!");
    }
    try
    {
        initSocket();
        if (FLAGS_certs_dir != NO_CERTS_MODE)
        {
            init_openssl();
            ctx_ = create_context();
            configure_context(ctx_);
        }
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "Failed to initialize server: " << e.what();
        if (sock_fd_ != -1)
        {
            close(sock_fd_);
            sock_fd_ = -1;
        }
        throw;
    }
}

SimpleJsonServerBase::~SimpleJsonServerBase()
{
    if (thread_)
    {
        stop();
    }
    close(sock_fd_);
    if (FLAGS_certs_dir != NO_CERTS_MODE && ctx_)
    {
        SSL_CTX_free(ctx_);
    }
}

void SimpleJsonServerBase::initSocket()
{
    struct sockaddr_in6 server_addr;

    /* Create socket for listening (client requests). */
    sock_fd_ = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (sock_fd_ == -1)
    {
        int error_code = errno;
        std::perror("socket()");
        throw std::runtime_error("socket() failed, error is " + std::string(std::strerror(error_code)));
    }

    /* Set socket to reuse address in case server is restarted. */
    int flag = 1;
    int ret = ::setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    if (ret == -1)
    {
        int error_code = errno;
        std::perror("setsockopt()");
        throw std::runtime_error("setsockopt() failed, error is " + std::string(std::strerror(error_code)));
    }

    // in6addr_any allows us to bind to both IPv4 and IPv6 clients.
    server_addr.sin6_addr = in6addr_any;
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(port_);

    /* Bind address and socket together */
    ret = ::bind(sock_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == -1)
    {
        int error_code = errno;
        std::perror("bind()");
        close(sock_fd_);
        throw std::runtime_error("bind() failed, error is " + std::string(std::strerror(error_code)));
    }

    /* Create listening queue (client requests) */
    ret = ::listen(sock_fd_, CLIENT_QUEUE_LEN);
    if (ret == -1)
    {
        int error_code = errno;
        std::perror("listen()");
        close(sock_fd_);
        throw std::runtime_error("listen() failed, error is " + std::string(std::strerror(error_code)));
    }

    /* Get port if assigned 0 */
    if (port_ == 0)
    {
        LOG(ERROR) << "RPC listening port is set to 0.";
        throw std::runtime_error("RPC listening port is set to 0. Please specify a valid port number!");
    }

    LOG(INFO) << "Listening to connections on port " << port_;
    initSuccess_ = true;
}

/* A simple wrapper to accept connections and read data
 *
 *  Messages are prefixed using the length so we know how long a message
 *  to actually read.
 *     : int32_t len
 *     : char json[]
 */
class ClientSocketWrapper
{
   public:
    ~ClientSocketWrapper()
    {
        if (FLAGS_certs_dir != NO_CERTS_MODE && ssl_)
        {
            int shutdown_ret = SSL_shutdown(ssl_);
            if (shutdown_ret < 0)
            {
                LOG(ERROR) << "SSL_shutdown failed, error code: " << shutdown_ret;
                shutdown_ret = SSL_shutdown(ssl_);
            }
            SSL_free(ssl_);
        }
        if (client_sock_fd_ != -1)
        {
            ::close(client_sock_fd_);
        }
    }

    bool accept(int server_socket, SSL_CTX* ctx)
    {
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        std::array<char, INET6_ADDRSTRLEN> client_addr_str;

        client_sock_fd_ = ::accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock_fd_ == -1)
        {
            std::perror("accept()");
            return false;
        }

        inet_ntop(AF_INET6, &(client_addr.sin6_addr), client_addr_str.data(), client_addr_str.size());
        LOG(INFO) << "Received connection from " << client_addr_str.data();

        // set timeout 3s
        struct timeval tv;
        tv.tv_sec = SOCKET_TIMEOUT_SEC;
        tv.tv_usec = 0;
        if (::setsockopt(client_sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            LOG(ERROR) << "Set client socket timeout failed.";
            return false;
        }

        if (FLAGS_certs_dir == NO_CERTS_MODE)
        {
            LOG(INFO) << "No certs mode";
            return true;
        }

        ssl_ = SSL_new(ctx);
        if (ssl_ == nullptr)
        {
            LOG(ERROR) << "Create SSL failed.";
            return false;
        }
        SSL_set_fd(ssl_, client_sock_fd_);
        if (SSL_accept(ssl_) <= 0)
        {
            ERR_print_errors_fp(stderr);
            return false;
        }
        LOG(INFO) << "SSL handshake success";
        return true;
    }

    std::string get_message()
    {
        int32_t msg_size = -1;

        if (read_helper((uint8_t*)&msg_size, sizeof(msg_size)) <= 0 || msg_size <= 0 || msg_size >= MAX_MESSAGE_LEN)
        {
            LOG(ERROR) << "Invalid message size = " << msg_size;
            return "";
        }

        std::string message;
        message.resize(msg_size);
        int recv = 0;
        int ret = 1;
        // set timeout 3s
        const auto timeout = std::chrono::seconds(SOCKET_TIMEOUT_SEC);
        auto start = std::chrono::steady_clock::now();
        while (recv < msg_size && ret > 0)
        {
            ret = read_helper((uint8_t*)&message[recv], msg_size - recv);
            recv += ret > 0 ? ret : 0;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - start;
            if (elapsed > timeout)
            {
                LOG(WARNING) << "Timeout! Message received size is " << recv;
                break;
            }
        }

        if (recv != msg_size)
        {
            LOG(ERROR) << "Received partial message, expected size " << msg_size << " found : " << recv;
            LOG(ERROR) << "Message received = " << message;
            return "";
        }

        return message;
    }

    bool send_response(const std::string& response)
    {
        int32_t size = response.size();
        int ret;
        if (FLAGS_certs_dir == NO_CERTS_MODE)
        {
            ret = ::write(client_sock_fd_, (void*)&size, sizeof(size));
            if (ret == -1)
            {
                std::perror("write()");
                return false;
            }
        }
        else
        {
            ret = SSL_write(ssl_, (void*)&size, sizeof(size));
            if (ret <= 0)
            {
                ERR_print_errors_fp(stderr);
                return false;
            }
        }
        int sent = 0;
        while (sent < size && ret > 0)
        {
            if (FLAGS_certs_dir == NO_CERTS_MODE)
            {
                ret = ::write(client_sock_fd_, (void*)&response[sent], size - sent);
                if (ret == -1)
                {
                    std::perror("write()");
                }
                else
                {
                    sent += ret;
                }
            }
            else
            {
                ret = SSL_write(ssl_, (void*)&response[sent], size - sent);
                if (ret <= 0)
                {
                    ERR_print_errors_fp(stderr);
                }
                else
                {
                    sent += ret;
                }
            }
        }

        if (sent < response.size())
        {
            LOG(ERROR) << "Unable to write full response";
            return false;
        }
        return ret > 0;
    }

   private:
    int read_helper(uint8_t* buf, int size)
    {
        if (FLAGS_certs_dir == NO_CERTS_MODE)
        {
            int ret = ::recv(client_sock_fd_, buf, size, 0);
            if (ret == -1)
            {
                std::perror("read()");
            }
            return ret;
        }
        int ret = SSL_read(ssl_, (void*)buf, size);
        if (ret <= 0)
        {
            ERR_print_errors_fp(stderr);
        }
        return ret;
    }

    int client_sock_fd_ = -1;
    SSL* ssl_ = nullptr;
};

/* Accepts socket connections and processes the payloads.
 * This will inturn call the Handler functions */
void SimpleJsonServerBase::loop() noexcept
{
    if (sock_fd_ == -1 || !initSuccess_)
    {
        return;
    }

    while (run_)
    {
        processOne();
    }
}

void SimpleJsonServerBase::processOne() noexcept
{
    check_crl_expiry();
    LOG(INFO) << "Waiting for connection.";
    ClientSocketWrapper client;
    if (!client.accept(sock_fd_, ctx_))
    {
        return;
    }
    std::string request_str = client.get_message();
    LOG(INFO) << "RPC message received = " << request_str;
    auto response_str = processOneImpl(request_str);
    if (response_str.empty())
    {
        return;
    }
    if (!client.send_response(response_str))
    {
        LOG(ERROR) << "Failed to send response";
    }
}

void SimpleJsonServerBase::run()
{
    LOG(INFO) << "Launching RPC thread";
    thread_ = std::make_unique<std::thread>([this]() { this->loop(); });
}

void SimpleJsonServerBase::init_openssl()
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    // OpenSSL 1.1.0+ (包括3.0+)
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#else
    // OpenSSL 1.0.x 及更早版本
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
#endif
}

SSL_CTX* SimpleJsonServerBase::create_context()
{
    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("Unable to create SSL context");
    }
    return ctx;
}

static std::string format_asn1_time(const ASN1_TIME* time)
{
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio)
    {
        return "unknown";
    }
    std::string result = "none";
    char* data = nullptr;
    const long len = time && ASN1_TIME_print(bio, time) == 1 ? BIO_get_mem_data(bio, &data) : 0;
    if (len > 0)
    {
        result.assign(data, static_cast<size_t>(len));
    }
    BIO_free(bio);
    return result;
}

static void verify_crl_signature(X509_CRL* crl, X509_STORE* store)
{
    X509_NAME* issuer = X509_CRL_get_issuer(crl);
    X509_STORE_CTX* store_ctx = X509_STORE_CTX_new();
    if (!issuer || !store_ctx)
    {
        X509_STORE_CTX_free(store_ctx);
        throw std::runtime_error("Failed to get CRL issuer or create store context");
    }
    const bool initialized = X509_STORE_CTX_init(store_ctx, store, nullptr, nullptr) == 1;
    STACK_OF(X509)* ca_certs = initialized ? X509_STORE_CTX_get1_certs(store_ctx, issuer) : nullptr;
    X509_STORE_CTX_free(store_ctx);
    if (!ca_certs || sk_X509_num(ca_certs) == 0)
    {
        sk_X509_pop_free(ca_certs, X509_free);
        throw std::runtime_error("No trusted CA certificate found for CRL issuer");
    }

    bool verified = false;
    for (int i = 0; i < sk_X509_num(ca_certs) && !verified; i++)
    {
        EVP_PKEY* pkey = X509_get_pubkey(sk_X509_value(ca_certs, i));
        verified = pkey && X509_CRL_verify(crl, pkey) == 1;
        EVP_PKEY_free(pkey);
    }
    sk_X509_pop_free(ca_certs, X509_free);
    if (!verified)
    {
        throw std::runtime_error("CRL signature verification failed");
    }
}

// 仅支持单 CA：CRL 必须覆盖信任库中的所有 CA，避免多根配置在握手期才失败。
static void verify_crl_covers_trusted_cas(X509_CRL* crl, X509_STORE* store)
{
    X509_NAME* issuer = X509_CRL_get_issuer(crl);
    STACK_OF(X509_OBJECT)* objects = X509_STORE_get0_objects(store);
    if (!issuer || !objects)
    {
        throw std::runtime_error("Failed to inspect trusted CA certificates");
    }

    int trusted_cert_count = 0;
    for (int i = 0; i < sk_X509_OBJECT_num(objects); i++)
    {
        X509_OBJECT* object = sk_X509_OBJECT_value(objects, i);
        if (!object || X509_OBJECT_get_type(object) != X509_LU_X509)
        {
            continue;
        }
        X509* trusted_cert = X509_OBJECT_get0_X509(object);
        if (!trusted_cert)
        {
            continue;
        }
        ++trusted_cert_count;

        bool covered = X509_NAME_cmp(X509_get_subject_name(trusted_cert), issuer) == 0;
        if (covered)
        {
            EVP_PKEY* pkey = X509_get_pubkey(trusted_cert);
            covered = pkey && X509_CRL_verify(crl, pkey) == 1;
            EVP_PKEY_free(pkey);
        }
        if (!covered)
        {
            char* subject = X509_NAME_oneline(X509_get_subject_name(trusted_cert), nullptr, 0);
            const std::string name = subject ? subject : "unknown";
            OPENSSL_free(subject);
            throw std::runtime_error("CA certificate is not covered by CRL: " + name);
        }
    }

    if (trusted_cert_count == 0)
    {
        throw std::runtime_error("No trusted CA certificate found in CA file");
    }
}

static void verify_crl_validity(X509_CRL* crl)
{
    time_t current_time = time(nullptr);
    if (X509_cmp_time(X509_CRL_get0_lastUpdate(crl), &current_time) >= 0)
    {
        throw std::runtime_error("CRL is not yet valid or has invalid thisUpdate");
    }
    const ASN1_TIME* next_update = X509_CRL_get0_nextUpdate(crl);
    if (!next_update)
    {
        LOG(WARNING) << "CRL has no nextUpdate field";
    }
    else if (X509_cmp_time(next_update, &current_time) <= 0)
    {
        throw std::runtime_error("CRL has expired or has invalid nextUpdate");
    }
}

static bool is_cert_revoked(X509* cert, X509_STORE* store)
{
    if (!cert || !store)
    {
        LOG(ERROR) << "Invalid certificate or store pointer";
        return false;
    }
    // 获取证书的颁发者名称
    X509_NAME* issuer = X509_get_issuer_name(cert);
    if (!issuer)
    {
        LOG(ERROR) << "Failed to get certificate issuer";
        return false;
    }
    // 获取证书的序列号
    const ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (!serial)
    {
        LOG(ERROR) << "Failed to get certificate serial number";
        return false;
    }
    // 创建证书验证上下文
    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    if (!ctx)
    {
        LOG(ERROR) << "Failed to create certificate store context";
        return false;
    }
    bool is_revoked = false;
    STACK_OF(X509_CRL)* crls = nullptr;
    try
    {
        // 初始化证书验证上下文
        if (X509_STORE_CTX_init(ctx, store, cert, nullptr) == 0)
        {
            LOG(ERROR) << "Failed to initialize certificate store context";
            X509_STORE_CTX_free(ctx);
            return false;
        }
        // 获取CRL列表
        crls = X509_STORE_CTX_get1_crls(ctx, issuer);
        if (!crls)
        {
            LOG(INFO) << "No CRLs found for issuer";
            X509_STORE_CTX_free(ctx);
            return false;
        }
        time_t current_time = time(nullptr);
        for (int i = 0; i < sk_X509_CRL_num(crls); i++)
        {
            X509_CRL* crl = sk_X509_CRL_value(crls, i);
            if (!crl)
            {
                LOG(ERROR) << "Invalid CRL at index " << i;
                continue;
            }
            // 检查 CRL 的有效期
            const ASN1_TIME* crl_this_update = X509_CRL_get0_lastUpdate(crl);
            const ASN1_TIME* crl_next_update = X509_CRL_get0_nextUpdate(crl);
            if (!crl_this_update)
            {
                LOG(ERROR) << "Failed to get CRL this_update time";
                continue;
            }
            // 检查 CRL 是否已生效
            if (X509_cmp_time(crl_this_update, &current_time) > 0)
            {
                LOG(INFO) << "CRL is not yet valid";
                continue;
            }
            // 检查 CRL 是否过期
            if (crl_next_update)
            {
                if (X509_cmp_time(crl_next_update, &current_time) < 0)
                {
                    LOG(INFO) << "CRL has expired";
                    continue;
                }
            }
            // 检查证书是否在 CRL 中
            STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
            if (revoked)
            {
                for (int j = 0; j < sk_X509_REVOKED_num(revoked); j++)
                {
                    X509_REVOKED* rev = sk_X509_REVOKED_value(revoked, j);
                    if (rev)
                    {
                        const ASN1_INTEGER* rev_serial = X509_REVOKED_get0_serialNumber(rev);
                        if (rev_serial && ASN1_INTEGER_cmp(serial, rev_serial) == 0)
                        {
                            LOG(INFO) << "Certificate is found in CRL";
                            is_revoked = true;
                            break;
                        }
                    }
                }
            }
            if (is_revoked)
            {
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "Exception while checking CRL: " << e.what();
        is_revoked = false;
    }
    if (crls)
    {
        sk_X509_CRL_pop_free(crls, X509_CRL_free);
    }
    X509_STORE_CTX_free(ctx);
    return is_revoked;
}

// 禁用终端回显的函数，但显示星号
int get_password_with_stars(char* buf, size_t bufsize)
{
    char* secure_buf = static_cast<char*>(CRYPTO_secure_malloc(bufsize, __FILE__, __LINE__));
    if (!secure_buf)
    {
        return -1; /* 内存申请失败 */
    }
    struct termios old_flags;
    struct termios new_flags;
    size_t idx = 0;
    if (tcgetattr(fileno(stdin), &old_flags) != 0)
    {
        CRYPTO_secure_free(secure_buf, __FILE__, __LINE__);
        return -1;
    }
    new_flags = old_flags;
    new_flags.c_lflag &= ~ECHO;
    tcsetattr(fileno(stdin), TCSANOW, &new_flags);

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF && idx + 1 < bufsize)
    {
        if (ch == DEL_ASCII || ch == BACKSPACE_ASCII)
        {
            if (idx > 0)
            {
                idx--;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else
        {
            secure_buf[idx++] = static_cast<char>(ch);
            printf("*");
            fflush(stdout);
        }
    }
    secure_buf[idx] = '\0';
    std::copy_n(secure_buf, idx + 1, buf);
    tcsetattr(fileno(stdin), TCSANOW, &old_flags);
    OPENSSL_cleanse(secure_buf, bufsize);
    CRYPTO_secure_free(secure_buf, __FILE__, __LINE__);
    return idx;
}

// 验证证书版本和签名算法
void SimpleJsonServerBase::verify_certificate_version_and_algorithm(X509* cert)
{
    // 1. 检查证书版本是否为 X.509v3
    if (X509_get_version(cert) != 2)
    {  // 2 表示 X.509v3
        throw std::runtime_error("Certificate is not X.509v3");
    }

    // 2. 检查证书签名算法
    const X509_ALGOR* sig_alg = X509_get0_tbs_sigalg(cert);
    if (!sig_alg)
    {
        throw std::runtime_error("Failed to get signature algorithm");
    }

    // 3. 检查安全证书签名算法 当前仅允许 RSA_SHA_256和RSA_SHA_512
    int sig_nid = OBJ_obj2nid(sig_alg->algorithm);
    if (sig_nid != NID_sha256WithRSAEncryption && sig_nid != NID_sha512WithRSAEncryption)
    {
        throw std::runtime_error("Certificate uses insecure signature algorithm: " + std::to_string(sig_nid));
    }
}

// 验证 RSA 密钥长度
void SimpleJsonServerBase::verify_rsa_key_length(EVP_PKEY* pkey)
{
    if (EVP_PKEY_base_id(pkey) == EVP_PKEY_RSA)
    {
        size_t key_length = 0;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        // OpenSSL 3.0 及以上版本
        key_length = static_cast<size_t>(EVP_PKEY_get_bits(pkey));  // 精确位数
#else
        // OpenSSL 1.1.1 及以下版本
        RSA* rsa = EVP_PKEY_get0_RSA(pkey);
        if (!rsa)
        {
            throw std::runtime_error("Failed to get RSA key");
        }

        const BIGNUM* n = nullptr;
        RSA_get0_key(rsa, &n, nullptr, nullptr);
        if (!n)
        {
            throw std::runtime_error("Failed to get RSA modulus");
        }

        key_length = BN_num_bits(n);
#endif
        if (key_length < MIN_RSA_KEY_LENGTH)
        {
            throw std::runtime_error("RSA key length " + std::to_string(key_length) + " bits is less than required " +
                                     std::to_string(MIN_RSA_KEY_LENGTH) + " bits");
        }
    }
}

// 对端证书验证回调：限制链深度，并对叶子证书执行密钥强度与扩展域校验
int SimpleJsonServerBase::verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx)
{
    if (!x509_ctx)
    {
        return 0;
    }

    const int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
    if (!preverify_ok)
    {
        LOG(ERROR) << "Certificate verification failed at depth " << depth << ": "
                   << X509_verify_cert_error_string(X509_STORE_CTX_get_error(x509_ctx));
    }
    // 仅允许“叶子证书 + 直接受信根”两级链，拒绝客户端自带中间 CA
    if (depth > 1)
    {
        LOG(ERROR) << "Certificate chain depth " << depth << " exceeds the allowed maximum of 1";
        return 0;
    }
    if (depth != 0)
    {
        return preverify_ok;
    }

    X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(cert ? X509_get_pubkey(cert) : nullptr, EVP_PKEY_free);
    try
    {
        if (!cert || !pkey)
        {
            throw std::runtime_error("Failed to get peer certificate or public key");
        }
        verify_rsa_key_length(pkey.get());
        verify_certificate_extensions(cert, CertificateType::Client);
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "Peer certificate verification failed: " << e.what();
        return 0;
    }
    return preverify_ok;
}

// 验证证书有效期
void SimpleJsonServerBase::verify_certificate_validity(X509* cert)
{
    ASN1_TIME* not_before = X509_get_notBefore(cert);
    ASN1_TIME* not_after = X509_get_notAfter(cert);
    if (!not_before || !not_after)
    {
        throw std::runtime_error("Failed to get certificate validity period");
    }

    time_t current_time = time(nullptr);
    struct tm tm_before = {};
    struct tm tm_after = {};
    if (!ASN1_TIME_to_tm(not_before, &tm_before) || !ASN1_TIME_to_tm(not_after, &tm_after))
    {
        throw std::runtime_error("Failed to convert certificate dates");
    }

    time_t not_before_time = mktime(&tm_before);
    time_t not_after_time = mktime(&tm_after);

    // 检查证书是否已生效
    if (current_time < not_before_time)
    {
        BIO* bio = BIO_new(BIO_s_mem());
        if (bio)
        {
            ASN1_TIME_print(bio, not_before);
            char* not_before_str = nullptr;
            long len = BIO_get_mem_data(bio, &not_before_str);
            if (len > 0)
            {
                std::string time_str(not_before_str, len);
                BIO_free(bio);
                throw std::runtime_error("Server certificate is not yet valid. Valid from: " + time_str);
            }
            BIO_free(bio);
        }
        throw std::runtime_error("Server certificate is not yet valid");
    }

    // 检查证书是否已过期
    if (current_time > not_after_time)
    {
        BIO* bio = BIO_new(BIO_s_mem());
        if (bio)
        {
            ASN1_TIME_print(bio, not_after);
            char* not_after_str = nullptr;
            long len = BIO_get_mem_data(bio, &not_after_str);
            if (len > 0)
            {
                std::string time_str(not_after_str, len);
                BIO_free(bio);
                throw std::runtime_error("Server certificate has expired. Expired at: " + time_str);
            }
            BIO_free(bio);
        }
        throw std::runtime_error("Server certificate has expired");
    }
}

// 验证证书扩展域
void SimpleJsonServerBase::verify_certificate_extensions(X509* cert, CertificateType type)
{
    const char* cert_type = (type == CertificateType::Client) ? "Client" : "Server";
    BASIC_CONSTRAINTS* constraints =
        static_cast<BASIC_CONSTRAINTS*>(X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    const bool has_ca_constraint = constraints && constraints->ca;
    BASIC_CONSTRAINTS_free(constraints);

    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    const bool has_key_usage = usage != nullptr;
    ASN1_BIT_STRING_free(usage);

    if (has_ca_constraint)
    {
        throw std::runtime_error(std::string(cert_type) + " certificate should not have CA constraint");
    }
    if (!has_key_usage)
    {
        throw std::runtime_error(std::string(cert_type) + " certificate must have key usage extension");
    }
    if (type == CertificateType::Server)
    {
        return;
    }

    EXTENDED_KEY_USAGE* eku =
        static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr));
    const bool has_extended_key_usage = eku != nullptr;
    bool has_client_auth = false;
    for (int i = 0; eku && i < sk_ASN1_OBJECT_num(eku); i++)
    {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(eku, i);
        if (obj && OBJ_obj2nid(obj) == NID_client_auth)
        {
            has_client_auth = true;
            break;
        }
    }
    EXTENDED_KEY_USAGE_free(eku);

    if (!has_extended_key_usage)
    {
        throw std::runtime_error("Client certificate must have extended key usage extension");
    }
    if (!has_client_auth)
    {
        throw std::runtime_error("Client certificate must have clientAuth extended key usage");
    }
}

// 加载私钥
void SimpleJsonServerBase::load_private_key(SSL_CTX* ctx, const std::string& server_key)
{
    FILE* key_file = fopen(server_key.c_str(), "r");
    if (!key_file)
    {
        throw std::runtime_error("Failed to open server key file");
    }

    bool is_encrypted = false;
    char line[256];
    while (fgets(line, sizeof(line), key_file))
    {
        if (strstr(line, "ENCRYPTED"))
        {
            is_encrypted = true;
            break;
        }
    }
    rewind(key_file);

    if (is_encrypted)
    {
        char user_input[256] = {0};
        std::cout << "Please enter the certificate password: ";
        get_password_with_stars(user_input, sizeof(user_input));
        std::cout << std::endl;

        EVP_PKEY* pkey = PEM_read_PrivateKey(
            key_file, nullptr,
            [](char* buf, int size, int rwflag, void* userdata) -> int
            {
                const char* pw = static_cast<const char*>(userdata);
                int pwlen = strlen(pw);
                if (pwlen > size) return 0;
                std::copy(pw, pw + pwlen, buf);
                return pwlen;
            },
            user_input);

        fclose(key_file);
        // 直接清空 char[] 密码
        std::fill(std::begin(user_input), std::end(user_input), 0);

        if (!pkey)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to load encrypted server private key");
        }

        if (SSL_CTX_use_PrivateKey(ctx, pkey) <= 0)
        {
            EVP_PKEY_free(pkey);
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to use server private key");
        }

        EVP_PKEY_free(pkey);
    }
    else
    {
        fclose(key_file);
        if (SSL_CTX_use_PrivateKey_file(ctx, server_key.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to load server private key");
        }
    }
}

// 加载并验证 CRL；TLS 模式下 CRL 为必需文件
void SimpleJsonServerBase::load_and_verify_crl(SSL_CTX* ctx, const std::string& crl_file)
{
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store)
    {
        throw std::runtime_error("Failed to get certificate store");
    }

    FILE* crl_file_ptr = fopen(crl_file.c_str(), "r");
    if (!crl_file_ptr)
    {
        const int error_code = errno;
        if (error_code == ENOENT)
        {
            throw std::runtime_error("CRL file is required but not found: " + crl_file);
        }
        throw std::runtime_error("Failed to open CRL file: " + crl_file + ": " +
                                 std::string(std::strerror(error_code)));
    }
    std::unique_ptr<X509_CRL, decltype(&X509_CRL_free)> crl(PEM_read_X509_CRL(crl_file_ptr, nullptr, nullptr, nullptr),
                                                            X509_CRL_free);
    fclose(crl_file_ptr);
    if (!crl)
    {
        throw std::runtime_error("Failed to read CRL from file: " + crl_file);
    }

    verify_crl_signature(crl.get(), store);
    verify_crl_validity(crl.get());
    verify_crl_covers_trusted_cas(crl.get(), store);
    if (X509_STORE_add_crl(store, crl.get()) != 1)
    {
        throw std::runtime_error("Failed to add CRL to certificate store");
    }

    X509* cert = SSL_CTX_get0_certificate(ctx);
    if (!cert)
    {
        throw std::runtime_error("Failed to get server certificate");
    }
    if (is_cert_revoked(cert, store))
    {
        throw std::runtime_error("Server certificate is revoked");
    }

    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl.get());
    char* issuer = X509_NAME_oneline(X509_CRL_get_issuer(crl.get()), nullptr, 0);
    LOG(INFO) << "CRL loaded: issuer=" << (issuer ? issuer : "unknown")
              << ", revoked_serials=" << (revoked ? sk_X509_REVOKED_num(revoked) : 0)
              << ", next_update=" << format_asn1_time(X509_CRL_get0_nextUpdate(crl.get()));
    OPENSSL_free(issuer);

    const ASN1_TIME* next_update = X509_CRL_get0_nextUpdate(crl.get());
    const time_t now = time(nullptr);
    ASN1_TIME* now_time = ASN1_TIME_set(nullptr, now);
    int days = 0;
    int seconds = 0;
    if (next_update && now_time && ASN1_TIME_diff(&days, &seconds, now_time, next_update) == 1)
    {
        crl_next_update_ = now + static_cast<time_t>(days) * SECONDS_PER_DAY + seconds;
    }
    ASN1_TIME_free(now_time);
    check_crl_expiry();
}

// 运行期按小时限频告警，提醒在 nextUpdate 前更新 CRL 并重启进程
void SimpleJsonServerBase::check_crl_expiry() noexcept
{
    if (crl_next_update_ == 0)
    {
        return;
    }
    const time_t now = time(nullptr);
    const long long remaining = static_cast<long long>(crl_next_update_) - static_cast<long long>(now);
    if (remaining > CRL_WARN_THRESHOLD_SECONDS || now - last_crl_warning_ < CRL_WARN_INTERVAL_SECONDS)
    {
        return;
    }
    last_crl_warning_ = now;
    if (remaining <= 0)
    {
        LOG(ERROR) << "CRL has expired; all new TLS handshakes will be rejected. "
                   << "Renew ca.crl and restart dynolog.";
        return;
    }
    LOG(WARNING) << "CRL expires in " << (remaining + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY
                 << " day(s); renew ca.crl and restart dynolog before nextUpdate.";
}

void SimpleJsonServerBase::configure_context(SSL_CTX* ctx)
{
    if (FLAGS_certs_dir.empty())
    {
        throw std::runtime_error("--certs-dir must be specified!");
    }

    std::string certs_dir = FLAGS_certs_dir;
    if (!certs_dir.empty() && certs_dir.back() != '/') certs_dir += '/';

    std::string server_cert = certs_dir + "server.crt";
    std::string server_key = certs_dir + "server.key";
    std::string ca_cert = certs_dir + "ca.crt";
    std::string crl_file = certs_dir + "ca.crl";

    LOG(INFO) << "Loading server cert: " << server_cert;
    LOG(INFO) << "Loading server key: " << server_key;
    LOG(INFO) << "Loading CA cert: " << ca_cert;

    // 加载并校验服务端证书、密钥、CA 和 CRL
    if (SSL_CTX_use_certificate_file(ctx, server_cert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("Failed to load server certificate");
    }
    X509* cert = SSL_CTX_get0_certificate(ctx);
    if (!cert)
    {
        throw std::runtime_error("Failed to get server certificate");
    }
    verify_certificate_version_and_algorithm(cert);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(X509_get_pubkey(cert), EVP_PKEY_free);
    if (!pkey)
    {
        throw std::runtime_error("Failed to get public key");
    }
    verify_rsa_key_length(pkey.get());
    verify_certificate_validity(cert);
    verify_certificate_extensions(cert, CertificateType::Server);
    load_private_key(ctx, server_key);

    if (SSL_CTX_load_verify_locations(ctx, ca_cert.c_str(), NULL) <= 0)
    {
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("Failed to load CA certificate");
    }
    load_and_verify_crl(ctx, crl_file);

    // 双向认证；只允许叶子证书由直接根签发，并对叶子证书执行 CRL 检查
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
    SSL_CTX_set_verify_depth(ctx, 1);
    X509_VERIFY_PARAM* param = SSL_CTX_get0_param(ctx);
    if (!param || X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_CRL_CHECK) != 1)
    {
        throw std::runtime_error("Failed to enable CRL checking");
    }
}
}  // namespace dynolog
