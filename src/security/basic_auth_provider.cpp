/**
 * @file basic_auth_provider.cpp
 * @brief HTTP Basic authentication provider implementation
 *
 * Implements HTTP Basic authentication with Base64 encoding
 * for EMR systems that don't support OAuth2.
 *
 * @see include/pacs/bridge/security/basic_auth_provider.h
 */

#include "pacs/bridge/security/basic_auth_provider.h"

#include <array>
#include <cstdint>

namespace pacs::bridge::security {

// =============================================================================
// Base64 Encoding
// =============================================================================

namespace {

constexpr std::array<char, 64> BASE64_CHARS = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

constexpr std::array<int, 256> make_decode_table() {
    std::array<int, 256> table{};
    for (auto& v : table) v = -1;
    for (size_t i = 0; i < BASE64_CHARS.size(); ++i) {
        table[static_cast<unsigned char>(BASE64_CHARS[i])] = static_cast<int>(i);
    }
    return table;
}

constexpr auto BASE64_DECODE_TABLE = make_decode_table();

}  // namespace

std::string base64_encode(std::string_view data) {
    if (data.empty()) {
        return {};
    }

    std::string result;
    size_t len = data.size();
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t octet_a = static_cast<unsigned char>(data[i]);
        uint32_t octet_b = (i + 1 < len) ? static_cast<unsigned char>(data[i + 1]) : 0;
        uint32_t octet_c = (i + 2 < len) ? static_cast<unsigned char>(data[i + 2]) : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result += BASE64_CHARS[(triple >> 18) & 0x3F];
        result += BASE64_CHARS[(triple >> 12) & 0x3F];
        result += (i + 1 < len) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? BASE64_CHARS[triple & 0x3F] : '=';
    }

    return result;
}

std::string base64_decode(std::string_view encoded) {
    if (encoded.empty()) {
        return {};
    }

    // Calculate output size
    size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') {
        padding++;
        if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') {
            padding++;
        }
    }

    size_t output_size = (encoded.size() / 4) * 3 - padding;
    std::string result;
    result.reserve(output_size);

    uint32_t buffer = 0;
    int bits_collected = 0;

    for (char c : encoded) {
        if (c == '=') break;

        int value = BASE64_DECODE_TABLE[static_cast<unsigned char>(c)];
        if (value < 0) {
            // Invalid character - return empty
            return {};
        }

        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits_collected += 6;

        if (bits_collected >= 8) {
            bits_collected -= 8;
            result += static_cast<char>((buffer >> bits_collected) & 0xFF);
        }
    }

    return result;
}

// =============================================================================
// Basic Auth Provider Implementation
// =============================================================================

basic_auth_provider::basic_auth_provider(std::string_view username,
                                         std::string_view password)
    : username_(username), password_(password) {
    update_header();
}

basic_auth_provider::basic_auth_provider(const basic_auth_config& config)
    : username_(config.username), password_(config.password) {
    update_header();
}

auto basic_auth_provider::get_authorization_header()
    -> std::expected<std::string, oauth2_error> {
    if (username_.empty() || password_.empty()) {
        return std::unexpected(oauth2_error::invalid_credentials);
    }

    if (!header_valid_) {
        update_header();
    }

    return cached_header_;
}

bool basic_auth_provider::is_authenticated() const noexcept {
    return !username_.empty() && !password_.empty();
}

std::string_view basic_auth_provider::auth_type() const noexcept {
    return "basic";
}

void basic_auth_provider::update_credentials(std::string_view username,
                                             std::string_view password) {
    username_ = username;
    password_ = password;
    header_valid_ = false;
}

void basic_auth_provider::invalidate() noexcept {
    username_.clear();
    password_.clear();
    cached_header_.clear();
    header_valid_ = false;
}

void basic_auth_provider::update_header() const {
    std::string credentials = username_ + ":" + password_;
    cached_header_ = "Basic " + base64_encode(credentials);
    header_valid_ = true;
}

}  // namespace pacs::bridge::security
