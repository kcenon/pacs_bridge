/**
 * @file cache_test.cpp
 * @brief Comprehensive unit tests for patient data cache module
 *
 * Tests for patient cache operations, TTL management, LRU eviction,
 * aliases, and statistics. Target coverage: >= 80%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/21
 */

#include "pacs/bridge/cache/patient_cache.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::cache::test {

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        if (test_func()) {                                                     \
            std::cout << "  PASSED" << std::endl;                              \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// Helper to create test patient
mapping::dicom_patient create_test_patient(const std::string& id,
                                            const std::string& name = "DOE^JOHN") {
    mapping::dicom_patient patient;
    patient.patient_id = id;
    patient.patient_name = name;
    patient.patient_birth_date = "19800515";
    patient.patient_sex = "M";
    patient.issuer_of_patient_id = "HOSPITAL";
    return patient;
}

// =============================================================================
// Cache Error Tests
// =============================================================================

bool test_cache_error_codes() {
    TEST_ASSERT(to_error_code(cache_error::not_found) == -920,
                "not_found should be -920");
    TEST_ASSERT(to_error_code(cache_error::cache_disabled) == -925,
                "cache_disabled should be -925");

    TEST_ASSERT(std::string(to_string(cache_error::expired)) ==
                    "Cache entry has expired",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(cache_error::invalid_key)) ==
                    "Invalid cache key format",
                "Error message should match");

    return true;
}

// =============================================================================
// Cache Entry Metadata Tests
// =============================================================================

bool test_cache_entry_metadata_is_expired() {
    cache_entry_metadata meta;
    meta.created_at = std::chrono::system_clock::now();
    meta.ttl = std::chrono::seconds{10};

    TEST_ASSERT(!meta.is_expired(), "New entry should not be expired");

    // Create expired entry
    cache_entry_metadata expired_meta;
    expired_meta.created_at = std::chrono::system_clock::now() - std::chrono::seconds{20};
    expired_meta.ttl = std::chrono::seconds{10};

    TEST_ASSERT(expired_meta.is_expired(), "Old entry should be expired");

    return true;
}

bool test_cache_entry_metadata_time_remaining() {
    cache_entry_metadata meta;
    meta.created_at = std::chrono::system_clock::now();
    meta.ttl = std::chrono::seconds{60};

    auto remaining = meta.time_remaining();
    TEST_ASSERT(remaining.count() > 55, "Should have about 60 seconds remaining");
    TEST_ASSERT(remaining.count() <= 60, "Should not exceed TTL");

    // Expired entry
    cache_entry_metadata expired;
    expired.created_at = std::chrono::system_clock::now() - std::chrono::seconds{120};
    expired.ttl = std::chrono::seconds{60};

    TEST_ASSERT(expired.time_remaining().count() == 0, "Expired entry should have 0 remaining");

    return true;
}

// =============================================================================
// Cache Configuration Tests
// =============================================================================

bool test_cache_config_defaults() {
    patient_cache_config config;

    TEST_ASSERT(config.max_entries == 10000, "Default max entries should be 10000");
    TEST_ASSERT(config.default_ttl == std::chrono::seconds{3600}, "Default TTL should be 1 hour");
    TEST_ASSERT(config.enabled, "Cache should be enabled by default");
    TEST_ASSERT(config.auto_evict, "Auto evict should be enabled by default");
    TEST_ASSERT(config.lru_eviction, "LRU eviction should be enabled by default");
    TEST_ASSERT(config.enable_statistics, "Statistics should be enabled by default");

    return true;
}

bool test_cache_custom_config() {
    patient_cache_config config;
    config.max_entries = 100;
    config.default_ttl = std::chrono::seconds{300};  // 5 minutes
    config.enabled = true;
    config.lru_eviction = false;

    patient_cache cache(config);

    auto retrieved = cache.config();
    TEST_ASSERT(retrieved.max_entries == 100, "Max entries should match");
    TEST_ASSERT(retrieved.default_ttl == std::chrono::seconds{300}, "TTL should match");

    return true;
}

// =============================================================================
// Basic Cache Operations Tests
// =============================================================================

bool test_cache_put_and_get() {
    patient_cache cache;

    auto patient = create_test_patient("12345", "DOE^JOHN^WILLIAM");
    cache.put("12345", patient);

    auto result = cache.get("12345");
    TEST_ASSERT(result.has_value(), "Should find cached patient");
    TEST_ASSERT(result->patient_id == "12345", "Patient ID should match");
    TEST_ASSERT(result->patient_name == "DOE^JOHN^WILLIAM", "Name should match");

    return true;
}

