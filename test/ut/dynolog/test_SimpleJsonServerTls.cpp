/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the
 * Mulan PSL v2.
 *
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

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "dynolog/src/rpc/SimpleJsonServer.h"

namespace dynolog
{
namespace
{

// Default-constructible deleters are required: with a plain function pointer deleter
// (decltype(&X509_free)) libstdc++ before GCC 12 disables unique_ptr's default
// constructor, so any such pointer declared as a member without an initializer fails
// to compile.
struct X509Deleter
{
    void operator()(X509* ptr) const { X509_free(ptr); }
};

struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY* ptr) const { EVP_PKEY_free(ptr); }
};

struct X509CrlDeleter
{
    void operator()(X509_CRL* ptr) const { X509_CRL_free(ptr); }
};

struct SslCtxDeleter
{
    void operator()(SSL_CTX* ptr) const { SSL_CTX_free(ptr); }
};

using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using X509CrlPtr = std::unique_ptr<X509_CRL, X509CrlDeleter>;
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;

constexpr long kCaSerial = 1;
constexpr long kServerSerial = 16;
constexpr long kClientSerial = 10;
constexpr long kDaySeconds = 24L * 60 * 60;

EVP_PKEY* MakeKey(int bits)
{
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* key = nullptr;
    const bool ok = ctx && EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) > 0 &&
                    EVP_PKEY_keygen(ctx, &key) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok)
    {
        EVP_PKEY_free(key);
        throw std::runtime_error("Failed to generate RSA key");
    }
    return key;
}

struct CertSpec
{
    bool is_ca;
    const char* key_usage;
    const char* extended_key_usage;
};

void AddExtension(X509* cert, X509V3_CTX* v3_ctx, int nid, const char* value)
{
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, v3_ctx, nid, value);
    if (!ext || X509_add_ext(cert, ext, -1) != 1)
    {
        X509_EXTENSION_free(ext);
        throw std::runtime_error("Failed to add certificate extension");
    }
    X509_EXTENSION_free(ext);
}

X509Ptr MakeCertificate(const char* common_name, EVP_PKEY* subject_key, EVP_PKEY* issuer_key, X509* issuer_cert,
                        long serial, const CertSpec& spec)
{
    X509Ptr cert(X509_new());
    if (!cert)
    {
        throw std::runtime_error("Failed to allocate certificate");
    }
    X509_set_version(cert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -kDaySeconds);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 10L * 365 * kDaySeconds);
    X509_set_pubkey(cert.get(), subject_key);

    X509_NAME* subject = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name), -1, -1,
                               0);
    X509_set_issuer_name(cert.get(), issuer_cert ? X509_get_subject_name(issuer_cert) : subject);

    X509V3_CTX v3_ctx;
    X509V3_set_ctx_nodb(&v3_ctx);
    X509V3_set_ctx(&v3_ctx, issuer_cert, cert.get(), nullptr, nullptr, 0);
    AddExtension(cert.get(), &v3_ctx, NID_basic_constraints, spec.is_ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
    if (spec.key_usage[0] != '\0')
    {
        AddExtension(cert.get(), &v3_ctx, NID_key_usage, spec.key_usage);
    }
    if (spec.extended_key_usage[0] != '\0')
    {
        AddExtension(cert.get(), &v3_ctx, NID_ext_key_usage, spec.extended_key_usage);
    }
    if (X509_sign(cert.get(), issuer_key, EVP_sha256()) <= 0)
    {
        throw std::runtime_error("Failed to sign certificate");
    }
    return cert;
}

