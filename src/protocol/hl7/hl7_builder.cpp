/**
 * @file hl7_builder.cpp
 * @brief HL7 message builder implementation
 */

#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// hl7_builder::impl
// =============================================================================

class hl7_builder::impl {
public:
    builder_options options_;
    hl7_message message_;

    explicit impl(const builder_options& options) : options_(options) {
        initialize_msh();
    }

    void initialize_msh() {
        message_.set_encoding(options_.encoding);

        auto& msh = message_.add_segment("MSH");

        // MSH-1: Field separator
        msh.set_field(1, std::string(1, options_.encoding.field_separator));

        // MSH-2: Encoding characters
        msh.set_field(2, options_.encoding.to_msh2());

        // MSH-3: Sending application
        if (!options_.sending_application.empty()) {
            msh.set_field(3, options_.sending_application);
        }

        // MSH-4: Sending facility
        if (!options_.sending_facility.empty()) {
            msh.set_field(4, options_.sending_facility);
        }

        // MSH-5: Receiving application
        if (!options_.receiving_application.empty()) {
            msh.set_field(5, options_.receiving_application);
        }

        // MSH-6: Receiving facility
        if (!options_.receiving_facility.empty()) {
            msh.set_field(6, options_.receiving_facility);
        }

        // MSH-7: Timestamp (set if auto_timestamp)
        if (options_.auto_timestamp) {
            msh.set_field(7, hl7_timestamp::now().to_string());
        }

        // MSH-10: Message control ID (set if auto_generate)
        if (options_.auto_generate_control_id) {
            msh.set_field(10, message_id_generator::generate());
        }

        // MSH-11: Processing ID
        if (!options_.processing_id.empty()) {
            msh.set_field(11, options_.processing_id);
        }

        // MSH-12: Version
        if (!options_.version.empty()) {
            msh.set_field(12, options_.version);
        }

        // MSH-17: Country code
        if (!options_.country_code.empty()) {
            msh.set_field(17, options_.country_code);
        }

        // MSH-18: Character set
        if (!options_.character_set.empty()) {
            msh.set_field(18, options_.character_set);
        }
    }
};

// =============================================================================
// hl7_builder Implementation
// =============================================================================

hl7_builder hl7_builder::create() {
    return hl7_builder(builder_options{});
}

hl7_builder hl7_builder::create(const builder_options& options) {
    return hl7_builder(options);
}

hl7_message hl7_builder::create_ack(const hl7_message& original, ack_code code,
                                     std::string_view text) {
    return original.create_ack(code, text);
}

hl7_message hl7_builder::create_ack(const hl7_message_header& original_header,
                                     ack_code code, std::string_view text) {
    hl7_message ack;
    ack.set_encoding(original_header.encoding);

    auto& msh = ack.add_segment("MSH");

    // Swap sending/receiving
    msh.set_field(1, std::string(1, original_header.encoding.field_separator));
    msh.set_field(2, original_header.encoding.to_msh2());
    msh.set_field(3, original_header.receiving_application);
    msh.set_field(4, original_header.receiving_facility);
    msh.set_field(5, original_header.sending_application);
    msh.set_field(6, original_header.sending_facility);
    msh.set_field(7, hl7_timestamp::now().to_string());
    msh.set_value("9.1", "ACK");
    msh.set_value("9.2", original_header.trigger_event);
    msh.set_field(10, original_header.message_control_id + "_ACK");
    msh.set_field(11, original_header.processing_id);
    msh.set_field(12, original_header.version_id);

    // Add MSA segment
    auto& msa = ack.add_segment("MSA");
    msa.set_field(1, to_string(code));
    msa.set_field(2, original_header.message_control_id);
    if (!text.empty()) {
        msa.set_field(3, std::string(text));
    }

    return ack;
}

hl7_builder::hl7_builder() : pimpl_(std::make_unique<impl>(builder_options{})) {}

hl7_builder::hl7_builder(const builder_options& options)
    : pimpl_(std::make_unique<impl>(options)) {}

hl7_builder::~hl7_builder() = default;

hl7_builder::hl7_builder(hl7_builder&&) noexcept = default;
hl7_builder& hl7_builder::operator=(hl7_builder&&) noexcept = default;

// MSH fields
hl7_builder& hl7_builder::sending_app(std::string_view app) {
    pimpl_->message_.set_value("MSH.3", std::string(app));
    return *this;
}

hl7_builder& hl7_builder::sending_facility(std::string_view facility) {
    pimpl_->message_.set_value("MSH.4", std::string(facility));
    return *this;
}