bool test_cache_get_not_found() {
    patient_cache cache;

    auto result = cache.get("nonexistent");
    TEST_ASSERT(!result.has_value(), "Should not find non-existent patient");
    TEST_ASSERT(result.error() == cache_error::not_found, "Error should be not_found");

    return true;
}

bool test_cache_peek() {
    patient_cache cache;

    auto patient = create_test_patient("12345");
    cache.put("12345", patient);

    auto result = cache.peek("12345");
    TEST_ASSERT(result.has_value(), "Should find cached patient");
    TEST_ASSERT(result->patient_id == "12345", "Patient ID should match");

    // Peek should not update access time (we can't directly verify this
    // but we can verify peek works)
    auto result2 = cache.peek("12345");
    TEST_ASSERT(result2.has_value(), "Second peek should also work");

    return true;
}

bool test_cache_contains() {
    patient_cache cache;

    auto patient = create_test_patient("12345");
    cache.put("12345", patient);

    TEST_ASSERT(cache.contains("12345"), "Should contain cached key");
    TEST_ASSERT(!cache.contains("nonexistent"), "Should not contain non-existent key");

    return true;
}

bool test_cache_remove() {
    patient_cache cache;

    auto patient = create_test_patient("12345");
    cache.put("12345", patient);

    TEST_ASSERT(cache.contains("12345"), "Should contain key before remove");

    bool removed = cache.remove("12345");
    TEST_ASSERT(removed, "Remove should succeed");
    TEST_ASSERT(!cache.contains("12345"), "Should not contain key after remove");

    bool not_found = cache.remove("nonexistent");
    TEST_ASSERT(!not_found, "Remove of non-existent should return false");

    return true;
}

bool test_cache_update() {
    patient_cache cache;

    auto patient1 = create_test_patient("12345", "DOE^JOHN");
    cache.put("12345", patient1);

    auto patient2 = create_test_patient("12345", "DOE^JOHN^UPDATED");
    cache.put("12345", patient2);  // Update

    auto result = cache.get("12345");
    TEST_ASSERT(result.has_value(), "Should find updated patient");
    TEST_ASSERT(result->patient_name == "DOE^JOHN^UPDATED", "Name should be updated");
    TEST_ASSERT(cache.size() == 1, "Size should still be 1");

    return true;
}

bool test_cache_clear() {
    patient_cache cache;

    for (int i = 0; i < 10; i++) {
        cache.put(std::to_string(i), create_test_patient(std::to_string(i)));
    }

    TEST_ASSERT(cache.size() == 10, "Should have 10 entries");

    cache.clear();

    TEST_ASSERT(cache.size() == 0, "Should be empty after clear");
    TEST_ASSERT(cache.empty(), "Empty check should return true");

    return true;
}

// =============================================================================
// TTL Tests
// =============================================================================

bool test_cache_custom_ttl() {
    patient_cache_config config;
    config.default_ttl = std::chrono::seconds{60};

    patient_cache cache(config);

    // Put with custom shorter TTL
    auto patient = create_test_patient("12345");
    cache.put("12345", patient, std::chrono::seconds{2});

    auto result1 = cache.get("12345");
    TEST_ASSERT(result1.has_value(), "Should find patient immediately");

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::seconds{3});

    auto result2 = cache.get("12345");
    TEST_ASSERT(!result2.has_value(), "Should not find expired patient");
    TEST_ASSERT(result2.error() == cache_error::expired, "Error should be expired");

    return true;
}

bool test_cache_evict_expired() {
    patient_cache_config config;
    config.default_ttl = std::chrono::seconds{1};

    patient_cache cache(config);

    for (int i = 0; i < 5; i++) {
        cache.put(std::to_string(i), create_test_patient(std::to_string(i)));
    }

    TEST_ASSERT(cache.size() == 5, "Should have 5 entries");

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::seconds{2});

    size_t evicted = cache.evict_expired();
    TEST_ASSERT(evicted == 5, "Should evict 5 entries");
    TEST_ASSERT(cache.size() == 0, "Should be empty after eviction");

    return true;
}

