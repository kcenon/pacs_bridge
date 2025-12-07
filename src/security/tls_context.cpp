/**
 * @file tls_context.cpp
 * @brief TLS context implementation using OpenSSL
 *
 * Provides OpenSSL-based implementation of TLS context management.
 * This implementation wraps OpenSSL's SSL_CTX for certificate loading,
 * session management, and secure connection establishment.
 *
 * @see include/pacs/bridge/security/tls_context.h
 */

#include "pacs/bridge/security/tls_context.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

// OpenSSL headers
#ifdef PACS_BRIDGE_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#else
// Stub definitions for compilation without OpenSSL
// These will be replaced when OpenSSL is available
struct ssl_ctx_st {};
using SSL_CTX = ssl_ctx_st;
#endif

namespace pacs::bridge::security {

// =============================================================================
// Global TLS State
// =============================================================================

namespace {

std::atomic<bool> g_tls_initialized{false};
std::mutex g_init_mutex;

#ifdef PACS_BRIDGE_HAS_OPENSSL
/**
 * @brief Get OpenSSL error string
 */
std::string get_openssl_error() {
    char buffer[256];
    ERR_error_string_n(ERR_get_error(), buffer, sizeof(buffer));
    return buffer;
}

/**
 * @brief Convert time_t to system_clock::time_point
 */
std::chrono::system_clock::time_point
asn1_time_to_time_point(const ASN1_TIME* time) {
    struct tm tm_time {};
    ASN1_TIME_to_tm(time, &tm_time);
    return std::chrono::system_clock::from_time_t(std::mktime(&tm_time));
}

/**
 * @brief Extract certificate information from X509
 */
security::certificate_info extract_cert_info(X509* cert) {
    security::certificate_info info;

    if (!cert) {
        return info;
    }

    // Subject
    char* subject_str = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (subject_str) {
        info.subject = subject_str;
        OPENSSL_free(subject_str);
    }

    // Issuer
    char* issuer_str = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (issuer_str) {
        info.issuer = issuer_str;
        OPENSSL_free(issuer_str);
    }

    // Serial number
    const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
    if (serial) {
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        if (bn) {
            char* hex = BN_bn2hex(bn);
            if (hex) {
                info.serial_number = hex;
                OPENSSL_free(hex);
            }
            BN_free(bn);
        }
    }

    // Validity
    info.not_before = asn1_time_to_time_point(X509_get0_notBefore(cert));
    info.not_after = asn1_time_to_time_point(X509_get0_notAfter(cert));

    // Subject Alternative Names
    STACK_OF(GENERAL_NAME)* san_names = static_cast<STACK_OF(GENERAL_NAME)*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

    if (san_names) {
        int count = sk_GENERAL_NAME_num(san_names);
        for (int i = 0; i < count; ++i) {
            GENERAL_NAME* name = sk_GENERAL_NAME_value(san_names, i);
            if (name->type == GEN_DNS) {
                const char* dns_name = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(name->d.dNSName));
                if (dns_name) {
                    info.san_entries.emplace_back(dns_name);
                }
            } else if (name->type == GEN_IPADD) {
                // IP address handling could be added here
            }
        }
        GENERAL_NAMES_free(san_names);
    }

    // SHA-256 fingerprint
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &digest_len)) {
        std::ostringstream oss;
        for (unsigned int i = 0; i < digest_len; ++i) {
            if (i > 0) oss << ":";
            oss << std::hex << std::uppercase << std::setw(2)
                << std::setfill('0') << static_cast<int>(digest[i]);
        }
        info.fingerprint_sha256 = oss.str();
    }

    return info;
}
#endif

}  // namespace

// =============================================================================
// TLS Context Implementation
// =============================================================================

class tls_context::impl {
public:
    impl() = default;

    ~impl() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (ctx_) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
#endif
    }

    // Non-copyable
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

#ifdef PACS_BRIDGE_HAS_OPENSSL
    SSL_CTX* ctx_ = nullptr;
#else
    void* ctx_ = nullptr;
#endif

    bool is_server_ = false;
    tls_version min_version_ = tls_version::tls_1_2;
    client_auth_mode client_auth_ = client_auth_mode::none;
    std::optional<security::certificate_info> cert_info_;
    mutable tls_statistics stats_;
    mutable std::mutex stats_mutex_;
    tls_context::verify_callback verify_cb_;
};

tls_context::tls_context() : pimpl_(std::make_unique<impl>()) {}

tls_context::~tls_context() = default;

tls_context::tls_context(tls_context&&) noexcept = default;
tls_context& tls_context::operator=(tls_context&&) noexcept = default;

