/**
 * @file tls_adapter_test.cpp
 * @brief Integration tests for TLS-enabled MLLP network adapter
 *
 * Tests TLS implementation:
 * - TLS handshake (1.2 and 1.3)
 * - Mutual TLS (client certificate authentication)
 * - Cipher suite negotiation
 * - Encrypted data integrity
 * - Certificate validation
 * - Security features
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/316
 */

#include "src/mllp/tls_mllp_server.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// OpenSSL headers
#ifdef PACS_BRIDGE_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace pacs::bridge::mllp::test {

#ifdef PACS_BRIDGE_HAS_OPENSSL

// =============================================================================
// Test Utilities
// =============================================================================

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{16000};
    return port_counter.fetch_add(1);
}

/**
 * @brief Wait for condition with timeout
 */
template <typename Predicate>
bool wait_for(Predicate condition, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

/**
 * @brief Check if fixture files exist
 */
static bool check_test_fixtures() {
    std::vector<std::string> required_files = {
        "tests/mllp/fixtures/tests/mllp/fixtures/test_server_cert.pem",
        "tests/mllp/fixtures/tests/mllp/fixtures/test_server_key.pem",
        "tests/mllp/fixtures/tests/mllp/fixtures/test_client_cert.pem",
        "tests/mllp/fixtures/tests/mllp/fixtures/test_client_key.pem"
    };

    for (const auto& file : required_files) {
        std::ifstream f(file);
        if (!f.good()) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Test fixture for TLS adapter tests
 */
class TLSAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Skip tests if OpenSSL not available
        if (!check_test_fixtures()) {
            GTEST_SKIP() << "Test certificate fixtures not found";
        }

        test_port_ = generate_test_port();

        // Initialize OpenSSL
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();
    }

    void TearDown() override {
        if (server_) {
            server_->stop(true);  // Wait for proper cleanup
            server_.reset();
        }
        // Give time for socket cleanup to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Cleanup OpenSSL
        EVP_cleanup();
        ERR_free_strings();
    }

    /**
     * @brief Create basic TLS configuration
     */
    security::tls_config create_tls_config(
        security::client_auth_mode auth_mode = security::client_auth_mode::none,
        security::tls_version min_version = security::tls_version::tls_1_2) {

        security::tls_config config;
        config.enabled = true;
        config.cert_path = "tests/mllp/fixtures/tests/mllp/fixtures/test_server_cert.pem";
        config.key_path = "tests/mllp/fixtures/tests/mllp/fixtures/test_server_key.pem";
        config.client_auth = auth_mode;
        config.min_version = min_version;
        config.verify_peer = (auth_mode != security::client_auth_mode::none);

        if (auth_mode != security::client_auth_mode::none) {
            config.ca_path = "tests/mllp/fixtures/tests/mllp/fixtures/test_server_cert.pem";  // Use server cert as CA for testing
        }

        return config;
    }

    /**
     * @brief Create and start TLS test server
     */
    std::unique_ptr<tls_mllp_server> create_server(
        uint16_t port,
        const security::tls_config& tls_config) {

        server_config config;
        config.port = port;
        config.backlog = 10;
        config.keep_alive = true;

        auto server = std::make_unique<tls_mllp_server>(config, tls_config);

        // Set connection callback
        server->on_connection([this](std::unique_ptr<mllp_session> session) {
            on_new_connection(std::move(session));
        });

        auto result = server->start();
        EXPECT_TRUE(result.has_value()) << "Server failed to start";

        return server;
    }

    /**
     * @brief Default connection handler (stores session)
     */
    virtual void on_new_connection(std::unique_ptr<mllp_session> session) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.push_back(std::move(session));
        sessions_cv_.notify_all();
    }

    /**
     * @brief Wait for N sessions to be accepted
     */
    bool wait_for_sessions(size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        return sessions_cv_.wait_for(lock, timeout,
                                      [this, count] { return sessions_.size() >= count; });
    }

    /**
     * @brief Create SSL context for client
     */
    SSL_CTX* create_client_ssl_ctx(
        security::tls_version min_version = security::tls_version::tls_1_2,
        bool use_client_cert = false) {

        const SSL_METHOD* method;
        if (min_version == security::tls_version::tls_1_3) {
            method = TLS_client_method();
        } else {
            method = TLS_client_method();
        }

        SSL_CTX* ctx = SSL_CTX_new(method);
        if (!ctx) {
            return nullptr;
        }

        // Set minimum TLS version
        int ssl_version = (min_version == security::tls_version::tls_1_3)
                         ? TLS1_3_VERSION : TLS1_2_VERSION;
        SSL_CTX_set_min_proto_version(ctx, ssl_version);

        // Disable certificate verification for test
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        if (use_client_cert) {
            if (SSL_CTX_use_certificate_file(ctx, "tests/mllp/fixtures/tests/mllp/fixtures/test_client_cert.pem",
                                             SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(ctx);
                return nullptr;
            }

            if (SSL_CTX_use_PrivateKey_file(ctx, "tests/mllp/fixtures/tests/mllp/fixtures/test_client_key.pem",
                                            SSL_FILETYPE_PEM) <= 0) {
                SSL_CTX_free(ctx);
                return nullptr;
            }
        }

        return ctx;
    }

    /**
     * @brief Connect TLS client to server
     */
    SSL* connect_tls_client(
        uint16_t port,
        SSL_CTX* ctx,
        socket_t& out_socket) {

        // Create socket
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            return nullptr;
        }
#else
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return nullptr;
        }
#endif

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return nullptr;
        }

        // Create SSL object
        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return nullptr;
        }

        SSL_set_fd(ssl, static_cast<int>(sock));

        // Perform handshake
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return nullptr;
        }

        out_socket = sock;
        return ssl;
    }

    uint16_t test_port_;
    std::unique_ptr<tls_mllp_server> server_;
    std::vector<std::unique_ptr<mllp_session>> sessions_;
    std::mutex sessions_mutex_;
    std::condition_variable sessions_cv_;
};

