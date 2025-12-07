#ifndef PACS_BRIDGE_TESTING_LOAD_GENERATOR_H
#define PACS_BRIDGE_TESTING_LOAD_GENERATOR_H

/**
 * @file load_generator.h
 * @brief HL7 message generator for load testing
 *
 * Generates realistic HL7 messages for load testing including ORM, ADT, SIU,
 * ORU, and MDM message types. Supports configurable message distribution
 * and randomized field values.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/45
 */

#include "load_types.h"

#include <expected>
#include <memory>
#include <random>
#include <string>
#include <string_view>

namespace pacs::bridge::testing {

/**
 * @brief HL7 message generator for load testing
 *
 * Thread-safe message generator that creates realistic HL7 messages
 * for various message types. Uses randomization to create unique
 * message identifiers while maintaining valid HL7 structure.
 *
 * @code
 * load_generator generator;
 *
 * // Generate single message
 * auto msg = generator.generate(hl7_message_type::ORM);
 *
 * // Generate with specific distribution
 * message_distribution dist{70, 20, 10, 0, 0};
 * auto mixed_msg = generator.generate_random(dist);
 * @endcode
 */
class load_generator {
public:
    /**
     * @brief Generator configuration
     */
    struct config {
        /** Sending application name */
        std::string sending_application = "PACS_BRIDGE_TEST";

        /** Sending facility name */
        std::string sending_facility = "LOAD_TEST";

        /** Receiving application name */
        std::string receiving_application = "RIS";

        /** Receiving facility name */
        std::string receiving_facility = "HOSPITAL";

        /** Include optional fields for more realistic messages */
        bool include_optional_fields = true;

        /** Average message size target (0 = natural size) */
        size_t target_message_size = 0;

        /** Random seed (0 = random seed) */
        uint64_t seed = 0;
    };

    /**
     * @brief Default constructor with default configuration
     */
    load_generator();

    /**
     * @brief Construct with configuration
     * @param cfg Generator configuration
     */
    explicit load_generator(const config& cfg);

    /**
     * @brief Destructor
     */
    ~load_generator();

    // Non-copyable
    load_generator(const load_generator&) = delete;
    load_generator& operator=(const load_generator&) = delete;

    // Movable
    load_generator(load_generator&&) noexcept;
    load_generator& operator=(load_generator&&) noexcept;

    /**
     * @brief Generate HL7 message of specific type
     * @param type Message type to generate
     * @return Generated HL7 message string or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate(hl7_message_type type);

    /**
     * @brief Generate random message based on distribution
     * @param dist Message type distribution
     * @return Generated HL7 message string or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate_random(const message_distribution& dist);

    /**
     * @brief Generate ORM (Order) message
     * @return Generated HL7 ORM message
     */
    [[nodiscard]] std::expected<std::string, load_error> generate_orm();

    /**
     * @brief Generate ADT (Admission/Discharge/Transfer) message
     * @return Generated HL7 ADT message
     */
    [[nodiscard]] std::expected<std::string, load_error> generate_adt();

    /**
     * @brief Generate SIU (Scheduling) message
     * @return Generated HL7 SIU message
     */
    [[nodiscard]] std::expected<std::string, load_error> generate_siu();

    /**
     * @brief Generate ORU (Observation Result) message
     * @return Generated HL7 ORU message
     */
    [[nodiscard]] std::expected<std::string, load_error> generate_oru();

    /**
     * @brief Generate MDM (Medical Document) message
     * @return Generated HL7 MDM message
     */
    [[nodiscard]] std::expected<std::string, load_error> generate_mdm();

    /**
     * @brief Get count of messages generated
     * @return Total messages generated
     */
    [[nodiscard]] uint64_t messages_generated() const noexcept;

    /**
     * @brief Get count by message type
     * @param type Message type
     * @return Count of that message type generated
     */
    [[nodiscard]] uint64_t messages_generated(hl7_message_type type) const noexcept;

    /**
     * @brief Reset generator state and counters
     */
    void reset();

    /**
     * @brief Generate unique message control ID
     * @return Unique message ID string
     */
    [[nodiscard]] std::string generate_message_id();

    /**
     * @brief Generate random patient ID
     * @return Random patient ID string
     */
    [[nodiscard]] std::string generate_patient_id();

    /**
     * @brief Generate random accession number
     * @return Random accession number string
     */
    [[nodiscard]] std::string generate_accession_number();

    /**
     * @brief Generate current timestamp in HL7 format
     * @return Timestamp string (YYYYMMDDHHMMSS)
     */
    [[nodiscard]] static std::string current_timestamp();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Builder for load_generator configuration
 */
class load_generator_builder {
public:
    load_generator_builder() = default;

    /**
     * @brief Set sending application
     */
    load_generator_builder& sending_application(std::string_view app) {
        config_.sending_application = std::string(app);
        return *this;
    }

    /**
     * @brief Set sending facility
     */
    load_generator_builder& sending_facility(std::string_view facility) {
        config_.sending_facility = std::string(facility);
        return *this;
    }

    /**
     * @brief Set receiving application
     */
    load_generator_builder& receiving_application(std::string_view app) {
        config_.receiving_application = std::string(app);
        return *this;
    }

    /**
     * @brief Set receiving facility
     */
    load_generator_builder& receiving_facility(std::string_view facility) {
        config_.receiving_facility = std::string(facility);
        return *this;
    }

    /**
     * @brief Include optional fields
     */
    load_generator_builder& include_optional_fields(bool include = true) {
        config_.include_optional_fields = include;
        return *this;
    }

    /**
     * @brief Set target message size
     */
    load_generator_builder& target_message_size(size_t size) {
        config_.target_message_size = size;
        return *this;
    }

    /**
     * @brief Set random seed for reproducibility
     */
    load_generator_builder& seed(uint64_t s) {
        config_.seed = s;
        return *this;
    }

    /**
     * @brief Build the generator
     */
    [[nodiscard]] load_generator build() const {
        return load_generator(config_);
    }

private:
    load_generator::config config_;
};

}  // namespace pacs::bridge::testing

#endif  // PACS_BRIDGE_TESTING_LOAD_GENERATOR_H