X509CrlPtr MakeCrl(EVP_PKEY* signing_key, X509_NAME* issuer_name, long this_update_offset, long next_update_offset,
                   long revoked_serial)
{
    X509CrlPtr crl(X509_CRL_new());
    if (!crl)
    {
        throw std::runtime_error("Failed to allocate CRL");
    }
    X509_CRL_set_version(crl.get(), 1);
    X509_CRL_set_issuer_name(crl.get(), issuer_name);
    ASN1_TIME* this_update = X509_gmtime_adj(nullptr, this_update_offset);
    X509_CRL_set_lastUpdate(crl.get(), this_update);
    ASN1_TIME_free(this_update);
    ASN1_TIME* next_update = X509_gmtime_adj(nullptr, next_update_offset);
    X509_CRL_set_nextUpdate(crl.get(), next_update);
    ASN1_TIME_free(next_update);

    if (revoked_serial != 0)
    {
        X509_REVOKED* revoked = X509_REVOKED_new();
        ASN1_INTEGER* serial = ASN1_INTEGER_new();
        ASN1_INTEGER_set(serial, revoked_serial);
        X509_REVOKED_set_serialNumber(revoked, serial);
        ASN1_INTEGER_free(serial);
        ASN1_TIME* revocation_time = X509_gmtime_adj(nullptr, -3600);
        X509_REVOKED_set_revocationDate(revoked, revocation_time);
        ASN1_TIME_free(revocation_time);
        X509_CRL_add0_revoked(crl.get(), revoked);
    }
    if (X509_CRL_sign(crl.get(), signing_key, EVP_sha256()) <= 0)
    {
        throw std::runtime_error("Failed to sign CRL");
    }
    return crl;
}

// Generates all key material in memory. Nothing is checked into the repository.
class TestPki
{
   public:
    TestPki() : ca_key_(MakeKey(3072)), rogue_key_(MakeKey(3072)), ca_cert_(nullptr)
    {
        ca_cert_ = MakeCertificate("msmonitor UT CA", ca_key_.get(), ca_key_.get(), nullptr, kCaSerial,
                                   CertSpec{true, "critical,keyCertSign,cRLSign", ""});
        EvpPkeyPtr client_key(MakeKey(3072));
        EvpPkeyPtr server_key(MakeKey(3072));
        EvpPkeyPtr weak_key(MakeKey(2048));

        Add("client_valid", "UT Client", client_key.get(), kClientSerial,
            CertSpec{false, "critical,digitalSignature", "clientAuth"});
        Add("client_no_eku", "UT Client No EKU", client_key.get(), kClientSerial + 1,
            CertSpec{false, "critical,digitalSignature", ""});
        Add("client_server_eku", "UT Client Server EKU", client_key.get(), kClientSerial + 2,
            CertSpec{false, "critical,digitalSignature", "serverAuth"});
        Add("client_no_key_usage", "UT Client No KU", client_key.get(), kClientSerial + 3,
            CertSpec{false, "", "clientAuth"});
        Add("client_ca_true", "UT Client CA True", client_key.get(), kClientSerial + 4,
            CertSpec{true, "critical,digitalSignature", "clientAuth"});
        Add("client_rsa2048", "UT Client 2048", weak_key.get(), kClientSerial + 5,
            CertSpec{false, "critical,digitalSignature", "clientAuth"});
        Add("server_valid", "UT Server", server_key.get(), kServerSerial,
            CertSpec{false, "critical,digitalSignature,keyEncipherment", ""});
        Add("server_no_key_usage", "UT Server No KU", server_key.get(), kServerSerial + 1, CertSpec{false, "", ""});
        owned_keys_.push_back(std::move(client_key));
        owned_keys_.push_back(std::move(server_key));
        owned_keys_.push_back(std::move(weak_key));
    }

    X509* Get(const char* name) const
    {
        const auto it = certs_.find(name);
        if (it == certs_.end())
        {
            throw std::runtime_error(std::string("Unknown test certificate: ") + name);
        }
        return it->second.get();
    }

    EVP_PKEY* Key(const char* name) const
    {
        const auto it = keys_.find(name);
        if (it == keys_.end())
        {
            throw std::runtime_error(std::string("Unknown test key: ") + name);
        }
        return it->second;
    }

    X509* CaCert() const { return ca_cert_.get(); }

    X509CrlPtr MakeCaCrl(long this_update_offset, long next_update_offset, long revoked_serial = 0) const
    {
        return MakeCrl(ca_key_.get(), X509_get_subject_name(ca_cert_.get()), this_update_offset, next_update_offset,
                       revoked_serial);
    }

    // Signed by an unknown key, with the subject of a non-CA certificate as issuer.
    X509CrlPtr MakeUntrustedCrl() const
    {
        return MakeCrl(rogue_key_.get(), X509_get_subject_name(Get("client_valid")), -kDaySeconds, 30 * kDaySeconds, 0);
    }

    // Same issuer name as the trusted CA, but signed by a different key.
    X509CrlPtr MakeForgedCrl() const
    {
        return MakeCrl(rogue_key_.get(), X509_get_subject_name(ca_cert_.get()), -kDaySeconds, 30 * kDaySeconds, 0);
    }