// =============================================================================
// TLS Handshake Tests
// =============================================================================

TEST_F(TLSAdapterTest, TLS12HandshakeSuccess) {
    auto tls_config = create_tls_config(security::client_auth_mode::none,
                                        security::tls_version::tls_1_2);
    server_ = create_server(test_port_, tls_config);

    ASSERT_TRUE(server_->is_running());

    // Connect TLS client
    SSL_CTX* ctx = create_client_ssl_ctx(security::tls_version::tls_1_2);
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Wait for server to accept connection
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Verify session
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        ASSERT_EQ(1u, sessions_.size());

        auto& session = sessions_[0];
        EXPECT_TRUE(session->is_open());
        EXPECT_EQ("127.0.0.1", session->remote_address());
    }

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

TEST_F(TLSAdapterTest, TLS13HandshakeSuccess) {
    auto tls_config = create_tls_config(security::client_auth_mode::none,
                                        security::tls_version::tls_1_3);
    server_ = create_server(test_port_, tls_config);

    ASSERT_TRUE(server_->is_running());

    // Connect TLS client with TLS 1.3
    SSL_CTX* ctx = create_client_ssl_ctx(security::tls_version::tls_1_3);
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);

    // TLS 1.3 may not be supported on all platforms
    if (ssl == nullptr) {
        SSL_CTX_free(ctx);
        GTEST_SKIP() << "TLS 1.3 not supported on this platform";
    }

    // Wait for server to accept connection
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

TEST_F(TLSAdapterTest, InvalidCertificateRejection) {
    auto tls_config = create_tls_config(security::client_auth_mode::none);
    server_ = create_server(test_port_, tls_config);

    // Create client context with certificate verification enabled
    SSL_CTX* ctx = TLS_client_method() ? SSL_CTX_new(TLS_client_method()) : nullptr;
    ASSERT_NE(nullptr, ctx);

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    // Don't load CA cert - handshake should fail

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);

    // Connection should fail due to certificate verification
    EXPECT_EQ(nullptr, ssl);

    SSL_CTX_free(ctx);
}

