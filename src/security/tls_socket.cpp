/**
 * @file tls_socket.cpp
 * @brief TLS socket implementation using OpenSSL
 *
 * Provides OpenSSL-based implementation of TLS socket operations
 * including handshake, read, write, and graceful shutdown.
 *
 * @see include/pacs/bridge/security/tls_socket.h
 */

#include "pacs/bridge/security/tls_socket.h"

#include <chrono>
#include <cstring>

// OpenSSL headers
#ifdef PACS_BRIDGE_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#else
// Stub definitions
struct ssl_st {};
using SSL = ssl_st;
#endif

namespace pacs::bridge::security {

namespace {

#ifdef PACS_BRIDGE_HAS_OPENSSL
/**
 * @brief Get OpenSSL error string
 */
std::string get_ssl_error_string() {
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
security::certificate_info extract_peer_cert_info(SSL* ssl) {
    security::certificate_info info;

    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        return info;
    }

    // Subject
    char* subject = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (subject) {
        info.subject = subject;
        OPENSSL_free(subject);
    }

    // Issuer
    char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (issuer) {
        info.issuer = issuer;
        OPENSSL_free(issuer);
    }

    // Serial
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

    X509_free(cert);
    return info;
}
#endif

}  // namespace

// =============================================================================
// TLS Socket Implementation
// =============================================================================

class tls_socket::impl {
public:
    impl() = default;

    ~impl() {
        close();
    }

    void close() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (ssl_) {
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
#endif
        socket_fd_ = -1;
        handshake_complete_ = false;
    }

#ifdef PACS_BRIDGE_HAS_OPENSSL
    SSL* ssl_ = nullptr;
#else
    void* ssl_ = nullptr;
#endif
    int socket_fd_ = -1;
    bool is_server_ = false;
    bool handshake_complete_ = false;
    std::string hostname_;
    std::string last_error_;
    std::optional<security::certificate_info> peer_cert_;
};

tls_socket::tls_socket() : pimpl_(std::make_unique<impl>()) {}

tls_socket::~tls_socket() = default;

tls_socket::tls_socket(tls_socket&&) noexcept = default;
tls_socket& tls_socket::operator=(tls_socket&&) noexcept = default;

std::expected<tls_socket, tls_error>
tls_socket::accept(tls_context& context, int socket_fd) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!context.is_server()) {
        return std::unexpected(tls_error::initialization_failed);
    }

    tls_socket socket;
    socket.pimpl_->socket_fd_ = socket_fd;
    socket.pimpl_->is_server_ = true;

    // Create SSL object from context
    SSL_CTX* ctx = static_cast<SSL_CTX*>(context.native_handle());
    socket.pimpl_->ssl_ = SSL_new(ctx);
    if (!socket.pimpl_->ssl_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    // Attach socket to SSL
    if (SSL_set_fd(socket.pimpl_->ssl_, socket_fd) != 1) {
        return std::unexpected(tls_error::initialization_failed);
    }

    // Perform handshake
    int result = SSL_accept(socket.pimpl_->ssl_);
    if (result != 1) {
        int err = SSL_get_error(socket.pimpl_->ssl_, result);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Non-blocking socket, handshake not complete
            return socket;
        }
        socket.pimpl_->last_error_ = get_ssl_error_string();
        return std::unexpected(tls_error::handshake_failed);
    }

    socket.pimpl_->handshake_complete_ = true;
    socket.pimpl_->peer_cert_ = extract_peer_cert_info(socket.pimpl_->ssl_);

    return socket;
#else
    (void)context;
    (void)socket_fd;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<tls_socket, tls_error>
tls_socket::connect(tls_context& context, int socket_fd,
                    std::string_view hostname) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!context.is_client()) {
        return std::unexpected(tls_error::initialization_failed);
    }

    tls_socket socket;
    socket.pimpl_->socket_fd_ = socket_fd;
    socket.pimpl_->is_server_ = false;
    socket.pimpl_->hostname_ = std::string(hostname);

    // Create SSL object from context
    SSL_CTX* ctx = static_cast<SSL_CTX*>(context.native_handle());
    socket.pimpl_->ssl_ = SSL_new(ctx);
    if (!socket.pimpl_->ssl_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    // Set SNI hostname
    if (!hostname.empty()) {
        SSL_set_tlsext_host_name(socket.pimpl_->ssl_,
                                  socket.pimpl_->hostname_.c_str());
        // Enable hostname verification
        SSL_set1_host(socket.pimpl_->ssl_, socket.pimpl_->hostname_.c_str());
    }

    // Attach socket to SSL
    if (SSL_set_fd(socket.pimpl_->ssl_, socket_fd) != 1) {
        return std::unexpected(tls_error::initialization_failed);
    }

    // Perform handshake
    int result = SSL_connect(socket.pimpl_->ssl_);
    if (result != 1) {
        int err = SSL_get_error(socket.pimpl_->ssl_, result);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Non-blocking socket, handshake not complete
            return socket;
        }
        socket.pimpl_->last_error_ = get_ssl_error_string();
        return std::unexpected(tls_error::handshake_failed);
    }

    socket.pimpl_->handshake_complete_ = true;
    socket.pimpl_->peer_cert_ = extract_peer_cert_info(socket.pimpl_->ssl_);

    return socket;
