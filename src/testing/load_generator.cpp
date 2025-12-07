/**
 * @file load_generator.cpp
 * @brief Implementation of HL7 message generator for load testing
 */

#include "pacs/bridge/testing/load_generator.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>

namespace pacs::bridge::testing {

// =============================================================================
// Implementation Class
// =============================================================================

class load_generator::impl {
public:
    explicit impl(const config& cfg)
        : config_(cfg)
        , rng_(cfg.seed != 0 ? cfg.seed : std::random_device{}())
        , message_counter_(0) {
        // Initialize counters
        for (auto& count : type_counters_) {
            count = 0;
        }
    }

    std::expected<std::string, load_error> generate(hl7_message_type type) {
        switch (type) {
            case hl7_message_type::ORM:
                return generate_orm();
            case hl7_message_type::ADT:
                return generate_adt();
            case hl7_message_type::SIU:
                return generate_siu();
            case hl7_message_type::ORU:
                return generate_oru();
            case hl7_message_type::MDM:
                return generate_mdm();
            default:
                return std::unexpected(load_error::generation_failed);
        }
    }

    std::expected<std::string, load_error>
    generate_random(const message_distribution& dist) {
        if (!dist.is_valid()) {
            return std::unexpected(load_error::invalid_configuration);
        }

        std::uniform_int_distribution<> d(1, 100);
        int roll;
        {
            std::lock_guard lock(rng_mutex_);
            roll = d(rng_);
        }

        uint8_t cumulative = 0;
        if (roll <= (cumulative += dist.orm_percent)) {
            return generate_orm();
        }
        if (roll <= (cumulative += dist.adt_percent)) {
            return generate_adt();
        }
        if (roll <= (cumulative += dist.siu_percent)) {
            return generate_siu();
        }
        if (roll <= (cumulative += dist.oru_percent)) {
            return generate_oru();
        }
        return generate_mdm();
    }

    std::expected<std::string, load_error> generate_orm() {
        std::ostringstream oss;

        auto msg_id = generate_message_id();
        auto timestamp = current_timestamp();
        auto patient_id = generate_patient_id();
        auto accession = generate_accession_number();

        // MSH Segment
        oss << "MSH|^~\\&|"
            << config_.sending_application << "|"
            << config_.sending_facility << "|"
            << config_.receiving_application << "|"
            << config_.receiving_facility << "|"
            << timestamp << "||ORM^O01|"
            << msg_id << "|P|2.5.1\r";

        // PID Segment
        oss << "PID|1||" << patient_id << "^^^HOSPITAL^MR||"
            << generate_patient_name() << "||"
            << generate_birth_date() << "|"
            << generate_gender() << "\r";

        // PV1 Segment
        oss << "PV1|1|O|" << generate_location() << "|||"
            << generate_physician_id() << "^" << generate_physician_name()
            << "||||||||||||" << generate_visit_number() << "\r";

        // ORC Segment
        oss << "ORC|NW|" << accession << "|||SC||^^^"
            << timestamp << "^^R||" << timestamp << "|"
            << generate_operator_id() << "^" << generate_operator_name() << "\r";

        // OBR Segment
        oss << "OBR|1|" << accession << "||"
            << generate_procedure_code() << "^" << generate_procedure_name()
            << "^L|||" << timestamp << "||||||||"
            << generate_physician_id() << "^" << generate_physician_name()
            << "||||||||" << generate_modality() << "\r";

        if (config_.include_optional_fields) {
            // ZDS Segment (custom)
            oss << "ZDS|1.2.840.10008.5.1.4.1.1.2^" << accession << "\r";
        }

        increment_counter(hl7_message_type::ORM);
        return oss.str();
    }

    std::expected<std::string, load_error> generate_adt() {
        std::ostringstream oss;

        auto msg_id = generate_message_id();
        auto timestamp = current_timestamp();
        auto patient_id = generate_patient_id();

        // MSH Segment
        oss << "MSH|^~\\&|"
            << config_.sending_application << "|"
            << config_.sending_facility << "|"
            << config_.receiving_application << "|"
            << config_.receiving_facility << "|"
            << timestamp << "||ADT^A01|"
            << msg_id << "|P|2.5.1\r";

        // EVN Segment
        oss << "EVN|A01|" << timestamp << "\r";

        // PID Segment
        oss << "PID|1||" << patient_id << "^^^HOSPITAL^MR||"
            << generate_patient_name() << "||"
            << generate_birth_date() << "|"
            << generate_gender() << "|||"
            << generate_address() << "||"
            << generate_phone() << "\r";

        // PV1 Segment
        oss << "PV1|1|I|" << generate_location() << "|||"
            << generate_physician_id() << "^" << generate_physician_name()
            << "||||MED|||||" << generate_visit_number() << "|||||||||||||||||||||"
            << timestamp << "\r";

        increment_counter(hl7_message_type::ADT);
        return oss.str();
    }

