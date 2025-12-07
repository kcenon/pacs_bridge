/**
 * @file hl7_types.cpp
 * @brief HL7 type implementations
 */

#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// hl7_timestamp Implementation
// =============================================================================

std::chrono::system_clock::time_point hl7_timestamp::to_time_point() const {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;

    std::time_t time = std::mktime(&tm);
    auto tp = std::chrono::system_clock::from_time_t(time);

    // Add milliseconds
    tp += std::chrono::milliseconds(millisecond);

    // Apply timezone offset if present
    if (timezone_offset_minutes.has_value()) {
        // Local time was assumed, adjust for specified timezone
        auto offset = std::chrono::minutes(timezone_offset_minutes.value());
        tp -= offset;  // Convert to UTC
    }

    return tp;
}

hl7_timestamp hl7_timestamp::from_time_point(
    std::chrono::system_clock::time_point tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);

    hl7_timestamp ts;
    ts.year = tm.tm_year + 1900;
    ts.month = tm.tm_mon + 1;
    ts.day = tm.tm_mday;
    ts.hour = tm.tm_hour;
    ts.minute = tm.tm_min;
    ts.second = tm.tm_sec;

    // Extract milliseconds
    auto time_since_epoch = tp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_since_epoch - seconds);
    ts.millisecond = static_cast<int>(millis.count());

    return ts;
}

std::optional<hl7_timestamp> hl7_timestamp::parse(std::string_view ts_string) {
    if (ts_string.empty() || ts_string.length() < 4) {
        return std::nullopt;
    }

    hl7_timestamp ts;

    // Parse year (YYYY)
    auto parse_int = [](std::string_view sv, int& out) -> bool {
        auto result = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        return result.ec == std::errc{};
    };

    if (!parse_int(ts_string.substr(0, 4), ts.year)) {
        return std::nullopt;
    }

    if (ts_string.length() >= 6) {
        if (!parse_int(ts_string.substr(4, 2), ts.month)) {
            return std::nullopt;
        }
    }

    if (ts_string.length() >= 8) {
        if (!parse_int(ts_string.substr(6, 2), ts.day)) {
            return std::nullopt;
        }
    }

    if (ts_string.length() >= 10) {
        if (!parse_int(ts_string.substr(8, 2), ts.hour)) {
            return std::nullopt;
        }
    }

    if (ts_string.length() >= 12) {
        if (!parse_int(ts_string.substr(10, 2), ts.minute)) {
            return std::nullopt;
        }
    }

    if (ts_string.length() >= 14) {
        if (!parse_int(ts_string.substr(12, 2), ts.second)) {
            return std::nullopt;
        }
    }

    // Handle fractional seconds (.FFFF)
    size_t pos = 14;
    if (ts_string.length() > pos && ts_string[pos] == '.') {
        ++pos;
        size_t frac_start = pos;
        while (pos < ts_string.length() && std::isdigit(ts_string[pos])) {
            ++pos;
        }
        if (pos > frac_start) {
            std::string frac_str(ts_string.substr(frac_start, pos - frac_start));
            // Pad or truncate to 3 digits for milliseconds
            while (frac_str.length() < 3) frac_str += '0';
            frac_str = frac_str.substr(0, 3);
            parse_int(frac_str, ts.millisecond);
        }
    }

    // Handle timezone (+/-ZZZZ)
    if (pos < ts_string.length()) {
        char sign = ts_string[pos];
        if (sign == '+' || sign == '-') {
            ++pos;
            if (ts_string.length() >= pos + 4) {
                int tz_hours = 0, tz_mins = 0;
                if (parse_int(ts_string.substr(pos, 2), tz_hours) &&
                    parse_int(ts_string.substr(pos + 2, 2), tz_mins)) {
                    int offset = tz_hours * 60 + tz_mins;
                    if (sign == '-') offset = -offset;
                    ts.timezone_offset_minutes = offset;
                }
            }
        }
    }

    return ts;
}