#else
    (void)context;
    (void)socket_fd;
    (void)hostname;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<tls_socket, tls_error>
tls_socket::create_pending(tls_context& context, int socket_fd,
                           bool is_server, std::string_view hostname) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    tls_socket socket;
    socket.pimpl_->socket_fd_ = socket_fd;
    socket.pimpl_->is_server_ = is_server;
    socket.pimpl_->hostname_ = std::string(hostname);

    SSL_CTX* ctx = static_cast<SSL_CTX*>(context.native_handle());
    socket.pimpl_->ssl_ = SSL_new(ctx);
    if (!socket.pimpl_->ssl_) {
        return std::unexpected(tls_error::initialization_failed);
    }

    if (!hostname.empty() && !is_server) {
        SSL_set_tlsext_host_name(socket.pimpl_->ssl_,
                                  socket.pimpl_->hostname_.c_str());
        SSL_set1_host(socket.pimpl_->ssl_, socket.pimpl_->hostname_.c_str());
    }

    if (SSL_set_fd(socket.pimpl_->ssl_, socket_fd) != 1) {
        return std::unexpected(tls_error::initialization_failed);
    }

    return socket;
#else
    (void)context;
    (void)socket_fd;
    (void)is_server;
    (void)hostname;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

tls_socket::handshake_status tls_socket::perform_handshake_step() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) {
        return handshake_status::failed;
    }

    if (pimpl_->handshake_complete_) {
        return handshake_status::complete;
    }

    int result;
    if (pimpl_->is_server_) {
        result = SSL_accept(pimpl_->ssl_);
    } else {
        result = SSL_connect(pimpl_->ssl_);
    }

    if (result == 1) {
        pimpl_->handshake_complete_ = true;
        pimpl_->peer_cert_ = extract_peer_cert_info(pimpl_->ssl_);
        return handshake_status::complete;
    }

    int err = SSL_get_error(pimpl_->ssl_, result);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            return handshake_status::want_read;
        case SSL_ERROR_WANT_WRITE:
            return handshake_status::want_write;
        default:
            pimpl_->last_error_ = get_ssl_error_string();
            return handshake_status::failed;
    }
#else
    return handshake_status::failed;
#endif
}

bool tls_socket::is_handshake_complete() const noexcept {
    return pimpl_->handshake_complete_;
}

std::expected<size_t, tls_error>
tls_socket::read(std::span<uint8_t> buffer) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_ || !pimpl_->handshake_complete_) {
        return std::unexpected(tls_error::handshake_failed);
    }

    int result = SSL_read(pimpl_->ssl_, buffer.data(),
                          static_cast<int>(buffer.size()));
    if (result > 0) {
        return static_cast<size_t>(result);
    }

    if (result == 0) {
        // Connection closed
        return 0;
    }

    int err = SSL_get_error(pimpl_->ssl_, result);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        // Would block - return 0 bytes read
        return 0;
    }

    pimpl_->last_error_ = get_ssl_error_string();
    return std::unexpected(tls_error::connection_closed);
#else
    (void)buffer;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<size_t, tls_error>
tls_socket::write(std::span<const uint8_t> data) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_ || !pimpl_->handshake_complete_) {
        return std::unexpected(tls_error::handshake_failed);
    }

    int result = SSL_write(pimpl_->ssl_, data.data(),
                           static_cast<int>(data.size()));
    if (result > 0) {
        return static_cast<size_t>(result);
    }

    int err = SSL_get_error(pimpl_->ssl_, result);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        return 0;
    }

    pimpl_->last_error_ = get_ssl_error_string();
    return std::unexpected(tls_error::connection_closed);
#else
    (void)data;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<std::vector<uint8_t>, tls_error>