hl7_builder& hl7_builder::receiving_app(std::string_view app) {
    pimpl_->message_.set_value("MSH.5", std::string(app));
    return *this;
}

hl7_builder& hl7_builder::receiving_facility(std::string_view facility) {
    pimpl_->message_.set_value("MSH.6", std::string(facility));
    return *this;
}

hl7_builder& hl7_builder::timestamp(const hl7_timestamp& ts) {
    pimpl_->message_.set_value("MSH.7", ts.to_string());
    return *this;
}

hl7_builder& hl7_builder::message_type(std::string_view type,
                                        std::string_view trigger) {
    pimpl_->message_.set_value("MSH.9.1", std::string(type));
    pimpl_->message_.set_value("MSH.9.2", std::string(trigger));
    return *this;
}

hl7_builder& hl7_builder::control_id(std::string_view id) {
    pimpl_->message_.set_value("MSH.10", std::string(id));
    return *this;
}

hl7_builder& hl7_builder::processing_id(std::string_view id) {
    pimpl_->message_.set_value("MSH.11", std::string(id));
    return *this;
}

hl7_builder& hl7_builder::version(std::string_view ver) {
    pimpl_->message_.set_value("MSH.12", std::string(ver));
    return *this;
}

hl7_builder& hl7_builder::security(std::string_view sec) {
    pimpl_->message_.set_value("MSH.8", std::string(sec));
    return *this;
}

// PID fields
hl7_builder& hl7_builder::patient_id(std::string_view id,
                                      std::string_view assigning_authority,
                                      std::string_view id_type) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }

    pimpl_->message_.set_value("PID.3.1", std::string(id));
    if (!assigning_authority.empty()) {
        pimpl_->message_.set_value("PID.3.4", std::string(assigning_authority));
    }
    if (!id_type.empty()) {
        pimpl_->message_.set_value("PID.3.5", std::string(id_type));
    }
    return *this;
}

hl7_builder& hl7_builder::patient_name(std::string_view family,
                                        std::string_view given,
                                        std::string_view middle,
                                        std::string_view suffix,
                                        std::string_view prefix) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }

    pimpl_->message_.set_value("PID.5.1", std::string(family));
    if (!given.empty()) {
        pimpl_->message_.set_value("PID.5.2", std::string(given));
    }
    if (!middle.empty()) {
        pimpl_->message_.set_value("PID.5.3", std::string(middle));
    }
    if (!suffix.empty()) {
        pimpl_->message_.set_value("PID.5.4", std::string(suffix));
    }
    if (!prefix.empty()) {
        pimpl_->message_.set_value("PID.5.5", std::string(prefix));
    }
    return *this;
}

hl7_builder& hl7_builder::patient_name(const hl7_person_name& name) {
    return patient_name(name.family_name, name.given_name, name.middle_name,
                        name.suffix, name.prefix);
}

hl7_builder& hl7_builder::patient_birth_date(int year, int month, int day) {
    hl7_timestamp ts;
    ts.year = year;
    ts.month = month;
    ts.day = day;
    return patient_birth_date(ts);
}

hl7_builder& hl7_builder::patient_birth_date(const hl7_timestamp& ts) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }
    pimpl_->message_.set_value("PID.7", ts.to_string(8));  // YYYYMMDD
    return *this;
}

hl7_builder& hl7_builder::patient_sex(std::string_view sex) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }
    pimpl_->message_.set_value("PID.8", std::string(sex));
    return *this;
}

hl7_builder& hl7_builder::patient_address(const hl7_address& addr) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }

    pimpl_->message_.set_value("PID.11.1", addr.street1);
    if (!addr.street2.empty()) {
        pimpl_->message_.set_value("PID.11.2", addr.street2);
    }
    pimpl_->message_.set_value("PID.11.3", addr.city);
    pimpl_->message_.set_value("PID.11.4", addr.state);
    pimpl_->message_.set_value("PID.11.5", addr.postal_code);
    if (!addr.country.empty()) {
        pimpl_->message_.set_value("PID.11.6", addr.country);
    }
    if (!addr.address_type.empty()) {
        pimpl_->message_.set_value("PID.11.7", addr.address_type);
    }
    return *this;
}

hl7_builder& hl7_builder::patient_phone(std::string_view phone) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }
    pimpl_->message_.set_value("PID.13", std::string(phone));
    return *this;
}

hl7_builder& hl7_builder::patient_ssn(std::string_view ssn) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }
    pimpl_->message_.set_value("PID.19", std::string(ssn));
    return *this;
}

hl7_builder& hl7_builder::patient_account(std::string_view account) {
    if (!pimpl_->message_.has_segment("PID")) {
        pimpl_->message_.add_segment("PID");
    }
    pimpl_->message_.set_value("PID.18", std::string(account));
    return *this;
}