bool test_cache_mixed_ttl_eviction() {
    patient_cache_config config;
    config.default_ttl = std::chrono::seconds{60};

    patient_cache cache(config);

    // Short TTL
    cache.put("short1", create_test_patient("short1"), std::chrono::seconds{1});
    cache.put("short2", create_test_patient("short2"), std::chrono::seconds{1});

    // Long TTL
    cache.put("long1", create_test_patient("long1"), std::chrono::seconds{60});
    cache.put("long2", create_test_patient("long2"), std::chrono::seconds{60});

    std::this_thread::sleep_for(std::chrono::seconds{2});

    size_t evicted = cache.evict_expired();
    TEST_ASSERT(evicted == 2, "Should evict 2 expired entries");
    TEST_ASSERT(cache.size() == 2, "Should have 2 remaining entries");
    TEST_ASSERT(cache.contains("long1"), "Long TTL entry should remain");
    TEST_ASSERT(cache.contains("long2"), "Long TTL entry should remain");

    return true;
}

// =============================================================================
// LRU Eviction Tests
// =============================================================================

bool test_cache_lru_eviction() {
    patient_cache_config config;
    config.max_entries = 5;
    config.lru_eviction = true;

    patient_cache cache(config);

    // Fill cache
    for (int i = 0; i < 5; i++) {
        cache.put(std::to_string(i), create_test_patient(std::to_string(i)));
    }

    TEST_ASSERT(cache.size() == 5, "Should have 5 entries");

    // Access first entry to make it recently used
    cache.get("0");

    // Add new entry, should evict least recently used (entry "1")
    cache.put("5", create_test_patient("5"));

    TEST_ASSERT(cache.size() == 5, "Should still have 5 entries");
    TEST_ASSERT(cache.contains("0"), "Entry 0 should remain (accessed recently)");
    TEST_ASSERT(cache.contains("5"), "Entry 5 should exist (just added)");
    TEST_ASSERT(!cache.contains("1"), "Entry 1 should be evicted (LRU)");

    return true;
}

bool test_cache_lru_order() {
    patient_cache_config config;
    config.max_entries = 3;
    config.lru_eviction = true;

    patient_cache cache(config);

    cache.put("a", create_test_patient("a"));
    cache.put("b", create_test_patient("b"));
    cache.put("c", create_test_patient("c"));

    // Access in order: b, a, c - so b becomes most recent, a becomes second
    cache.get("b");
    cache.get("a");
    cache.get("c");

    // Add new entry, "b" was accessed first (least recently used now)
    cache.put("d", create_test_patient("d"));

    TEST_ASSERT(!cache.contains("b"), "Entry b should be evicted");
    TEST_ASSERT(cache.contains("a"), "Entry a should remain");
    TEST_ASSERT(cache.contains("c"), "Entry c should remain");
    TEST_ASSERT(cache.contains("d"), "Entry d should exist");

    return true;
}

// =============================================================================
// Alias Tests
// =============================================================================

bool test_cache_alias_basic() {
    patient_cache cache;

    auto patient = create_test_patient("12345");
    cache.put("12345", patient);

    bool added = cache.add_alias("SSN:123-45-6789", "12345");
    TEST_ASSERT(added, "Should add alias successfully");

    // Get by alias
    auto result = cache.get("SSN:123-45-6789");
    TEST_ASSERT(result.has_value(), "Should find patient by alias");
    TEST_ASSERT(result->patient_id == "12345", "Patient ID should match");

    // Contains by alias
    TEST_ASSERT(cache.contains("SSN:123-45-6789"), "Should contain alias");

    return true;
}

bool test_cache_alias_invalid_primary() {
    patient_cache cache;

    bool added = cache.add_alias("alias", "nonexistent");
    TEST_ASSERT(!added, "Should not add alias for non-existent primary");

    return true;
}

bool test_cache_remove_alias() {
    patient_cache cache;

    cache.put("12345", create_test_patient("12345"));
    cache.add_alias("alias1", "12345");

    TEST_ASSERT(cache.contains("alias1"), "Should contain alias");

    bool removed = cache.remove_alias("alias1");
    TEST_ASSERT(removed, "Should remove alias");
    TEST_ASSERT(!cache.contains("alias1"), "Should not contain removed alias");
    TEST_ASSERT(cache.contains("12345"), "Primary should still exist");

    return true;
}

bool test_cache_remove_primary_removes_aliases() {
    patient_cache cache;

    cache.put("12345", create_test_patient("12345"));
    cache.add_alias("alias1", "12345");
    cache.add_alias("alias2", "12345");

    TEST_ASSERT(cache.contains("alias1"), "Should contain alias1");
    TEST_ASSERT(cache.contains("alias2"), "Should contain alias2");

    cache.remove("12345");

    TEST_ASSERT(!cache.contains("alias1"), "alias1 should be removed");
    TEST_ASSERT(!cache.contains("alias2"), "alias2 should be removed");
    TEST_ASSERT(!cache.contains("12345"), "Primary should be removed");

    return true;
}