    std::expected<std::string, load_error> generate_siu() {
        std::ostringstream oss;

        auto msg_id = generate_message_id();
        auto timestamp = current_timestamp();
        auto patient_id = generate_patient_id();
        auto schedule_id = generate_schedule_id();

        // MSH Segment
        oss << "MSH|^~\\&|"
            << config_.sending_application << "|"
            << config_.sending_facility << "|"
            << config_.receiving_application << "|"
            << config_.receiving_facility << "|"
            << timestamp << "||SIU^S12|"
            << msg_id << "|P|2.5.1\r";

        // SCH Segment
        oss << "SCH|" << schedule_id << "||||||"
            << generate_procedure_code() << "^" << generate_procedure_name()
            << "||30|MIN|^^^" << timestamp << "^^30^MIN\r";

        // PID Segment
        oss << "PID|1||" << patient_id << "^^^HOSPITAL^MR||"
            << generate_patient_name() << "||"
            << generate_birth_date() << "|"
            << generate_gender() << "\r";

        // RGS Segment
        oss << "RGS|1|A\r";

        // AIS Segment
        oss << "AIS|1|A|" << generate_procedure_code() << "^"
            << generate_procedure_name() << "||" << timestamp << "|30|MIN\r";

        increment_counter(hl7_message_type::SIU);
        return oss.str();
    }

    std::expected<std::string, load_error> generate_oru() {
        std::ostringstream oss;

        auto msg_id = generate_message_id();
        auto timestamp = current_timestamp();
        auto patient_id = generate_patient_id();
        auto accession = generate_accession_number();

        // MSH Segment
        oss << "MSH|^~\\&|"
            << config_.sending_application << "|"
            << config_.sending_facility << "|"
            << config_.receiving_application << "|"
            << config_.receiving_facility << "|"
            << timestamp << "||ORU^R01|"
            << msg_id << "|P|2.5.1\r";

        // PID Segment
        oss << "PID|1||" << patient_id << "^^^HOSPITAL^MR||"
            << generate_patient_name() << "||"
            << generate_birth_date() << "|"
            << generate_gender() << "\r";

        // OBR Segment
        oss << "OBR|1|" << accession << "|" << accession << "|"
            << generate_procedure_code() << "^" << generate_procedure_name()
            << "|||" << timestamp << "||||||||||"
            << generate_physician_id() << "^" << generate_physician_name()
            << "|||||||F\r";

        // OBX Segment
        oss << "OBX|1|TX|" << generate_procedure_code() << "^FINDINGS||"
            << generate_report_text() << "||||||F\r";

        increment_counter(hl7_message_type::ORU);
        return oss.str();
    }

    std::expected<std::string, load_error> generate_mdm() {
        std::ostringstream oss;

        auto msg_id = generate_message_id();
        auto timestamp = current_timestamp();
        auto patient_id = generate_patient_id();
        auto document_id = generate_document_id();

        // MSH Segment
        oss << "MSH|^~\\&|"
            << config_.sending_application << "|"
            << config_.sending_facility << "|"
            << config_.receiving_application << "|"
            << config_.receiving_facility << "|"
            << timestamp << "||MDM^T02|"
            << msg_id << "|P|2.5.1\r";

        // EVN Segment
        oss << "EVN|T02|" << timestamp << "\r";

        // PID Segment
        oss << "PID|1||" << patient_id << "^^^HOSPITAL^MR||"
            << generate_patient_name() << "||"
            << generate_birth_date() << "|"
            << generate_gender() << "\r";

        // TXA Segment
        oss << "TXA|1|RAD|TX|" << timestamp << "|"
            << generate_physician_id() << "^" << generate_physician_name()
            << "||" << timestamp << "|||" << document_id
            << "||AU\r";

        // OBX Segment
        oss << "OBX|1|TX|RAD^REPORT||"
            << generate_report_text() << "||||||F\r";

        increment_counter(hl7_message_type::MDM);
        return oss.str();
    }

    std::string generate_message_id() {
        uint64_t counter = message_counter_.fetch_add(1);
        std::ostringstream oss;
        oss << "LOADTEST" << std::setfill('0') << std::setw(12) << counter;
        return oss.str();
    }

    std::string generate_patient_id() {
        std::uniform_int_distribution<> dist(100000, 999999);
        std::lock_guard lock(rng_mutex_);
        return "P" + std::to_string(dist(rng_));
    }

    std::string generate_accession_number() {
        std::uniform_int_distribution<> dist(10000000, 99999999);
        std::lock_guard lock(rng_mutex_);
        return "ACC" + std::to_string(dist(rng_));
    }

