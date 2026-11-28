// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include <gflags/gflags.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <atomic>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "dynolog/src/ServiceHandler.h"

DECLARE_string(certs_dir);

namespace dynolog
{
// This is a simple service built using UNIX Sockets
// with remote procedure calls implemented via JSON string.
class SimpleJsonServerBase
{
   public:
    explicit SimpleJsonServerBase(int port);
    virtual ~SimpleJsonServerBase();

    int getPort() const { return port_; }

    bool initSuccessful() const { return initSuccess_; }
    // spin up a new thread to process requets
    void run();

    void stop()
    {
        run_ = 0;
        if (thread_ != nullptr && thread_->joinable())
        {
            thread_->join();
        }
    }

    // synchronously processes a request
    void processOne() noexcept;

   protected:
    void initSocket();
    void init_openssl();
    SSL_CTX *create_context();
    void configure_context(SSL_CTX *ctx);

    // process requests in a loop
    void loop() noexcept;

    // implement processing of request using the handler
    virtual std::string processOneImpl(const std::string &request_str) { return ""; }

    enum class CertificateType
    {
        Server,
        Client,
    };

    void verify_certificate_version_and_algorithm(X509 *cert);
    static void verify_rsa_key_length(EVP_PKEY *pkey);
    static int verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx);
    void verify_certificate_validity(X509 *cert);
    static void verify_certificate_extensions(X509 *cert, CertificateType type);
    void load_private_key(SSL_CTX *ctx, const std::string &server_key);
    void load_and_verify_crl(SSL_CTX *ctx, const std::string &crl_file);
    void check_crl_expiry() noexcept;

    int port_;
    int sock_fd_{-1};
    bool initSuccess_{false};

    std::atomic<bool> run_{true};
    std::unique_ptr<std::thread> thread_;

    SSL_CTX *ctx_{nullptr};
    time_t crl_next_update_{0};
    time_t last_crl_warning_{0};
};

}  // namespace dynolog