bool test_cache_multiple_aliases() {
    patient_cache cache;

    cache.put("12345", create_test_patient("12345"));
    cache.add_alias("SSN:123-45-6789", "12345");
    cache.add_alias("MRN:HOSP-12345", "12345");
    cache.add_alias("EMPI:E12345", "12345");

    // All aliases should work
    TEST_ASSERT(cache.get("SSN:123-45-6789").has_value(), "SSN alias should work");
    TEST_ASSERT(cache.get("MRN:HOSP-12345").has_value(), "MRN alias should work");
    TEST_ASSERT(cache.get("EMPI:E12345").has_value(), "EMPI alias should work");

    return true;
}

// =============================================================================
// Get or Load Tests
// =============================================================================

bool test_cache_get_or_load_cached() {
    patient_cache cache;

    cache.put("12345", create_test_patient("12345", "CACHED^PATIENT"));

    bool loader_called = false;
    auto result = cache.get_or_load("12345", [&loader_called]() {
        loader_called = true;
        return create_test_patient("12345", "LOADED^PATIENT");
    });

    TEST_ASSERT(result.has_value(), "Should return cached value");
    TEST_ASSERT(result->patient_name == "CACHED^PATIENT", "Should be cached value");
    TEST_ASSERT(!loader_called, "Loader should not be called");

    return true;
}

bool test_cache_get_or_load_not_cached() {
    patient_cache cache;

    bool loader_called = false;
    auto result = cache.get_or_load("12345", [&loader_called]() {
        loader_called = true;
        return create_test_patient("12345", "LOADED^PATIENT");
    });

    TEST_ASSERT(result.has_value(), "Should return loaded value");
    TEST_ASSERT(result->patient_name == "LOADED^PATIENT", "Should be loaded value");
    TEST_ASSERT(loader_called, "Loader should be called");

    // Now it should be cached
    auto cached = cache.get("12345");
    TEST_ASSERT(cached.has_value(), "Should be cached now");
    TEST_ASSERT(cached->patient_name == "LOADED^PATIENT", "Cached value should match");

    return true;
}

bool test_cache_get_or_load_loader_returns_nullopt() {
    patient_cache cache;

    auto result = cache.get_or_load("12345", []() {
        return std::nullopt;  // Loader can't find patient
    });

    TEST_ASSERT(!result.has_value(), "Should return error");
    TEST_ASSERT(result.error() == cache_error::not_found, "Error should be not_found");

    return true;
}

// =============================================================================
// Bulk Operations Tests
// =============================================================================

bool test_cache_get_many() {
    patient_cache cache;

    cache.put("1", create_test_patient("1", "PATIENT^ONE"));
    cache.put("2", create_test_patient("2", "PATIENT^TWO"));
    cache.put("3", create_test_patient("3", "PATIENT^THREE"));

    std::vector<std::string> keys = {"1", "2", "4", "5"};  // 4 and 5 don't exist
    auto results = cache.get_many(keys);

    TEST_ASSERT(results.size() == 2, "Should find 2 patients");
    TEST_ASSERT(results["1"].patient_name == "PATIENT^ONE", "Patient 1 should match");
    TEST_ASSERT(results["2"].patient_name == "PATIENT^TWO", "Patient 2 should match");
    TEST_ASSERT(results.find("4") == results.end(), "Patient 4 should not exist");

    return true;
}

bool test_cache_put_many() {
    patient_cache cache;

    std::unordered_map<std::string, mapping::dicom_patient> patients;
    patients["1"] = create_test_patient("1", "PATIENT^ONE");
    patients["2"] = create_test_patient("2", "PATIENT^TWO");
    patients["3"] = create_test_patient("3", "PATIENT^THREE");

    cache.put_many(patients);

    TEST_ASSERT(cache.size() == 3, "Should have 3 entries");
    TEST_ASSERT(cache.get("1")->patient_name == "PATIENT^ONE", "Patient 1 should match");
    TEST_ASSERT(cache.get("2")->patient_name == "PATIENT^TWO", "Patient 2 should match");
    TEST_ASSERT(cache.get("3")->patient_name == "PATIENT^THREE", "Patient 3 should match");

    return true;
}