   private:
    void Add(const char* name, const char* common_name, EVP_PKEY* key, long serial, const CertSpec& spec)
    {
        certs_.emplace(name, MakeCertificate(common_name, key, ca_key_.get(), ca_cert_.get(), serial, spec));
        keys_[name] = key;
    }

    EvpPkeyPtr ca_key_;
    EvpPkeyPtr rogue_key_;
    X509Ptr ca_cert_;
    std::map<std::string, X509Ptr> certs_;
    std::map<std::string, EVP_PKEY*> keys_;
    std::vector<EvpPkeyPtr> owned_keys_;
};

const TestPki& Pki()
{
    static const TestPki pki;
    return pki;
}

class TempDir
{
   public:
    TempDir()
    {
        char pattern[] = "/tmp/msmonitor_tls_ut_XXXXXX";
        char* created = mkdtemp(pattern);
        if (created == nullptr)
        {
            throw std::runtime_error("Failed to create temporary directory");
        }
        path_ = created;
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    std::string Path(const char* name) const { return path_ + "/" + name; }

   private:
    std::string path_;
};

void WriteCrl(const std::string& path, X509_CRL* crl)
{
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr || PEM_write_X509_CRL(file, crl) != 1)
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
        throw std::runtime_error("Failed to write CRL file: " + path);
    }
    std::fclose(file);
}

void WritePem(const std::string& path, X509* cert)
{
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr || PEM_write_X509(file, cert) != 1)
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
        throw std::runtime_error("Failed to write certificate file: " + path);
    }
    std::fclose(file);
}

void WritePem(const std::string& path, EVP_PKEY* key)
{
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr || PEM_write_PrivateKey(file, key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
        throw std::runtime_error("Failed to write private key file: " + path);
    }
    std::fclose(file);
}

int FindFreePort()
{
    const int sock = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return -1;
    }
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    socklen_t addr_len = sizeof(addr);
    int port = -1;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
        ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0)
    {
        port = ntohs(addr.sin6_port);
    }
    ::close(sock);
    return port;
}

class TestableSimpleJsonServer : public SimpleJsonServerBase
{
   public:
    using CertificateType = SimpleJsonServerBase::CertificateType;
    TestableSimpleJsonServer() : SimpleJsonServerBase(FindFreePort()) {}
    using SimpleJsonServerBase::configure_context;
    using SimpleJsonServerBase::create_context;
    using SimpleJsonServerBase::load_and_verify_crl;
    using SimpleJsonServerBase::verify_certificate_extensions;
    using SimpleJsonServerBase::verify_rsa_key_length;
};

// Writes the full certificate directory expected by configure_context() and points
// FLAGS_certs_dir at it, restoring the previous value on destruction.
class CertDirectory
{
   public:
    explicit CertDirectory(long revoked_serial)
    {
        WritePem(temp_.Path("ca.crt"), Pki().CaCert());
        WritePem(temp_.Path("server.crt"), Pki().Get("server_valid"));
        WritePem(temp_.Path("server.key"), Pki().Key("server_valid"));
        WriteCrl(temp_.Path("ca.crl"), Pki().MakeCaCrl(-kDaySeconds, 30 * kDaySeconds, revoked_serial).get());
        previous_ = FLAGS_certs_dir;
        FLAGS_certs_dir = temp_.Path("");
    }
    ~CertDirectory() { FLAGS_certs_dir = previous_; }

   private:
    TempDir temp_;
    std::string previous_;
};