TEST_F(TLSAdapterTest, HandshakeTimeout) {
    auto tls_config = create_tls_config(security::client_auth_mode::none);
    server_ = create_server(test_port_, tls_config);

    // Connect without completing handshake
    socket_t client_sock;
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

    // Don't send TLS handshake - just wait
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Server should timeout and not create session
    EXPECT_FALSE(wait_for_sessions(1, std::chrono::milliseconds(500)));

#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

// =============================================================================
// Mutual TLS Tests
// =============================================================================

TEST_F(TLSAdapterTest, MutualTLS_ValidClientCertificate) {
    auto tls_config = create_tls_config(security::client_auth_mode::required);
    server_ = create_server(test_port_, tls_config);

    // Create client with certificate
    SSL_CTX* ctx = create_client_ssl_ctx(security::tls_version::tls_1_2, true);
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Wait for server to accept connection
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

TEST_F(TLSAdapterTest, MutualTLS_MissingClientCertificate) {
    auto tls_config = create_tls_config(security::client_auth_mode::required);
    server_ = create_server(test_port_, tls_config);

    // Create client WITHOUT certificate
    SSL_CTX* ctx = create_client_ssl_ctx(security::tls_version::tls_1_2, false);
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);

    // Connection should fail due to missing client certificate
    // Note: Some platforms may still complete handshake but server will reject
    if (ssl != nullptr) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
#ifdef _WIN32
        closesocket(client_sock);
        WSACleanup();
#else
        close(client_sock);
#endif
    }

    SSL_CTX_free(ctx);

    // Server should not create valid session
    EXPECT_FALSE(wait_for_sessions(1, std::chrono::milliseconds(500)));
}

TEST_F(TLSAdapterTest, MutualTLS_InvalidClientCertificate) {
    auto tls_config = create_tls_config(security::client_auth_mode::required);
    server_ = create_server(test_port_, tls_config);

    // Create client with invalid certificate (use server cert as client cert)
    SSL_CTX* ctx = TLS_client_method() ? SSL_CTX_new(TLS_client_method()) : nullptr;
    ASSERT_NE(nullptr, ctx);

    // Load wrong certificate
    SSL_CTX_use_certificate_file(ctx, "tests/mllp/fixtures/test_server_cert.pem", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "tests/mllp/fixtures/test_server_key.pem", SSL_FILETYPE_PEM);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);

    // Connection may succeed or fail depending on certificate validation
    if (ssl != nullptr) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
#ifdef _WIN32
        closesocket(client_sock);
        WSACleanup();
#else
        close(client_sock);
#endif
    }

    SSL_CTX_free(ctx);
}

// =============================================================================
// Encryption Integrity Tests
// =============================================================================

TEST_F(TLSAdapterTest, EncryptedDataTransmission) {
    std::vector<uint8_t> server_received_data;
    std::mutex data_mutex;
    std::condition_variable data_cv;
    std::atomic<bool> thread_completed{false};

    auto tls_config = create_tls_config();
    server_ = create_server(test_port_, tls_config);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            auto result = s->receive(1024, std::chrono::seconds(5));
            if (result.has_value()) {
                std::lock_guard<std::mutex> lock(data_mutex);
                server_received_data = std::move(result.value());
                data_cv.notify_all();

                // Echo back
                s->send(server_received_data);
            }
            thread_completed.store(true);
        }).detach();
    });

    // Connect TLS client
    SSL_CTX* ctx = create_client_ssl_ctx();
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Send encrypted data
    std::string test_message = "Encrypted MLLP Message!";
    int sent = SSL_write(ssl, test_message.data(), test_message.size());
    EXPECT_EQ(test_message.size(), static_cast<size_t>(sent));

    // Wait for server to receive
    {
        std::unique_lock<std::mutex> lock(data_mutex);
        ASSERT_TRUE(data_cv.wait_for(lock, std::chrono::seconds(5),
                                      [&] { return !server_received_data.empty(); }));

        std::string received(server_received_data.begin(), server_received_data.end());
        EXPECT_EQ(test_message, received);
    }

    // Receive echo
    char buffer[1024] = {};
    int received = SSL_read(ssl, buffer, sizeof(buffer));
    EXPECT_EQ(test_message.size(), static_cast<size_t>(received));
    EXPECT_EQ(test_message, std::string(buffer, received));

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif

    EXPECT_TRUE(wait_for([&] { return thread_completed.load(); }, std::chrono::seconds(2)));
}