// =============================================================================
// Disabled Cache Tests
// =============================================================================

bool test_cache_disabled() {
    patient_cache_config config;
    config.enabled = false;

    patient_cache cache(config);

    cache.put("12345", create_test_patient("12345"));

    auto result = cache.get("12345");
    TEST_ASSERT(!result.has_value(), "Disabled cache should not store");
    TEST_ASSERT(result.error() == cache_error::cache_disabled, "Error should be cache_disabled");

    TEST_ASSERT(!cache.contains("12345"), "Disabled cache should not contain");
    TEST_ASSERT(cache.size() == 0, "Disabled cache should be empty");

    return true;
}

bool test_cache_enable_disable() {
    patient_cache cache;

    cache.put("12345", create_test_patient("12345"));
    TEST_ASSERT(cache.contains("12345"), "Should contain when enabled");

    cache.set_enabled(false);

    auto result = cache.get("12345");
    TEST_ASSERT(!result.has_value(), "Should not get when disabled");
    TEST_ASSERT(result.error() == cache_error::cache_disabled, "Error should be cache_disabled");

    cache.set_enabled(true);

    // Note: Previously cached data is still there when re-enabled
    // (implementation choice - could alternatively clear on disable)

    return true;
}

// =============================================================================
// Metadata Tests
// =============================================================================

bool test_cache_get_metadata() {
    patient_cache cache;

    auto patient = create_test_patient("12345");
    cache.put("12345", patient, std::chrono::seconds{120});

    auto meta = cache.get_metadata("12345");
    TEST_ASSERT(meta.has_value(), "Should get metadata");
    TEST_ASSERT(meta->ttl == std::chrono::seconds{120}, "TTL should match");
    TEST_ASSERT(meta->access_count == 0, "Access count should be 0");
    TEST_ASSERT(!meta->is_expired(), "Should not be expired");

    // Access entry
    cache.get("12345");
    cache.get("12345");

    auto meta2 = cache.get_metadata("12345");
    TEST_ASSERT(meta2->access_count == 2, "Access count should be 2");

    return true;
}