std::expected<tls_context, tls_error>
tls_context::create_server_context(const tls_config& config) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!g_tls_initialized) {
        return std::unexpected(tls_error::initialization_failed);
    }

    if (!config.is_valid_for_server()) {
        return std::unexpected(tls_error::certificate_invalid);
    }

    tls_context context;
    context.pimpl_->is_server_ = true;
    context.pimpl_->min_version_ = config.min_version;
    context.pimpl_->client_auth_ = config.client_auth;

    // Create SSL context
    const SSL_METHOD* method = TLS_server_method();
    context.pimpl_->ctx_ = SSL_CTX_new(method);
    if (!context.pimpl_->ctx_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    SSL_CTX* ctx = context.pimpl_->ctx_;

    // Set minimum TLS version
    int min_version_flag = (config.min_version == tls_version::tls_1_3)
                               ? TLS1_3_VERSION
                               : TLS1_2_VERSION;
    SSL_CTX_set_min_proto_version(ctx, min_version_flag);

    // Load certificate
    if (!config.cert_path.empty()) {
        if (SSL_CTX_use_certificate_chain_file(
                ctx, config.cert_path.string().c_str()) != 1) {
            return std::unexpected(tls_error::certificate_invalid);
        }
    }

    // Load private key
    if (!config.key_path.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx, config.key_path.string().c_str(),
                                         SSL_FILETYPE_PEM) != 1) {
            return std::unexpected(tls_error::private_key_invalid);
        }

        // Verify key matches certificate
        if (SSL_CTX_check_private_key(ctx) != 1) {
            return std::unexpected(tls_error::key_certificate_mismatch);
        }
    }

    // Load CA for client verification
    if (!config.ca_path.empty()) {
        if (SSL_CTX_load_verify_locations(
                ctx, config.ca_path.string().c_str(), nullptr) != 1) {
            return std::unexpected(tls_error::ca_certificate_invalid);
        }
    }

    // Configure client authentication
    int verify_mode = SSL_VERIFY_NONE;
    if (config.client_auth == client_auth_mode::optional) {
        verify_mode = SSL_VERIFY_PEER;
    } else if (config.client_auth == client_auth_mode::required) {
        verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    SSL_CTX_set_verify(ctx, verify_mode, nullptr);

    // Set cipher suites if specified
    if (!config.cipher_suites.empty()) {
        std::string cipher_string;
        for (const auto& suite : config.cipher_suites) {
            if (!cipher_string.empty()) cipher_string += ":";
            cipher_string += suite;
        }
        SSL_CTX_set_cipher_list(ctx, cipher_string.c_str());
    }

    // Configure session cache
    if (config.session_cache_size > 0) {
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
        SSL_CTX_sess_set_cache_size(ctx,
            static_cast<long>(config.session_cache_size));
    }

    return context;
#else
    // Stub implementation when OpenSSL is not available
    (void)config;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<tls_context, tls_error>
tls_context::create_client_context(const tls_config& config) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!g_tls_initialized) {
        return std::unexpected(tls_error::initialization_failed);
    }

    tls_context context;
    context.pimpl_->is_server_ = false;
    context.pimpl_->min_version_ = config.min_version;

    // Create SSL context
    const SSL_METHOD* method = TLS_client_method();
    context.pimpl_->ctx_ = SSL_CTX_new(method);
    if (!context.pimpl_->ctx_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    SSL_CTX* ctx = context.pimpl_->ctx_;

    // Set minimum TLS version
    int min_version_flag = (config.min_version == tls_version::tls_1_3)
                               ? TLS1_3_VERSION
                               : TLS1_2_VERSION;
    SSL_CTX_set_min_proto_version(ctx, min_version_flag);

    // Load CA for server verification
    if (!config.ca_path.empty()) {
        if (SSL_CTX_load_verify_locations(
                ctx, config.ca_path.string().c_str(), nullptr) != 1) {
            return std::unexpected(tls_error::ca_certificate_invalid);
        }
    }

    // Load client certificate for mutual TLS
    if (!config.cert_path.empty()) {
        if (SSL_CTX_use_certificate_chain_file(
                ctx, config.cert_path.string().c_str()) != 1) {
            return std::unexpected(tls_error::certificate_invalid);
        }
    }

    // Load client private key for mutual TLS
    if (!config.key_path.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx, config.key_path.string().c_str(),
                                         SSL_FILETYPE_PEM) != 1) {
            return std::unexpected(tls_error::private_key_invalid);
        }

        if (SSL_CTX_check_private_key(ctx) != 1) {
            return std::unexpected(tls_error::key_certificate_mismatch);
        }
    }

    // Configure server verification
    if (config.verify_peer) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    }

    return context;
#else
    (void)config;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

void tls_context::set_verify_callback(verify_callback callback) {
    pimpl_->verify_cb_ = std::move(callback);
}

std::expected<void, tls_error>
tls_context::load_ca_certificates(const std::filesystem::path& ca_path) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ctx_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    if (SSL_CTX_load_verify_locations(pimpl_->ctx_, ca_path.string().c_str(),
                                       nullptr) != 1) {
        return std::unexpected(tls_error::ca_certificate_invalid);
    }

    return {};