// PV1 fields
hl7_builder& hl7_builder::patient_class(std::string_view pclass) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.2", std::string(pclass));
    return *this;
}

hl7_builder& hl7_builder::patient_location(std::string_view point_of_care,
                                            std::string_view room,
                                            std::string_view bed) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.3.1", std::string(point_of_care));
    if (!room.empty()) {
        pimpl_->message_.set_value("PV1.3.2", std::string(room));
    }
    if (!bed.empty()) {
        pimpl_->message_.set_value("PV1.3.3", std::string(bed));
    }
    return *this;
}

hl7_builder& hl7_builder::admission_type(std::string_view type) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.4", std::string(type));
    return *this;
}

hl7_builder& hl7_builder::attending_doctor(std::string_view id,
                                            std::string_view family,
                                            std::string_view given) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.7.1", std::string(id));
    pimpl_->message_.set_value("PV1.7.2", std::string(family));
    pimpl_->message_.set_value("PV1.7.3", std::string(given));
    return *this;
}

hl7_builder& hl7_builder::referring_doctor(std::string_view id,
                                            std::string_view family,
                                            std::string_view given) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.8.1", std::string(id));
    pimpl_->message_.set_value("PV1.8.2", std::string(family));
    pimpl_->message_.set_value("PV1.8.3", std::string(given));
    return *this;
}

hl7_builder& hl7_builder::visit_number(std::string_view number) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.19", std::string(number));
    return *this;
}

hl7_builder& hl7_builder::admit_datetime(const hl7_timestamp& ts) {
    if (!pimpl_->message_.has_segment("PV1")) {
        pimpl_->message_.add_segment("PV1");
    }
    pimpl_->message_.set_value("PV1.44", ts.to_string());
    return *this;
}

// ORC fields
hl7_builder& hl7_builder::order_control(std::string_view control) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.1", std::string(control));
    return *this;
}

hl7_builder& hl7_builder::placer_order_number(std::string_view number,
                                               std::string_view namespace_id) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.2.1", std::string(number));
    if (!namespace_id.empty()) {
        pimpl_->message_.set_value("ORC.2.2", std::string(namespace_id));
    }
    return *this;
}

hl7_builder& hl7_builder::filler_order_number(std::string_view number,
                                               std::string_view namespace_id) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.3.1", std::string(number));
    if (!namespace_id.empty()) {
        pimpl_->message_.set_value("ORC.3.2", std::string(namespace_id));
    }
    return *this;
}

hl7_builder& hl7_builder::order_status(std::string_view status) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.5", std::string(status));
    return *this;
}

hl7_builder& hl7_builder::ordering_provider(std::string_view id,
                                             std::string_view family,
                                             std::string_view given) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.12.1", std::string(id));
    pimpl_->message_.set_value("ORC.12.2", std::string(family));
    pimpl_->message_.set_value("ORC.12.3", std::string(given));
    return *this;
}

hl7_builder& hl7_builder::entered_by(std::string_view id, std::string_view family,
                                      std::string_view given) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.10.1", std::string(id));
    pimpl_->message_.set_value("ORC.10.2", std::string(family));
    pimpl_->message_.set_value("ORC.10.3", std::string(given));
    return *this;
}

hl7_builder& hl7_builder::order_effective_datetime(const hl7_timestamp& ts) {
    if (!pimpl_->message_.has_segment("ORC")) {
        pimpl_->message_.add_segment("ORC");
    }
    pimpl_->message_.set_value("ORC.15", ts.to_string());
    return *this;
}

// OBR fields
hl7_builder& hl7_builder::procedure_code(std::string_view code,
                                          std::string_view description,
                                          std::string_view coding_system) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.4.1", std::string(code));
    if (!description.empty()) {
        pimpl_->message_.set_value("OBR.4.2", std::string(description));
    }
    if (!coding_system.empty()) {
        pimpl_->message_.set_value("OBR.4.3", std::string(coding_system));
    }
    return *this;
}

hl7_builder& hl7_builder::priority(std::string_view priority) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.5", std::string(priority));
    return *this;
}

hl7_builder& hl7_builder::requested_datetime(const hl7_timestamp& ts) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.6", ts.to_string());
    return *this;
}

hl7_builder& hl7_builder::observation_datetime(const hl7_timestamp& ts) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.7", ts.to_string());
    return *this;
}

hl7_builder& hl7_builder::clinical_info(std::string_view info) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.13", std::string(info));
    return *this;
}

hl7_builder& hl7_builder::specimen_source(std::string_view source) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.15", std::string(source));
    return *this;
}

