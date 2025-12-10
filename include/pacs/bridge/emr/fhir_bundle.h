#ifndef PACS_BRIDGE_EMR_FHIR_BUNDLE_H
#define PACS_BRIDGE_EMR_FHIR_BUNDLE_H

/**
 * @file fhir_bundle.h
 * @brief FHIR Bundle resource handling
 *
 * Provides structures for representing and manipulating FHIR Bundle resources,
 * including search result bundles and pagination support.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 * @see https://www.hl7.org/fhir/bundle.html
 */

#include "emr_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::emr {

/**
 * @brief FHIR Bundle types
 */
enum class bundle_type {
    /** Document bundle */
    document,
    /** Message bundle */
    message,
    /** Transaction bundle */
    transaction,
    /** Transaction response bundle */
    transaction_response,
    /** Batch bundle */
    batch,
    /** Batch response bundle */
    batch_response,
    /** History bundle */
    history,
    /** Search results bundle */
    searchset,
    /** Collection bundle */
    collection
};

/**
 * @brief Convert bundle_type to string
 */
[[nodiscard]] constexpr std::string_view to_string(bundle_type type) noexcept {
    switch (type) {
        case bundle_type::document:
            return "document";
        case bundle_type::message:
            return "message";
        case bundle_type::transaction:
            return "transaction";
        case bundle_type::transaction_response:
            return "transaction-response";
        case bundle_type::batch:
            return "batch";
        case bundle_type::batch_response:
            return "batch-response";
        case bundle_type::history:
            return "history";
        case bundle_type::searchset:
            return "searchset";
        case bundle_type::collection:
            return "collection";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse bundle_type from string
 */
[[nodiscard]] std::optional<bundle_type> parse_bundle_type(
    std::string_view type_str) noexcept;

/**
 * @brief Bundle link relation types
 */
enum class link_relation {
    /** Self reference */
    self,
    /** First page */
    first,
    /** Last page */
    last,
    /** Next page */
    next,
    /** Previous page */
    previous
};

/**
 * @brief Convert link_relation to string
 */
[[nodiscard]] constexpr std::string_view to_string(
    link_relation relation) noexcept {
    switch (relation) {
        case link_relation::self:
            return "self";
        case link_relation::first:
            return "first";
        case link_relation::last:
            return "last";
        case link_relation::next:
            return "next";
        case link_relation::previous:
            return "previous";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse link_relation from string
 */
[[nodiscard]] std::optional<link_relation> parse_link_relation(
    std::string_view relation_str) noexcept;

/**
 * @brief Bundle link for pagination
 */
struct bundle_link {
    /** Link relation */
    link_relation relation{link_relation::self};

    /** Link URL */
    std::string url;
};

/**
 * @brief Search entry mode
 */
enum class search_mode {
    /** Resource matched the search criteria */
    match,
    /** Resource included via _include */
    include,
    /** Additional match result */
    outcome
};

/**
 * @brief Convert search_mode to string
 */
[[nodiscard]] constexpr std::string_view to_string(search_mode mode) noexcept {
    switch (mode) {
        case search_mode::match:
            return "match";
        case search_mode::include:
            return "include";
        case search_mode::outcome:
            return "outcome";
        default:
            return "unknown";
    }
}

/**
 * @brief Bundle entry search information
 */
struct entry_search {
    /** Search mode */
    search_mode mode{search_mode::match};

    /** Search score (0.0 to 1.0) */
    std::optional<double> score;
};

/**
 * @brief Bundle entry request information (for transactions)
 */
struct entry_request {
    /** HTTP method */
    http_method method{http_method::get};

    /** Request URL */
    std::string url;

    /** If-Match header value */
    std::optional<std::string> if_match;

    /** If-None-Match header value */
    std::optional<std::string> if_none_match;

    /** If-None-Exist header value (for conditional create) */
    std::optional<std::string> if_none_exist;
};

/**
 * @brief Bundle entry response information (for transaction responses)
 */
struct entry_response {
    /** HTTP status code */
    std::string status;

    /** Resource location */
    std::optional<std::string> location;

    /** ETag */
    std::optional<std::string> etag;

    /** Last modified timestamp */
    std::optional<std::string> last_modified;
};

/**
 * @brief Single entry in a FHIR Bundle
 */
struct bundle_entry {
    /** Full URL of the resource */
    std::optional<std::string> full_url;

    /** Resource content as JSON string */
    std::string resource;

    /** Resource type (parsed from resource) */
    std::string resource_type;

    /** Resource ID (parsed from resource) */
    std::optional<std::string> resource_id;

    /** Search information (for searchset bundles) */
    std::optional<entry_search> search;

    /** Request information (for transactions) */
    std::optional<entry_request> request;

    /** Response information (for transaction responses) */
    std::optional<entry_response> response;
};

/**
 * @brief FHIR Bundle resource
 *
 * Represents a FHIR Bundle containing multiple resources.
 * Commonly used for search results, transactions, and batches.
 *
 * @example Search Result Bundle
 * ```cpp
 * auto bundle = fhir_bundle::parse(json_response);
 * if (bundle.has_value()) {
 *     std::cout << "Total: " << bundle->total.value_or(0) << "\n";
 *     for (const auto& entry : bundle->entries) {
 *         std::cout << "Resource: " << entry.resource_type << "\n";
 *     }
 *
 *     // Check for next page
 *     if (auto next_url = bundle->get_link(link_relation::next)) {
 *         // Fetch next page
 *     }
 * }
 * ```
 */
struct fhir_bundle {
    /** Resource type (always "Bundle") */
    static constexpr std::string_view resource_type_name = "Bundle";

    /** Bundle ID */
    std::optional<std::string> id;

    /** Bundle type */
    bundle_type type{bundle_type::searchset};

    /** Total number of matching resources (for searchset) */
    std::optional<size_t> total;

    /** Bundle timestamp */
    std::optional<std::string> timestamp;

    /** Pagination links */
    std::vector<bundle_link> links;

    /** Bundle entries */
    std::vector<bundle_entry> entries;

    /**
     * @brief Get link by relation type
     * @param relation Link relation to find
     * @return Link URL or nullopt if not found
     */
    [[nodiscard]] std::optional<std::string_view> get_link(
        link_relation relation) const noexcept {
        for (const auto& link : links) {
            if (link.relation == relation) {
                return link.url;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Check if bundle has next page
     */
    [[nodiscard]] bool has_next() const noexcept {
        return get_link(link_relation::next).has_value();
    }

    /**
     * @brief Check if bundle has previous page
     */
    [[nodiscard]] bool has_previous() const noexcept {
        return get_link(link_relation::previous).has_value();
    }

    /**
     * @brief Get next page URL
     */
    [[nodiscard]] std::optional<std::string_view> next_url() const noexcept {
        return get_link(link_relation::next);
    }

    /**
     * @brief Get previous page URL
     */
    [[nodiscard]] std::optional<std::string_view> previous_url() const noexcept {
        return get_link(link_relation::previous);
    }

    /**
     * @brief Get number of entries in this bundle
     */
    [[nodiscard]] size_t entry_count() const noexcept {
        return entries.size();
    }

    /**
     * @brief Check if bundle is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return entries.empty();
    }

    /**
     * @brief Parse bundle from JSON string
     * @param json JSON string
     * @return Parsed bundle or nullopt on error
     */
    [[nodiscard]] static std::optional<fhir_bundle> parse(
        std::string_view json);

    /**
     * @brief Serialize bundle to JSON string
     * @return JSON string
     */
    [[nodiscard]] std::string to_json() const;
};

/**
 * @brief Builder for creating transaction/batch bundles
 *
 * @example Transaction Bundle
 * ```cpp
 * bundle_builder builder(bundle_type::transaction);
 *
 * builder.add_create("Patient", patient_json)
 *        .add_update("Patient/123", patient_update_json)
 *        .add_delete("Patient/456");
 *
 * auto bundle = builder.build();
 * ```
 */
class bundle_builder {
public:
    /**
     * @brief Construct builder for specified bundle type
     * @param type Bundle type (transaction, batch, collection)
     */
    explicit bundle_builder(bundle_type type = bundle_type::transaction);

    /**
     * @brief Add a create (POST) entry
     * @param resource_type Resource type to create
     * @param resource Resource JSON
     * @param conditional_create If-None-Exist condition (optional)
     * @return Reference to this for chaining
     */
    bundle_builder& add_create(std::string_view resource_type,
                               std::string resource,
                               std::optional<std::string> conditional_create = std::nullopt);

    /**
     * @brief Add an update (PUT) entry
     * @param url Resource URL (e.g., "Patient/123")
     * @param resource Resource JSON
     * @param if_match ETag for conditional update (optional)
     * @return Reference to this for chaining
     */
    bundle_builder& add_update(std::string_view url, std::string resource,
                               std::optional<std::string> if_match = std::nullopt);

    /**
     * @brief Add a patch (PATCH) entry
     * @param url Resource URL
     * @param patch_body Patch content
     * @return Reference to this for chaining
     */
    bundle_builder& add_patch(std::string_view url, std::string patch_body);

    /**
     * @brief Add a delete (DELETE) entry
     * @param url Resource URL to delete
     * @return Reference to this for chaining
     */
    bundle_builder& add_delete(std::string_view url);

    /**
     * @brief Add a read (GET) entry
     * @param url Resource URL to read
     * @return Reference to this for chaining
     */
    bundle_builder& add_read(std::string_view url);

    /**
     * @brief Add a search (GET) entry
     * @param url Search URL
     * @return Reference to this for chaining
     */
    bundle_builder& add_search(std::string_view url);

    /**
     * @brief Build the bundle
     * @return Completed bundle
     */
    [[nodiscard]] fhir_bundle build() const;

    /**
     * @brief Build and serialize to JSON
     * @return Bundle JSON string
     */
    [[nodiscard]] std::string to_json() const;

    /**
     * @brief Get number of entries
     */
    [[nodiscard]] size_t size() const noexcept {
        return entries_.size();
    }

    /**
     * @brief Check if builder has no entries
     */
    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

private:
    bundle_type type_;
    std::vector<bundle_entry> entries_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_FHIR_BUNDLE_H
