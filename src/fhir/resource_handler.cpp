/**
 * @file resource_handler.cpp
 * @brief FHIR resource handler implementation
 *
 * Implements the resource handler base class and handler registry.
 *
 * @see include/pacs/bridge/fhir/resource_handler.h
 */

#include "pacs/bridge/fhir/resource_handler.h"

#include <algorithm>

namespace pacs::bridge::fhir {

// =============================================================================
// Resource Handler Default Implementations
// =============================================================================

resource_result<std::unique_ptr<fhir_resource>> resource_handler::vread(
    const std::string& id, const std::string& /*version_id*/) {
    // Default implementation: fall back to read (ignoring version)
    // Concrete implementations can override to support versioning
    return read(id);
}

resource_result<std::unique_ptr<fhir_resource>> resource_handler::create(
    std::unique_ptr<fhir_resource> /*resource*/) {
    return not_implemented("create");
}

resource_result<std::unique_ptr<fhir_resource>> resource_handler::update(
    const std::string& /*id*/, std::unique_ptr<fhir_resource> /*resource*/) {
    return not_implemented("update");
}

resource_result<std::monostate> resource_handler::delete_resource(
    const std::string& /*id*/) {
    return not_implemented("delete");
}

resource_result<search_result> resource_handler::search(
    const std::map<std::string, std::string>& /*params*/,
    const pagination_params& /*pagination*/) {
    return not_implemented("search");
}

std::map<std::string, std::string>
resource_handler::supported_search_params() const {
    // Default: no search parameters
    return {};
}

bool resource_handler::supports_interaction(
    interaction_type type) const noexcept {
    auto supported = supported_interactions();
    return std::find(supported.begin(), supported.end(), type) !=
           supported.end();
}

std::vector<interaction_type> resource_handler::supported_interactions() const {
    // Default: only read is supported
    return {interaction_type::read};
}

operation_outcome resource_handler::not_implemented(std::string_view operation) {
    std::string message = "Operation '";
    message += operation;
    message += "' is not implemented for this resource type";
    return operation_outcome(
        outcome_issue::error(issue_type::not_supported, std::move(message)));
}

operation_outcome resource_handler::resource_not_found(
    const std::string& id) const {
    return operation_outcome::not_found(type_name(), id);
}

// =============================================================================
// Handler Registry Implementation
// =============================================================================

bool handler_registry::register_handler(
    std::shared_ptr<resource_handler> handler) {
    if (!handler) {
        return false;
    }

    std::lock_guard lock(mutex_);
    auto type = handler->handled_type();

    // Check if handler already exists for this type
    if (handlers_.find(type) != handlers_.end()) {
        return false;
    }

    handlers_[type] = std::move(handler);
    return true;
}

std::shared_ptr<resource_handler> handler_registry::get_handler(
    resource_type type) const {
    std::lock_guard lock(mutex_);
    auto it = handlers_.find(type);
    if (it == handlers_.end()) {
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<resource_handler> handler_registry::get_handler(
    std::string_view type_name) const {
    auto type = parse_resource_type(type_name);
    if (!type) {
        return nullptr;
    }
    return get_handler(*type);
}

std::vector<std::shared_ptr<resource_handler>>
handler_registry::all_handlers() const {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<resource_handler>> result;
    result.reserve(handlers_.size());
    for (const auto& [type, handler] : handlers_) {
        result.push_back(handler);
    }
    return result;
}

std::vector<resource_type> handler_registry::registered_types() const {
    std::lock_guard lock(mutex_);
    std::vector<resource_type> result;
    result.reserve(handlers_.size());
    for (const auto& [type, handler] : handlers_) {
        result.push_back(type);
    }
    return result;
}

bool handler_registry::has_handler(resource_type type) const {
    std::lock_guard lock(mutex_);
    return handlers_.find(type) != handlers_.end();
}

void handler_registry::clear() {
    std::lock_guard lock(mutex_);
    handlers_.clear();
}

}  // namespace pacs::bridge::fhir