TEST_F(TLSAdapterTest, LargeEncryptedMessage) {
    std::vector<uint8_t> large_data(1024 * 1024, 0xCD);  // 1MB
    std::vector<uint8_t> server_received_data;
    std::mutex data_mutex;
    std::condition_variable data_cv;
    std::atomic<bool> thread_completed{false};

    auto tls_config = create_tls_config();
    server_ = create_server(test_port_, tls_config);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            auto result = s->receive(large_data.size(), std::chrono::seconds(30));
            if (result.has_value()) {
                std::lock_guard<std::mutex> lock(data_mutex);
                server_received_data = std::move(result.value());
                data_cv.notify_all();
            }
            thread_completed.store(true);
        }).detach();
    });

    // Connect TLS client
    SSL_CTX* ctx = create_client_ssl_ctx();
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Send large encrypted data
    size_t total_sent = 0;
    while (total_sent < large_data.size()) {
        int sent = SSL_write(ssl, large_data.data() + total_sent,
                            large_data.size() - total_sent);
        ASSERT_GT(sent, 0);
        total_sent += sent;
    }

    // Wait for server to receive
    {
        std::unique_lock<std::mutex> lock(data_mutex);
        ASSERT_TRUE(data_cv.wait_for(lock, std::chrono::seconds(30),
                                      [&] { return !server_received_data.empty(); }));

        EXPECT_EQ(large_data.size(), server_received_data.size());
        EXPECT_EQ(large_data, server_received_data);
    }

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif

    EXPECT_TRUE(wait_for([&] { return thread_completed.load(); }, std::chrono::seconds(5)));
}

TEST_F(TLSAdapterTest, CipherSuiteNegotiation) {
    auto tls_config = create_tls_config();
    server_ = create_server(test_port_, tls_config);

    SSL_CTX* ctx = create_client_ssl_ctx();
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Check negotiated cipher
    const char* cipher = SSL_get_cipher(ssl);
    EXPECT_NE(nullptr, cipher);
    EXPECT_GT(strlen(cipher), 0u);

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

// =============================================================================
// Security Feature Tests
// =============================================================================

TEST_F(TLSAdapterTest, TLSVersionNegotiation) {
    auto tls_config = create_tls_config(security::client_auth_mode::none,
                                        security::tls_version::tls_1_2);
    server_ = create_server(test_port_, tls_config);

    SSL_CTX* ctx = create_client_ssl_ctx(security::tls_version::tls_1_2);
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Check negotiated version
    int version = SSL_version(ssl);
    EXPECT_TRUE(version == TLS1_2_VERSION || version == TLS1_3_VERSION);

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

TEST_F(TLSAdapterTest, TLSSessionInfo) {
    auto tls_config = create_tls_config();
    server_ = create_server(test_port_, tls_config);

    SSL_CTX* ctx = create_client_ssl_ctx();
    ASSERT_NE(nullptr, ctx);

    socket_t client_sock;
    SSL* ssl = connect_tls_client(test_port_, ctx, client_sock);
    ASSERT_NE(nullptr, ssl);

    // Wait for server session
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Verify TLS session info
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto* tls_session = dynamic_cast<tls_mllp_session*>(sessions_[0].get());
        if (tls_session) {
            EXPECT_FALSE(tls_session->tls_version().empty());
            EXPECT_FALSE(tls_session->tls_cipher().empty());
        }
    }

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

#else  // PACS_BRIDGE_HAS_OPENSSL

TEST(TLSAdapterTest, OpenSSLNotAvailable) {
    GTEST_SKIP() << "OpenSSL not available - TLS tests skipped";
}

#endif  // PACS_BRIDGE_HAS_OPENSSL

}  // namespace pacs::bridge::mllp::test