bool test_cache_get_keys() {
    patient_cache cache;

    cache.put("a", create_test_patient("a"));
    cache.put("b", create_test_patient("b"));
    cache.put("c", create_test_patient("c"));

    auto keys = cache.keys();
    TEST_ASSERT(keys.size() == 3, "Should have 3 keys");

    // Verify all keys are present
    bool found_a = false, found_b = false, found_c = false;
    for (const auto& key : keys) {
        if (key == "a") found_a = true;
        if (key == "b") found_b = true;
        if (key == "c") found_c = true;
    }
    TEST_ASSERT(found_a && found_b && found_c, "All keys should be present");

    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_cache_statistics_basic() {
    patient_cache cache;

    cache.put("1", create_test_patient("1"));
    cache.put("2", create_test_patient("2"));

    cache.get("1");  // Hit
    cache.get("1");  // Hit
    cache.get("2");  // Hit
    cache.get("3");  // Miss

    auto stats = cache.get_statistics();
    TEST_ASSERT(stats.put_count == 2, "Put count should be 2");
    TEST_ASSERT(stats.get_count == 4, "Get count should be 4");
    TEST_ASSERT(stats.hit_count == 3, "Hit count should be 3");
    TEST_ASSERT(stats.miss_count == 1, "Miss count should be 1");
    TEST_ASSERT(stats.current_size == 2, "Current size should be 2");

    return true;
}

bool test_cache_statistics_hit_rate() {
    patient_cache cache;

    cache.put("1", create_test_patient("1"));

    cache.get("1");  // Hit
    cache.get("1");  // Hit
    cache.get("1");  // Hit
    cache.get("2");  // Miss

    auto stats = cache.get_statistics();
    double hit_rate = stats.hit_rate();

    TEST_ASSERT(hit_rate == 0.75, "Hit rate should be 0.75 (3/4)");

    return true;
}

bool test_cache_statistics_reset() {
    patient_cache cache;

    cache.put("1", create_test_patient("1"));
    cache.get("1");
    cache.get("2");

    auto stats1 = cache.get_statistics();
    TEST_ASSERT(stats1.get_count > 0, "Should have counts");

    cache.reset_statistics();

    auto stats2 = cache.get_statistics();
    TEST_ASSERT(stats2.get_count == 0, "Get count should be 0");
    TEST_ASSERT(stats2.hit_count == 0, "Hit count should be 0");
    TEST_ASSERT(stats2.put_count == 0, "Put count should be 0");
    TEST_ASSERT(stats2.current_size == 1, "Current size should remain");

    return true;
}

bool test_cache_statistics_eviction() {
    patient_cache_config config;
    config.max_entries = 3;
    config.lru_eviction = true;

    patient_cache cache(config);

    cache.put("1", create_test_patient("1"));
    cache.put("2", create_test_patient("2"));
    cache.put("3", create_test_patient("3"));
    cache.put("4", create_test_patient("4"));  // Triggers eviction

    auto stats = cache.get_statistics();
    TEST_ASSERT(stats.eviction_count >= 1, "Should have eviction count");

    return true;
}

bool test_cache_statistics_max_size() {
    patient_cache_config config;
    config.max_entries = 100;

    patient_cache cache(config);

    for (int i = 0; i < 10; i++) {
        cache.put(std::to_string(i), create_test_patient(std::to_string(i)));
    }

    auto stats1 = cache.get_statistics();
    TEST_ASSERT(stats1.max_size_reached == 10, "Max size should be 10");

    cache.clear();

    auto stats2 = cache.get_statistics();
    TEST_ASSERT(stats2.max_size_reached == 10, "Max size should still be 10");
    TEST_ASSERT(stats2.current_size == 0, "Current size should be 0");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Cache Error Tests ===" << std::endl;
    RUN_TEST(test_cache_error_codes);

    std::cout << "\n=== Cache Entry Metadata Tests ===" << std::endl;
    RUN_TEST(test_cache_entry_metadata_is_expired);
    RUN_TEST(test_cache_entry_metadata_time_remaining);

    std::cout << "\n=== Cache Configuration Tests ===" << std::endl;
    RUN_TEST(test_cache_config_defaults);
    RUN_TEST(test_cache_custom_config);

    std::cout << "\n=== Basic Cache Operations Tests ===" << std::endl;
    RUN_TEST(test_cache_put_and_get);
    RUN_TEST(test_cache_get_not_found);
    RUN_TEST(test_cache_peek);
    RUN_TEST(test_cache_contains);
    RUN_TEST(test_cache_remove);
    RUN_TEST(test_cache_update);
    RUN_TEST(test_cache_clear);

    std::cout << "\n=== TTL Tests ===" << std::endl;
    RUN_TEST(test_cache_custom_ttl);
    RUN_TEST(test_cache_evict_expired);
    RUN_TEST(test_cache_mixed_ttl_eviction);

    std::cout << "\n=== LRU Eviction Tests ===" << std::endl;
    RUN_TEST(test_cache_lru_eviction);
    RUN_TEST(test_cache_lru_order);

    std::cout << "\n=== Alias Tests ===" << std::endl;
    RUN_TEST(test_cache_alias_basic);
    RUN_TEST(test_cache_alias_invalid_primary);
    RUN_TEST(test_cache_remove_alias);
    RUN_TEST(test_cache_remove_primary_removes_aliases);
    RUN_TEST(test_cache_multiple_aliases);

    std::cout << "\n=== Get or Load Tests ===" << std::endl;
    RUN_TEST(test_cache_get_or_load_cached);
    RUN_TEST(test_cache_get_or_load_not_cached);
    RUN_TEST(test_cache_get_or_load_loader_returns_nullopt);

    std::cout << "\n=== Bulk Operations Tests ===" << std::endl;
    RUN_TEST(test_cache_get_many);
    RUN_TEST(test_cache_put_many);

    std::cout << "\n=== Disabled Cache Tests ===" << std::endl;
    RUN_TEST(test_cache_disabled);
    RUN_TEST(test_cache_enable_disable);

    std::cout << "\n=== Metadata Tests ===" << std::endl;
    RUN_TEST(test_cache_get_metadata);
    RUN_TEST(test_cache_get_keys);

    std::cout << "\n=== Statistics Tests ===" << std::endl;
    RUN_TEST(test_cache_statistics_basic);
    RUN_TEST(test_cache_statistics_hit_rate);
    RUN_TEST(test_cache_statistics_reset);
    RUN_TEST(test_cache_statistics_eviction);
    RUN_TEST(test_cache_statistics_max_size);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    double coverage = (passed * 100.0) / (passed + failed);
    std::cout << "Pass Rate: " << coverage << "%" << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::cache::test

int main() {
    return pacs::bridge::cache::test::run_all_tests();
}