hl7_builder& hl7_builder::result_status(std::string_view status) {
    if (!pimpl_->message_.has_segment("OBR")) {
        pimpl_->message_.add_segment("OBR");
    }
    pimpl_->message_.set_value("OBR.25", std::string(status));
    return *this;
}

// OBX fields
hl7_builder& hl7_builder::add_observation(std::string_view value_type,
                                           std::string_view observation_id,
                                           std::string_view value,
                                           std::string_view units) {
    size_t obx_count = pimpl_->message_.segment_count("OBX");
    auto& obx = pimpl_->message_.add_segment("OBX");

    // OBX-1: Set ID
    obx.set_field(1, std::to_string(obx_count + 1));

    // OBX-2: Value type
    obx.set_field(2, std::string(value_type));

    // OBX-3: Observation identifier
    obx.set_value("3.1", std::string(observation_id));

    // OBX-5: Observation value
    obx.set_field(5, std::string(value));

    // OBX-6: Units
    if (!units.empty()) {
        obx.set_field(6, std::string(units));
    }

    return *this;
}

// Generic field access
hl7_builder& hl7_builder::set_field(std::string_view path, std::string_view value) {
    pimpl_->message_.set_value(path, std::string(value));
    return *this;
}

hl7_segment& hl7_builder::add_segment(std::string_view segment_id) {
    return pimpl_->message_.add_segment(segment_id);
}

hl7_segment* hl7_builder::get_segment(std::string_view segment_id,
                                       size_t occurrence) {
    return pimpl_->message_.segment(segment_id, occurrence);
}

// Build
Result<hl7_message> hl7_builder::build() {
    if (pimpl_->options_.validate_before_build) {
        auto validation = pimpl_->message_.validate();
        if (!validation.valid) {
            return to_error_info(hl7_error::validation_failed);
        }
    }

    return pimpl_->message_.clone();
}

Result<std::string> hl7_builder::build_string() {
    auto result = build();
    if (!result.is_ok()) {
        return result.error();
    }
    return result.value().serialize();
}

const hl7_message& hl7_builder::message() const {
    return pimpl_->message_;
}

void hl7_builder::reset() {
    pimpl_->message_.clear();
    pimpl_->initialize_msh();
}

// =============================================================================
// Specialized Builders
// =============================================================================

hl7_builder adt_builder::admit() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A01");
    return builder;
}

hl7_builder adt_builder::transfer() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A02");
    return builder;
}

hl7_builder adt_builder::discharge() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A03");
    return builder;
}

hl7_builder adt_builder::register_patient() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A04");
    return builder;
}

hl7_builder adt_builder::update() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A08");
    return builder;
}

hl7_builder adt_builder::merge() {
    auto builder = hl7_builder::create();
    builder.message_type("ADT", "A40");
    return builder;
}

hl7_builder orm_builder::new_order() {
    auto builder = hl7_builder::create();
    builder.message_type("ORM", "O01").order_control("NW");
    return builder;
}

hl7_builder orm_builder::cancel_order() {
    auto builder = hl7_builder::create();
    builder.message_type("ORM", "O01").order_control("CA");
    return builder;
}

hl7_builder orm_builder::modify_order() {
    auto builder = hl7_builder::create();
    builder.message_type("ORM", "O01").order_control("XO");
    return builder;
}

hl7_builder orm_builder::status_request() {
    auto builder = hl7_builder::create();
    builder.message_type("ORM", "O01").order_control("SC");
    return builder;
}

hl7_builder oru_builder::result() {
    auto builder = hl7_builder::create();
    builder.message_type("ORU", "R01");
    return builder;
}

// =============================================================================
// message_id_generator Implementation
// =============================================================================

namespace {
std::atomic<uint32_t> g_sequence_counter{0};
}  // namespace

std::string message_id_generator::generate() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d%H%M%S") << "_"
        << std::setw(6) << std::setfill('0')
        << (g_sequence_counter.fetch_add(1) % 1000000);

    return oss.str();
}

std::string message_id_generator::generate_uuid() {
    // Simple UUID-like generation (not cryptographically secure)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << dis(gen) << "-"
        << std::setw(4) << (dis(gen) & 0xFFFF) << "-"
        << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-"  // Version 4
        << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-"  // Variant
        << std::setw(8) << dis(gen) << std::setw(4) << (dis(gen) & 0xFFFF);

    return oss.str();
}

std::string message_id_generator::generate_with_prefix(std::string_view prefix) {
    return std::string(prefix) + "_" + generate();
}

}  // namespace pacs::bridge::hl7