#else
    (void)ca_path;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<void, tls_error>
tls_context::set_cipher_suites(std::string_view cipher_string) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ctx_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    if (SSL_CTX_set_cipher_list(pimpl_->ctx_,
                                 std::string(cipher_string).c_str()) != 1) {
        return std::unexpected(tls_error::invalid_cipher_suite);
    }

    return {};
#else
    (void)cipher_string;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

void tls_context::enable_session_resumption(size_t cache_size) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ctx_) return;

    if (cache_size > 0) {
        SSL_CTX_set_session_cache_mode(pimpl_->ctx_, SSL_SESS_CACHE_SERVER);
        SSL_CTX_sess_set_cache_size(pimpl_->ctx_, static_cast<long>(cache_size));
    } else {
        SSL_CTX_set_session_cache_mode(pimpl_->ctx_, SSL_SESS_CACHE_OFF);
    }
#else
    (void)cache_size;
#endif
}

bool tls_context::is_server() const noexcept {
    return pimpl_->is_server_;
}

bool tls_context::is_client() const noexcept {
    return !pimpl_->is_server_;
}

tls_version tls_context::min_version() const noexcept {
    return pimpl_->min_version_;
}

client_auth_mode tls_context::client_auth() const noexcept {
    return pimpl_->client_auth_;
}

std::optional<security::certificate_info>
tls_context::certificate_info() const noexcept {
    return pimpl_->cert_info_;
}

tls_statistics tls_context::statistics() const noexcept {
    std::lock_guard lock(pimpl_->stats_mutex_);
    return pimpl_->stats_;
}

void* tls_context::native_handle() noexcept {
    return pimpl_->ctx_;
}

const void* tls_context::native_handle() const noexcept {
    return pimpl_->ctx_;
}

// =============================================================================
// Global TLS Functions
// =============================================================================

std::expected<void, tls_error> initialize_tls() {
    std::lock_guard lock(g_init_mutex);

    if (g_tls_initialized) {
        return {};  // Already initialized
    }

#ifdef PACS_BRIDGE_HAS_OPENSSL
    // Initialize OpenSSL library
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                         OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                     nullptr);

    g_tls_initialized = true;
    return {};
#else
    // Stub - TLS not available without OpenSSL
    return std::unexpected(tls_error::initialization_failed);
#endif
}

void cleanup_tls() {
    std::lock_guard lock(g_init_mutex);

    if (!g_tls_initialized) {
        return;
    }

#ifdef PACS_BRIDGE_HAS_OPENSSL
    // Note: OpenSSL 1.1.0+ handles cleanup automatically via atexit handlers
    // EVP_cleanup() and related functions are deprecated
#endif

    g_tls_initialized = false;
}

std::expected<security::certificate_info, tls_error>
read_certificate_info(const std::filesystem::path& cert_path) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    FILE* fp = fopen(cert_path.string().c_str(), "r");
    if (!fp) {
        return std::unexpected(tls_error::certificate_invalid);
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        return std::unexpected(tls_error::certificate_invalid);
    }

    auto info = extract_cert_info(cert);
    X509_free(cert);

    return info;
#else
    (void)cert_path;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<void, tls_error>
verify_key_pair(const std::filesystem::path& cert_path,
                const std::filesystem::path& key_path) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    // Load certificate
    FILE* cert_fp = fopen(cert_path.string().c_str(), "r");
    if (!cert_fp) {
        return std::unexpected(tls_error::certificate_invalid);
    }
    X509* cert = PEM_read_X509(cert_fp, nullptr, nullptr, nullptr);
    fclose(cert_fp);
    if (!cert) {
        return std::unexpected(tls_error::certificate_invalid);
    }

    // Load private key
    FILE* key_fp = fopen(key_path.string().c_str(), "r");
    if (!key_fp) {
        X509_free(cert);
        return std::unexpected(tls_error::private_key_invalid);
    }
    EVP_PKEY* pkey = PEM_read_PrivateKey(key_fp, nullptr, nullptr, nullptr);
    fclose(key_fp);
    if (!pkey) {
        X509_free(cert);
        return std::unexpected(tls_error::private_key_invalid);
    }

    // Verify key matches certificate
    EVP_PKEY* cert_pkey = X509_get_pubkey(cert);
    int result = EVP_PKEY_eq(cert_pkey, pkey);

    EVP_PKEY_free(cert_pkey);
    EVP_PKEY_free(pkey);
    X509_free(cert);

    if (result != 1) {
        return std::unexpected(tls_error::key_certificate_mismatch);
    }

    return {};
#else
    (void)cert_path;
    (void)key_path;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::string openssl_version() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    return OPENSSL_VERSION_TEXT;
#else
    return "OpenSSL not available";
#endif
}

}  // namespace pacs::bridge::security