    static std::string current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_time;
#ifdef _WIN32
        localtime_s(&tm_time, &time);
#else
        localtime_r(&time, &tm_time);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_time, "%Y%m%d%H%M%S");
        return oss.str();
    }

    uint64_t messages_generated() const noexcept {
        return message_counter_.load();
    }

    uint64_t messages_generated(hl7_message_type type) const noexcept {
        auto idx = static_cast<size_t>(type);
        if (idx < type_counters_.size()) {
            return type_counters_[idx].load();
        }
        return 0;
    }

    void reset() {
        message_counter_.store(0);
        for (auto& count : type_counters_) {
            count.store(0);
        }
    }

private:
    void increment_counter(hl7_message_type type) {
        auto idx = static_cast<size_t>(type);
        if (idx < type_counters_.size()) {
            type_counters_[idx].fetch_add(1);
        }
    }

    std::string generate_patient_name() {
        static const std::array<const char*, 10> first_names = {
            "JOHN", "JANE", "MICHAEL", "SARAH", "ROBERT",
            "EMILY", "WILLIAM", "JESSICA", "DAVID", "ASHLEY"
        };
        static const std::array<const char*, 10> last_names = {
            "SMITH", "JOHNSON", "WILLIAMS", "BROWN", "JONES",
            "GARCIA", "MILLER", "DAVIS", "RODRIGUEZ", "MARTINEZ"
        };

        std::uniform_int_distribution<size_t> dist(0, 9);
        std::lock_guard lock(rng_mutex_);
        return std::string(last_names[dist(rng_)]) + "^" +
               first_names[dist(rng_)] + "^";
    }

    std::string generate_birth_date() {
        std::uniform_int_distribution<> year_dist(1940, 2010);
        std::uniform_int_distribution<> month_dist(1, 12);
        std::uniform_int_distribution<> day_dist(1, 28);

        std::lock_guard lock(rng_mutex_);
        std::ostringstream oss;
        oss << year_dist(rng_)
            << std::setfill('0') << std::setw(2) << month_dist(rng_)
            << std::setfill('0') << std::setw(2) << day_dist(rng_);
        return oss.str();
    }

    std::string generate_gender() {
        std::uniform_int_distribution<> dist(0, 1);
        std::lock_guard lock(rng_mutex_);
        return dist(rng_) == 0 ? "M" : "F";
    }

    std::string generate_location() {
        static const std::array<const char*, 5> locations = {
            "RAD^CT^1", "RAD^MRI^2", "RAD^XRAY^3", "RAD^US^4", "RAD^NUC^5"
        };
        std::uniform_int_distribution<size_t> dist(0, locations.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return locations[dist(rng_)];
    }

    std::string generate_physician_id() {
        std::uniform_int_distribution<> dist(1000, 9999);
        std::lock_guard lock(rng_mutex_);
        return "DR" + std::to_string(dist(rng_));
    }

    std::string generate_physician_name() {
        static const std::array<const char*, 5> names = {
            "HOUSE^GREGORY", "WILSON^JAMES", "CUDDY^LISA",
            "CHASE^ROBERT", "FOREMAN^ERIC"
        };
        std::uniform_int_distribution<size_t> dist(0, names.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return names[dist(rng_)];
    }

    std::string generate_visit_number() {
        std::uniform_int_distribution<> dist(100000, 999999);
        std::lock_guard lock(rng_mutex_);
        return "V" + std::to_string(dist(rng_));
    }

    std::string generate_operator_id() {
        std::uniform_int_distribution<> dist(100, 999);
        std::lock_guard lock(rng_mutex_);
        return "OP" + std::to_string(dist(rng_));
    }

    std::string generate_operator_name() {
        static const std::array<const char*, 5> names = {
            "TECH^ALICE", "TECH^BOB", "TECH^CAROL", "TECH^DAN", "TECH^EVE"
        };
        std::uniform_int_distribution<size_t> dist(0, names.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return names[dist(rng_)];
    }

    std::string generate_procedure_code() {
        static const std::array<const char*, 10> codes = {
            "70553", "71250", "72148", "73720", "74177",
            "76700", "77065", "78452", "80076", "82150"
        };
        std::uniform_int_distribution<size_t> dist(0, codes.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return codes[dist(rng_)];
    }

    std::string generate_procedure_name() {
        static const std::array<const char*, 10> names = {
            "CT HEAD W/O CONTRAST",
            "CT CHEST W/ CONTRAST",
            "MRI LUMBAR SPINE",
            "MRI KNEE W/O CONTRAST",
            "CT ABDOMEN/PELVIS",
            "US ABDOMEN COMPLETE",
            "MAMMOGRAM BILATERAL",
            "CARDIAC SPECT",
            "METABOLIC PANEL",
            "GLUCOSE TOLERANCE"
        };
        std::uniform_int_distribution<size_t> dist(0, names.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return names[dist(rng_)];
    }

    std::string generate_modality() {
        static const std::array<const char*, 5> modalities = {
            "CT", "MR", "CR", "US", "NM"
        };
        std::uniform_int_distribution<size_t> dist(0, modalities.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return modalities[dist(rng_)];
    }

    std::string generate_schedule_id() {
        std::uniform_int_distribution<> dist(100000, 999999);
        std::lock_guard lock(rng_mutex_);
        return "SCH" + std::to_string(dist(rng_));
    }

    std::string generate_document_id() {
        std::uniform_int_distribution<> dist(10000000, 99999999);
        std::lock_guard lock(rng_mutex_);
        return "DOC" + std::to_string(dist(rng_));
    }

    std::string generate_address() {
        std::uniform_int_distribution<> num_dist(100, 9999);
        static const std::array<const char*, 5> streets = {
            "MAIN ST", "OAK AVE", "PARK BLVD", "MAPLE DR", "FIRST ST"
        };
        static const std::array<const char*, 5> cities = {
            "SPRINGFIELD", "RIVERSIDE", "FAIRVIEW", "CLINTON", "MADISON"
        };

        std::lock_guard lock(rng_mutex_);
        std::uniform_int_distribution<size_t> street_dist(0, streets.size() - 1);
        std::uniform_int_distribution<size_t> city_dist(0, cities.size() - 1);

        return std::to_string(num_dist(rng_)) + " " + streets[street_dist(rng_)] +
               "^^" + cities[city_dist(rng_)] + "^ST^12345";
    }

    std::string generate_phone() {
        std::uniform_int_distribution<> area_dist(200, 999);
        std::uniform_int_distribution<> num_dist(1000000, 9999999);
        std::lock_guard lock(rng_mutex_);
        std::ostringstream oss;
        oss << "(" << area_dist(rng_) << ")" << num_dist(rng_);
        return oss.str();
    }

    std::string generate_report_text() {
        static const std::array<const char*, 5> findings = {
            "No acute findings. Recommend clinical correlation.",
            "Normal examination. No significant abnormality detected.",
            "Findings consistent with clinical history. Recommend follow-up.",
            "Unremarkable study. No evidence of acute pathology.",
            "Within normal limits. No actionable findings."
        };
        std::uniform_int_distribution<size_t> dist(0, findings.size() - 1);
        std::lock_guard lock(rng_mutex_);
        return findings[dist(rng_)];
    }

    config config_;
    std::mt19937_64 rng_;
    mutable std::mutex rng_mutex_;
    std::atomic<uint64_t> message_counter_;
    std::array<std::atomic<uint64_t>, 5> type_counters_;
};

// =============================================================================
// Public Interface
// =============================================================================

load_generator::load_generator()
    : pimpl_(std::make_unique<impl>(config{})) {}

load_generator::load_generator(const config& cfg)
    : pimpl_(std::make_unique<impl>(cfg)) {}

load_generator::~load_generator() = default;

load_generator::load_generator(load_generator&&) noexcept = default;
load_generator& load_generator::operator=(load_generator&&) noexcept = default;

std::expected<std::string, load_error>
load_generator::generate(hl7_message_type type) {
    return pimpl_->generate(type);
}

std::expected<std::string, load_error>
load_generator::generate_random(const message_distribution& dist) {
    return pimpl_->generate_random(dist);
}

std::expected<std::string, load_error> load_generator::generate_orm() {
    return pimpl_->generate_orm();
}

std::expected<std::string, load_error> load_generator::generate_adt() {
    return pimpl_->generate_adt();
}

std::expected<std::string, load_error> load_generator::generate_siu() {
    return pimpl_->generate_siu();
}

std::expected<std::string, load_error> load_generator::generate_oru() {
    return pimpl_->generate_oru();
}

std::expected<std::string, load_error> load_generator::generate_mdm() {
    return pimpl_->generate_mdm();
}

uint64_t load_generator::messages_generated() const noexcept {
    return pimpl_->messages_generated();
}

uint64_t load_generator::messages_generated(hl7_message_type type) const noexcept {
    return pimpl_->messages_generated(type);
}

void load_generator::reset() {
    pimpl_->reset();
}

std::string load_generator::generate_message_id() {
    return pimpl_->generate_message_id();
}

std::string load_generator::generate_patient_id() {
    return pimpl_->generate_patient_id();
}

std::string load_generator::generate_accession_number() {
    return pimpl_->generate_accession_number();
}

std::string load_generator::current_timestamp() {
    return impl::current_timestamp();
}

}  // namespace pacs::bridge::testing