std::string hl7_timestamp::to_string(int precision) const {
    std::ostringstream oss;
    oss << std::setfill('0');

    oss << std::setw(4) << year;

    if (precision >= 6) {
        oss << std::setw(2) << month;
    }

    if (precision >= 8) {
        oss << std::setw(2) << day;
    }

    if (precision >= 10) {
        oss << std::setw(2) << hour;
    }

    if (precision >= 12) {
        oss << std::setw(2) << minute;
    }

    if (precision >= 14) {
        oss << std::setw(2) << second;
    }

    if (millisecond > 0) {
        oss << '.' << std::setw(3) << millisecond;
    }

    if (timezone_offset_minutes.has_value()) {
        int offset = timezone_offset_minutes.value();
        char sign = offset >= 0 ? '+' : '-';
        offset = std::abs(offset);
        int tz_hours = offset / 60;
        int tz_mins = offset % 60;
        oss << sign << std::setw(2) << tz_hours << std::setw(2) << tz_mins;
    }

    return oss.str();
}

hl7_timestamp hl7_timestamp::now() {
    return from_time_point(std::chrono::system_clock::now());
}

bool hl7_timestamp::is_valid() const noexcept {
    if (year < 1900 || year > 2100) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;
    if (millisecond < 0 || millisecond > 999) return false;
    return true;
}

// =============================================================================
// hl7_person_name Implementation
// =============================================================================

std::string hl7_person_name::display_name() const {
    std::string result;
    if (!given_name.empty()) {
        result += given_name;
    }
    if (!family_name.empty()) {
        if (!result.empty()) result += ' ';
        result += family_name;
    }
    return result;
}

std::string hl7_person_name::formatted_name() const {
    std::string result = family_name;
    if (!given_name.empty()) {
        result += ", ";
        result += given_name;
        if (!middle_name.empty()) {
            result += ' ';
            result += middle_name;
        }
    }
    return result;
}

std::string hl7_person_name::to_dicom_pn() const {
    // DICOM PN format: FamilyName^GivenName^MiddleName^Prefix^Suffix
    std::string result = family_name;
    result += '^';
    result += given_name;
    result += '^';
    result += middle_name;
    result += '^';
    result += prefix;
    result += '^';
    result += suffix;

    // Trim trailing carets
    while (!result.empty() && result.back() == '^') {
        result.pop_back();
    }

    return result;
}

hl7_person_name hl7_person_name::from_dicom_pn(std::string_view pn) {
    hl7_person_name name;

    std::vector<std::string_view> parts;
    size_t start = 0;
    size_t pos = 0;

    while (pos <= pn.length()) {
        if (pos == pn.length() || pn[pos] == '^') {
            parts.push_back(pn.substr(start, pos - start));
            start = pos + 1;
        }
        ++pos;
    }

    if (parts.size() > 0) name.family_name = std::string(parts[0]);
    if (parts.size() > 1) name.given_name = std::string(parts[1]);
    if (parts.size() > 2) name.middle_name = std::string(parts[2]);
    if (parts.size() > 3) name.prefix = std::string(parts[3]);
    if (parts.size() > 4) name.suffix = std::string(parts[4]);

    return name;
}

// =============================================================================
// hl7_address Implementation
// =============================================================================

std::string hl7_address::formatted() const {
    std::string result;

    if (!street1.empty()) {
        result += street1;
    }
    if (!street2.empty()) {
        if (!result.empty()) result += ", ";
        result += street2;
    }
    if (!city.empty()) {
        if (!result.empty()) result += ", ";
        result += city;
    }
    if (!state.empty()) {
        if (!result.empty()) result += ", ";
        result += state;
    }
    if (!postal_code.empty()) {
        if (!result.empty()) result += ' ';
        result += postal_code;
    }
    if (!country.empty()) {
        if (!result.empty()) result += ", ";
        result += country;
    }

    return result;
}

}  // namespace pacs::bridge::hl7