// Drives one TLS handshake over an in-memory BIO pair using the production server
// context. Returns true only if both sides complete the handshake.
bool Handshake(SSL_CTX* server_ctx, const char* client_cert)
{
    SslCtxPtr client_ctx(SSL_CTX_new(TLS_client_method()));
    if (!client_ctx || SSL_CTX_use_certificate(client_ctx.get(), Pki().Get(client_cert)) != 1 ||
        SSL_CTX_use_PrivateKey(client_ctx.get(), Pki().Key(client_cert)) != 1)
    {
        throw std::runtime_error("Failed to create client context");
    }
    SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);

    std::unique_ptr<SSL, decltype(&SSL_free)> server_ssl(SSL_new(server_ctx), SSL_free);
    std::unique_ptr<SSL, decltype(&SSL_free)> client_ssl(SSL_new(client_ctx.get()), SSL_free);
    BIO* server_bio = nullptr;
    BIO* client_bio = nullptr;
    if (!server_ssl || !client_ssl || BIO_new_bio_pair(&server_bio, 0, &client_bio, 0) != 1)
    {
        throw std::runtime_error("Failed to create SSL/BIO pair");
    }
    SSL_set_bio(server_ssl.get(), server_bio, server_bio);
    SSL_set_bio(client_ssl.get(), client_bio, client_bio);
    SSL_set_accept_state(server_ssl.get());
    SSL_set_connect_state(client_ssl.get());

    for (int i = 0; i < 16; ++i)
    {
        const int server_ret = SSL_do_handshake(server_ssl.get());
        const int server_error = SSL_get_error(server_ssl.get(), server_ret);
        const int client_ret = SSL_do_handshake(client_ssl.get());
        const int client_error = SSL_get_error(client_ssl.get(), client_ret);
        if (server_ret == 1 && client_ret == 1)
        {
            return true;
        }
        const bool server_waiting = server_error == SSL_ERROR_WANT_READ || server_error == SSL_ERROR_WANT_WRITE;
        const bool client_waiting = client_error == SSL_ERROR_WANT_READ || client_error == SSL_ERROR_WANT_WRITE;
        if (!(server_ret == 1 || server_waiting) || !(client_ret == 1 || client_waiting))
        {
            return false;
        }
    }
    return false;
}

template <typename Func>
testing::AssertionResult ThrowsWithMessage(Func&& func, const std::string& expected)
{
    try
    {
        func();
    }
    catch (const std::exception& e)
    {
        if (std::string(e.what()).find(expected) != std::string::npos)
        {
            return testing::AssertionSuccess();
        }
        return testing::AssertionFailure()
               << "error message '" << e.what() << "' does not contain '" << expected << "'";
    }
    return testing::AssertionFailure() << "expected an exception containing '" << expected << "'";
}

SslCtxPtr CreateServerContext()
{
    SslCtxPtr ctx(SSL_CTX_new(TLS_server_method()));
    if (!ctx || X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), Pki().CaCert()) != 1 ||
        SSL_CTX_use_certificate(ctx.get(), Pki().Get("server_valid")) != 1)
    {
        throw std::runtime_error("Failed to create server context");
    }
    return ctx;
}

void ExpectCrlError(TestableSimpleJsonServer& server, SSL_CTX* ctx, X509_CRL* crl, const char* expected)
{
    TempDir temp;
    const std::string path = temp.Path("ca.crl");
    WriteCrl(path, crl);
    EXPECT_TRUE(ThrowsWithMessage([&] { server.load_and_verify_crl(ctx, path); }, expected));
}

class SimpleJsonServerTlsTest : public ::testing::Test
{
   protected:
    using CertType = TestableSimpleJsonServer::CertificateType;
    TestableSimpleJsonServer server_;
};

TEST_F(SimpleJsonServerTlsTest, ValidatesClientCertificateExtensions)
{
    const auto check = [this](const char* cert, const char* error)
    {
        if (error == nullptr)
        {
            EXPECT_NO_THROW(server_.verify_certificate_extensions(Pki().Get(cert), CertType::Client));
            return;
        }
        EXPECT_TRUE(
            ThrowsWithMessage([&] { server_.verify_certificate_extensions(Pki().Get(cert), CertType::Client); }, error))
            << cert;
    };
    check("client_valid", nullptr);
    check("client_no_eku", "extended key usage extension");
    check("client_server_eku", "clientAuth extended key usage");
    check("client_no_key_usage", "key usage extension");
    check("client_ca_true", "CA constraint");
}

TEST_F(SimpleJsonServerTlsTest, ValidatesServerCertificateExtensions)
{
    EXPECT_NO_THROW(server_.verify_certificate_extensions(Pki().Get("server_valid"), CertType::Server));
    EXPECT_TRUE(ThrowsWithMessage(
        [&] { server_.verify_certificate_extensions(Pki().Get("server_no_key_usage"), CertType::Server); },
        "key usage extension"));
}

TEST_F(SimpleJsonServerTlsTest, ValidatesRsaKeyLength)
{
    EvpPkeyPtr strong(X509_get_pubkey(Pki().Get("client_valid")));
    EvpPkeyPtr weak(X509_get_pubkey(Pki().Get("client_rsa2048")));
    ASSERT_TRUE(strong && weak);
    EXPECT_NO_THROW(server_.verify_rsa_key_length(strong.get()));
    EXPECT_TRUE(ThrowsWithMessage([&] { server_.verify_rsa_key_length(weak.get()); }, "less than required"));
}