tls_socket::read_all(size_t max_size) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    std::vector<uint8_t> result;
    std::array<uint8_t, 4096> buffer{};

    while (result.size() < max_size) {
        size_t to_read = std::min(buffer.size(), max_size - result.size());
        auto [status, bytes] = try_read(std::span(buffer.data(), to_read));

        if (status == io_status::success && bytes > 0) {
            result.insert(result.end(), buffer.begin(),
                          buffer.begin() + static_cast<std::ptrdiff_t>(bytes));
        } else if (status == io_status::want_read ||
                   status == io_status::want_write) {
            break;  // Would block
        } else if (status == io_status::closed) {
            break;  // Connection closed
        } else {
            return std::unexpected(tls_error::connection_closed);
        }
    }

    return result;
#else
    (void)max_size;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

std::expected<void, tls_error>
tls_socket::write_all(std::span<const uint8_t> data) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    size_t total_written = 0;

    while (total_written < data.size()) {
        auto remaining = data.subspan(total_written);
        auto result = write(remaining);

        if (!result) {
            return std::unexpected(result.error());
        }

        if (*result == 0) {
            // Would block - for blocking sockets this shouldn't happen
            continue;
        }

        total_written += *result;
    }

    return {};
#else
    (void)data;
    return std::unexpected(tls_error::initialization_failed);
#endif
}

bool tls_socket::has_pending_data() const noexcept {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) return false;
    return SSL_pending(pimpl_->ssl_) > 0;
#else
    return false;
#endif
}

std::expected<void, tls_error>
tls_socket::shutdown(std::chrono::milliseconds timeout) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) {
        return {};
    }

    auto start = std::chrono::steady_clock::now();

    // First shutdown attempt
    int result = SSL_shutdown(pimpl_->ssl_);

    while (result == 0) {
        // Check timeout
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            break;
        }

        // Wait for peer's close_notify
        result = SSL_shutdown(pimpl_->ssl_);
    }

    return {};
#else
    (void)timeout;
    return {};
#endif
}

void tls_socket::close() {
    pimpl_->close();
}

bool tls_socket::is_open() const noexcept {
    return pimpl_->ssl_ != nullptr && pimpl_->socket_fd_ >= 0;
}

int tls_socket::socket_fd() const noexcept {
    return pimpl_->socket_fd_;
}

std::optional<security::certificate_info>
tls_socket::peer_certificate() const noexcept {
    return pimpl_->peer_cert_;
}

std::string tls_socket::protocol_version() const {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) return "unknown";
    return SSL_get_version(pimpl_->ssl_);
#else
    return "unknown";
#endif
}

std::string tls_socket::cipher_suite() const {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) return "unknown";
    const SSL_CIPHER* cipher = SSL_get_current_cipher(pimpl_->ssl_);
    if (!cipher) return "unknown";
    return SSL_CIPHER_get_name(cipher);
#else
    return "unknown";
#endif
}

bool tls_socket::is_session_resumed() const noexcept {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_) return false;
    return SSL_session_reused(pimpl_->ssl_) != 0;
#else
    return false;
#endif
}

std::string tls_socket::last_error_message() const {
    return pimpl_->last_error_;
}

std::pair<tls_socket::io_status, size_t>
tls_socket::try_read(std::span<uint8_t> buffer) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_ || !pimpl_->handshake_complete_) {
        return {io_status::error, 0};
    }

    int result = SSL_read(pimpl_->ssl_, buffer.data(),
                          static_cast<int>(buffer.size()));
    if (result > 0) {
        return {io_status::success, static_cast<size_t>(result)};
    }

    if (result == 0) {
        return {io_status::closed, 0};
    }

    int err = SSL_get_error(pimpl_->ssl_, result);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            return {io_status::want_read, 0};
        case SSL_ERROR_WANT_WRITE:
            return {io_status::want_write, 0};
        default:
            pimpl_->last_error_ = get_ssl_error_string();
            return {io_status::error, 0};
    }
#else
    (void)buffer;
    return {io_status::error, 0};
#endif
}

std::pair<tls_socket::io_status, size_t>
tls_socket::try_write(std::span<const uint8_t> data) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
    if (!pimpl_->ssl_ || !pimpl_->handshake_complete_) {
        return {io_status::error, 0};
    }

    int result = SSL_write(pimpl_->ssl_, data.data(),
                           static_cast<int>(data.size()));
    if (result > 0) {
        return {io_status::success, static_cast<size_t>(result)};
    }

    int err = SSL_get_error(pimpl_->ssl_, result);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            return {io_status::want_read, 0};
        case SSL_ERROR_WANT_WRITE:
            return {io_status::want_write, 0};
        default:
            pimpl_->last_error_ = get_ssl_error_string();
            return {io_status::error, 0};
    }
#else
    (void)data;
    return {io_status::error, 0};
#endif
}

}  // namespace pacs::bridge::security