TEST_F(SimpleJsonServerTlsTest, RejectsMissingOrCorruptCrl)
{
    TempDir temp;
    SslCtxPtr ctx = CreateServerContext();
    EXPECT_TRUE(ThrowsWithMessage([&] { server_.load_and_verify_crl(ctx.get(), temp.Path("missing.crl")); },
                                  "CRL file is required but not found"));
    const std::string path = temp.Path("corrupt.crl");
    std::ofstream(path) << "not a CRL\n";
    EXPECT_TRUE(
        ThrowsWithMessage([&] { server_.load_and_verify_crl(ctx.get(), path); }, "Failed to read CRL from file"));
}

TEST_F(SimpleJsonServerTlsTest, LoadsValidCrl)
{
    TempDir temp;
    const std::string path = temp.Path("ca.crl");
    WriteCrl(path, Pki().MakeCaCrl(-kDaySeconds, 30 * kDaySeconds).get());
    SslCtxPtr ctx = CreateServerContext();
    EXPECT_NO_THROW(server_.load_and_verify_crl(ctx.get(), path));
}

TEST_F(SimpleJsonServerTlsTest, RejectsInvalidCrl)
{
    SslCtxPtr ctx = CreateServerContext();
    ExpectCrlError(server_, ctx.get(), Pki().MakeCaCrl(-10 * kDaySeconds, -kDaySeconds).get(), "CRL has expired");
    ExpectCrlError(server_, ctx.get(), Pki().MakeUntrustedCrl().get(), "No trusted CA certificate found");
    ExpectCrlError(server_, ctx.get(), Pki().MakeForgedCrl().get(), "CRL signature verification failed");
    ExpectCrlError(server_, ctx.get(), Pki().MakeCaCrl(-kDaySeconds, 30 * kDaySeconds, kServerSerial).get(),
                   "Server certificate is revoked");
}

TEST_F(SimpleJsonServerTlsTest, RejectsTrustedCaNotCoveredByCrl)
{
    TempDir temp;
    EvpPkeyPtr second_ca_key(MakeKey(3072));
    X509Ptr second_ca = MakeCertificate("UT Second CA", second_ca_key.get(), second_ca_key.get(), nullptr,
                                        kCaSerial + 1, CertSpec{true, "critical,keyCertSign,cRLSign", ""});
    SslCtxPtr ctx = CreateServerContext();
    ASSERT_EQ(X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), second_ca.get()), 1);

    const std::string path = temp.Path("ca.crl");
    WriteCrl(path, Pki().MakeCaCrl(-kDaySeconds, 30 * kDaySeconds).get());
    EXPECT_TRUE(ThrowsWithMessage([&] { server_.load_and_verify_crl(ctx.get(), path); },
                                  "CA certificate is not covered by CRL"));
}

TEST_F(SimpleJsonServerTlsTest, HandshakeAcceptsValidClientCertificate)
{
    CertDirectory certs(0);
    SslCtxPtr ctx(server_.create_context());
    server_.configure_context(ctx.get());
    EXPECT_TRUE(Handshake(ctx.get(), "client_valid"));
}

// Exercises the production wiring: verify_callback, X509_V_FLAG_CRL_CHECK and verify_depth.
TEST_F(SimpleJsonServerTlsTest, HandshakeRejectsInvalidClientCertificates)
{
    CertDirectory certs(kClientSerial);
    SslCtxPtr ctx(server_.create_context());
    server_.configure_context(ctx.get());
    EXPECT_FALSE(Handshake(ctx.get(), "client_valid")) << "revoked by CRL";
    EXPECT_FALSE(Handshake(ctx.get(), "client_no_eku")) << "missing clientAuth EKU";
    EXPECT_FALSE(Handshake(ctx.get(), "client_server_eku")) << "serverAuth instead of clientAuth";
    EXPECT_FALSE(Handshake(ctx.get(), "client_ca_true")) << "CA:TRUE leaf";
    EXPECT_FALSE(Handshake(ctx.get(), "client_rsa2048")) << "2048-bit RSA key";
}

}  // namespace
}  // namespace dynolog

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
