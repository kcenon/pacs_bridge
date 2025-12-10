/**
 * @file bridge_metrics.cpp
 * @brief Metrics collection implementation for PACS Bridge
 *
 * @see include/pacs/bridge/monitoring/bridge_metrics.h
 */

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <dirent.h>
#endif
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Internal Metric Storage
// ═══════════════════════════════════════════════════════════════════════════

struct bridge_metrics_collector::metrics_data {
    // HL7 Message Counters (by message_type)
    std::unordered_map<std::string, std::atomic<uint64_t>> hl7_messages_received;
    std::unordered_map<std::string, std::atomic<uint64_t>> hl7_messages_sent;
    std::unordered_map<std::string, std::atomic<uint64_t>> hl7_errors;

    // HL7 Processing Duration Histogram (by message_type)
    // We store raw durations for histogram calculation
    struct histogram_data {
        std::vector<std::chrono::nanoseconds> samples;
        std::mutex mutex;
        static constexpr size_t max_samples = 10000;

        void add_sample(std::chrono::nanoseconds duration) {
            std::lock_guard<std::mutex> lock(mutex);
            if (samples.size() >= max_samples) {
                // Ring buffer behavior: remove oldest
                samples.erase(samples.begin());
            }
            samples.push_back(duration);
        }

        std::vector<std::chrono::nanoseconds> get_samples() const {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex));
            return samples;
        }
    };

    std::unordered_map<std::string, histogram_data> hl7_processing_duration;
    std::mutex hl7_duration_mutex;

    // MWL Counters
    std::atomic<uint64_t> mwl_entries_created{0};
    std::atomic<uint64_t> mwl_entries_updated{0};
    std::atomic<uint64_t> mwl_entries_cancelled{0};
    histogram_data mwl_query_duration;

    // Queue Metrics (by destination)
    std::unordered_map<std::string, std::atomic<size_t>> queue_depth;
    std::unordered_map<std::string, std::atomic<uint64_t>> messages_enqueued;
    std::unordered_map<std::string, std::atomic<uint64_t>> messages_delivered;
    std::unordered_map<std::string, std::atomic<uint64_t>> delivery_failures;
    std::unordered_map<std::string, std::atomic<uint64_t>> dead_letters;
    std::mutex queue_mutex;

    // Connection Metrics
    std::atomic<size_t> mllp_active_connections{0};
    std::atomic<uint64_t> mllp_total_connections{0};
    std::atomic<size_t> fhir_active_requests{0};
    std::unordered_map<std::string, std::atomic<uint64_t>> fhir_requests;
    std::mutex fhir_mutex;

    // System Metrics
    std::atomic<double> process_cpu_seconds{0.0};
    std::atomic<size_t> process_memory_bytes{0};
    std::atomic<size_t> process_open_fds{0};

    metrics_data() = default;
};

// ═══════════════════════════════════════════════════════════════════════════
// Singleton Instance
// ═══════════════════════════════════════════════════════════════════════════

bridge_metrics_collector& bridge_metrics_collector::instance() {
    static bridge_metrics_collector instance;
    return instance;
}

bridge_metrics_collector::bridge_metrics_collector()
    : data_(std::make_unique<metrics_data>())
#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
      ,
      performance_monitor_("pacs_bridge")
#endif
{
}

bridge_metrics_collector::~bridge_metrics_collector() { shutdown(); }

// ═══════════════════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════════════════

bool bridge_metrics_collector::initialize(const std::string& service_name,
                                          uint16_t prometheus_port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_.load()) {
        return true;  // Already initialized
    }

    service_name_ = service_name;
    prometheus_port_ = prometheus_port;

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    // Initialize monitoring_system performance monitor
    auto init_result = performance_monitor_.initialize();
    if (init_result.is_err()) {
        return false;
    }

    // Create Prometheus exporter if port is specified
    if (prometheus_port > 0) {
        kcenon::monitoring::metric_export_config config;
        config.port = prometheus_port;
        config.format = kcenon::monitoring::metric_export_format::prometheus_text;
        config.job_name = service_name;

        prometheus_exporter_ =
            std::make_unique<kcenon::monitoring::prometheus_exporter>(config);
    }
#endif

    initialized_.store(true);
    enabled_.store(true);
    return true;
}

void bridge_metrics_collector::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_.load()) {
        return;
    }

    enabled_.store(false);

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    performance_monitor_.cleanup();
    if (prometheus_exporter_) {
        prometheus_exporter_->shutdown();
        prometheus_exporter_.reset();
    }
#endif

    initialized_.store(false);
}

// ═══════════════════════════════════════════════════════════════════════════
// HL7 Message Metrics
// ═══════════════════════════════════════════════════════════════════════════

void bridge_metrics_collector::record_hl7_message_received(
    const std::string& message_type) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    data_->hl7_messages_received[message_type]++;
}

void bridge_metrics_collector::record_hl7_message_sent(
    const std::string& message_type) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    data_->hl7_messages_sent[message_type]++;
}

void bridge_metrics_collector::record_hl7_processing_duration(
    const std::string& message_type, std::chrono::nanoseconds duration) {
    if (!enabled_.load())
        return;

    {
        std::lock_guard<std::mutex> lock(data_->hl7_duration_mutex);
        data_->hl7_processing_duration[message_type].add_sample(duration);
    }

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    // Also record in performance_monitor for advanced analysis
    std::string operation_name = "hl7_processing_" + message_type;
    performance_monitor_.get_profiler().record_sample(operation_name, duration,
                                                       true);
#endif
}

void bridge_metrics_collector::record_hl7_error(const std::string& message_type,
                                                const std::string& error_type) {
    if (!enabled_.load())
        return;

    std::string key = message_type + ":" + error_type;
    std::lock_guard<std::mutex> lock(mutex_);
    data_->hl7_errors[key]++;
}

// ═══════════════════════════════════════════════════════════════════════════
// MWL Metrics
// ═══════════════════════════════════════════════════════════════════════════

void bridge_metrics_collector::record_mwl_entry_created() {
    if (!enabled_.load())
        return;
    data_->mwl_entries_created++;
}

void bridge_metrics_collector::record_mwl_entry_updated() {
    if (!enabled_.load())
        return;
    data_->mwl_entries_updated++;
}

void bridge_metrics_collector::record_mwl_entry_cancelled() {
    if (!enabled_.load())
        return;
    data_->mwl_entries_cancelled++;
}

void bridge_metrics_collector::record_mwl_query_duration(
    std::chrono::nanoseconds duration) {
    if (!enabled_.load())
        return;

    data_->mwl_query_duration.add_sample(duration);

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    performance_monitor_.get_profiler().record_sample("mwl_query", duration,
                                                       true);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Queue Metrics
// ═══════════════════════════════════════════════════════════════════════════

void bridge_metrics_collector::set_queue_depth(const std::string& destination,
                                               size_t depth) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(data_->queue_mutex);
    data_->queue_depth[destination].store(depth);
}

void bridge_metrics_collector::record_message_enqueued(
    const std::string& destination) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(data_->queue_mutex);
    data_->messages_enqueued[destination]++;
}

void bridge_metrics_collector::record_message_delivered(
    const std::string& destination) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(data_->queue_mutex);
    data_->messages_delivered[destination]++;
}

void bridge_metrics_collector::record_delivery_failure(
    const std::string& destination) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(data_->queue_mutex);
    data_->delivery_failures[destination]++;
}

void bridge_metrics_collector::record_dead_letter(
    const std::string& destination) {
    if (!enabled_.load())
        return;

    std::lock_guard<std::mutex> lock(data_->queue_mutex);
    data_->dead_letters[destination]++;
}

// ═══════════════════════════════════════════════════════════════════════════
// Connection Metrics
// ═══════════════════════════════════════════════════════════════════════════

void bridge_metrics_collector::set_mllp_active_connections(size_t count) {
    if (!enabled_.load())
        return;
    data_->mllp_active_connections.store(count);
}

void bridge_metrics_collector::record_mllp_connection() {
    if (!enabled_.load())
        return;
    data_->mllp_total_connections++;
}

void bridge_metrics_collector::set_fhir_active_requests(size_t count) {
    if (!enabled_.load())
        return;
    data_->fhir_active_requests.store(count);
}

void bridge_metrics_collector::record_fhir_request(const std::string& method,
                                                   const std::string& resource) {
    if (!enabled_.load())
        return;

    std::string key = method + ":" + resource;
    std::lock_guard<std::mutex> lock(data_->fhir_mutex);
    data_->fhir_requests[key]++;
}

// ═══════════════════════════════════════════════════════════════════════════
// System Metrics
// ═══════════════════════════════════════════════════════════════════════════

void bridge_metrics_collector::update_system_metrics() {
    if (!enabled_.load())
        return;

#if defined(__APPLE__)
    // macOS: Get process memory using mach
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        data_->process_memory_bytes.store(info.resident_size);
    }

    // Get CPU time
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        double user_seconds = usage.ru_utime.tv_sec +
                              usage.ru_utime.tv_usec / 1000000.0;
        double system_seconds = usage.ru_stime.tv_sec +
                                usage.ru_stime.tv_usec / 1000000.0;
        data_->process_cpu_seconds.store(user_seconds + system_seconds);
    }

    // Get open file descriptors (approximate)
    int max_fd = getdtablesize();
    int open_fds = 0;
    for (int i = 0; i < std::min(max_fd, 1024); i++) {
        if (fcntl(i, F_GETFD) != -1) {
            open_fds++;
        }
    }
    data_->process_open_fds.store(static_cast<size_t>(open_fds));

#elif defined(__linux__)
    // Linux: Get process memory from /proc/self/statm
    FILE* statm = fopen("/proc/self/statm", "r");
    if (statm) {
        unsigned long size, resident;
        if (fscanf(statm, "%lu %lu", &size, &resident) == 2) {
            long page_size = sysconf(_SC_PAGESIZE);
            data_->process_memory_bytes.store(resident * page_size);
        }
        fclose(statm);
    }

    // Get CPU time
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        double user_seconds = usage.ru_utime.tv_sec +
                              usage.ru_utime.tv_usec / 1000000.0;
        double system_seconds = usage.ru_stime.tv_sec +
                                usage.ru_stime.tv_usec / 1000000.0;
        data_->process_cpu_seconds.store(user_seconds + system_seconds);
    }

    // Get open file descriptors
    DIR* fd_dir = opendir("/proc/self/fd");
    if (fd_dir) {
        int count = 0;
        while (readdir(fd_dir) != nullptr) {
            count++;
        }
        closedir(fd_dir);
        // Subtract . and ..
        data_->process_open_fds.store(static_cast<size_t>(std::max(0, count - 2)));
    }

#elif defined(_WIN32)
    // Windows: Get process memory
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        data_->process_memory_bytes.store(pmc.WorkingSetSize);
    }

    // Get CPU time
    FILETIME creation_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
                        &kernel_time, &user_time)) {
        ULARGE_INTEGER kernel, user;
        kernel.LowPart = kernel_time.dwLowDateTime;
        kernel.HighPart = kernel_time.dwHighDateTime;
        user.LowPart = user_time.dwLowDateTime;
        user.HighPart = user_time.dwHighDateTime;

        // Convert 100-nanosecond intervals to seconds
        double total_seconds =
            (kernel.QuadPart + user.QuadPart) / 10000000.0;
        data_->process_cpu_seconds.store(total_seconds);
    }

    // Get handle count (approximate for open fds)
    DWORD handle_count;
    if (GetProcessHandleCount(GetCurrentProcess(), &handle_count)) {
        data_->process_open_fds.store(static_cast<size_t>(handle_count));
    }
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Prometheus Export
// ═══════════════════════════════════════════════════════════════════════════

namespace {

void write_counter_metric(std::ostringstream& ss, const std::string& name,
                          const std::string& help,
                          const std::string& labels, uint64_t value) {
    ss << "# HELP " << name << " " << help << "\n";
    ss << "# TYPE " << name << " counter\n";
    ss << name;
    if (!labels.empty()) {
        ss << "{" << labels << "}";
    }
    ss << " " << value << "\n";
}

void write_gauge_metric(std::ostringstream& ss, const std::string& name,
                        const std::string& help,
                        const std::string& labels, double value) {
    ss << "# HELP " << name << " " << help << "\n";
    ss << "# TYPE " << name << " gauge\n";
    ss << name;
    if (!labels.empty()) {
        ss << "{" << labels << "}";
    }
    ss << " " << std::fixed << std::setprecision(6) << value << "\n";
}

void write_histogram_metric(
    std::ostringstream& ss, const std::string& name, const std::string& help,
    const std::string& labels,
    const std::vector<std::chrono::nanoseconds>& samples,
    const std::vector<double>& buckets) {
    if (samples.empty())
        return;

    ss << "# HELP " << name << " " << help << "\n";
    ss << "# TYPE " << name << " histogram\n";

    // Calculate bucket counts
    std::vector<uint64_t> bucket_counts(buckets.size(), 0);
    double sum = 0.0;

    for (const auto& sample : samples) {
        double seconds =
            std::chrono::duration<double>(sample).count();
        sum += seconds;

        for (size_t i = 0; i < buckets.size(); i++) {
            if (seconds <= buckets[i]) {
                bucket_counts[i]++;
            }
        }
    }

    std::string base_labels = labels.empty() ? "" : labels + ",";

    // Write bucket counts
    uint64_t cumulative = 0;
    for (size_t i = 0; i < buckets.size(); i++) {
        cumulative += bucket_counts[i];
        ss << name << "_bucket{" << base_labels << "le=\""
           << std::fixed << std::setprecision(3) << buckets[i] << "\"} "
           << cumulative << "\n";
    }

    // +Inf bucket
    ss << name << "_bucket{" << base_labels << "le=\"+Inf\"} "
       << samples.size() << "\n";

    // Sum and count
    ss << name << "_sum";
    if (!labels.empty()) {
        ss << "{" << labels << "}";
    }
    ss << " " << std::fixed << std::setprecision(6) << sum << "\n";

    ss << name << "_count";
    if (!labels.empty()) {
        ss << "{" << labels << "}";
    }
    ss << " " << samples.size() << "\n";
}

}  // namespace

std::string bridge_metrics_collector::get_prometheus_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;

    auto buckets = default_latency_buckets();

    // HL7 Message Counters
    for (const auto& [msg_type, count] : data_->hl7_messages_received) {
        std::string labels = "message_type=\"" + msg_type + "\"";
        ss << "# HELP hl7_messages_received_total Total HL7 messages received\n";
        ss << "# TYPE hl7_messages_received_total counter\n";
        ss << "hl7_messages_received_total{" << labels << "} "
           << count.load() << "\n";
    }

    for (const auto& [msg_type, count] : data_->hl7_messages_sent) {
        std::string labels = "message_type=\"" + msg_type + "\"";
        ss << "# HELP hl7_messages_sent_total Total HL7 messages sent\n";
        ss << "# TYPE hl7_messages_sent_total counter\n";
        ss << "hl7_messages_sent_total{" << labels << "} "
           << count.load() << "\n";
    }

    // HL7 Errors
    for (const auto& [key, count] : data_->hl7_errors) {
        auto pos = key.find(':');
        std::string msg_type = (pos != std::string::npos) ? key.substr(0, pos) : key;
        std::string error_type = (pos != std::string::npos) ? key.substr(pos + 1) : "unknown";
        std::string labels = "message_type=\"" + msg_type + "\",error_type=\"" + error_type + "\"";
        ss << "# HELP hl7_message_errors_total Total HL7 message errors\n";
        ss << "# TYPE hl7_message_errors_total counter\n";
        ss << "hl7_message_errors_total{" << labels << "} "
           << count.load() << "\n";
    }

    // HL7 Processing Duration Histograms
    {
        std::lock_guard<std::mutex> duration_lock(
            const_cast<std::mutex&>(data_->hl7_duration_mutex));
        for (auto& [msg_type, histogram] : data_->hl7_processing_duration) {
            std::string labels = "message_type=\"" + msg_type + "\"";
            auto samples = histogram.get_samples();
            write_histogram_metric(
                ss, "hl7_message_processing_duration_seconds",
                "HL7 message processing duration in seconds",
                labels, samples, buckets);
        }
    }

    // MWL Counters
    write_counter_metric(ss, "mwl_entries_created_total",
                         "Total MWL entries created", "",
                         data_->mwl_entries_created.load());
    write_counter_metric(ss, "mwl_entries_updated_total",
                         "Total MWL entries updated", "",
                         data_->mwl_entries_updated.load());
    write_counter_metric(ss, "mwl_entries_cancelled_total",
                         "Total MWL entries cancelled", "",
                         data_->mwl_entries_cancelled.load());

    // MWL Query Duration
    {
        auto samples = data_->mwl_query_duration.get_samples();
        write_histogram_metric(ss, "mwl_query_duration_seconds",
                               "MWL query duration in seconds",
                               "", samples, buckets);
    }

    // Queue Metrics
    {
        std::lock_guard<std::mutex> queue_lock(
            const_cast<std::mutex&>(data_->queue_mutex));

        for (const auto& [dest, depth] : data_->queue_depth) {
            std::string labels = "destination=\"" + dest + "\"";
            write_gauge_metric(ss, "queue_depth", "Current queue depth",
                               labels, static_cast<double>(depth.load()));
        }

        for (const auto& [dest, count] : data_->messages_enqueued) {
            std::string labels = "destination=\"" + dest + "\"";
            ss << "# HELP queue_messages_enqueued_total Total messages enqueued\n";
            ss << "# TYPE queue_messages_enqueued_total counter\n";
            ss << "queue_messages_enqueued_total{" << labels << "} "
               << count.load() << "\n";
        }

        for (const auto& [dest, count] : data_->messages_delivered) {
            std::string labels = "destination=\"" + dest + "\"";
            ss << "# HELP queue_messages_delivered_total Total messages delivered\n";
            ss << "# TYPE queue_messages_delivered_total counter\n";
            ss << "queue_messages_delivered_total{" << labels << "} "
               << count.load() << "\n";
        }

        for (const auto& [dest, count] : data_->delivery_failures) {
            std::string labels = "destination=\"" + dest + "\"";
            ss << "# HELP queue_delivery_failures_total Total delivery failures\n";
            ss << "# TYPE queue_delivery_failures_total counter\n";
            ss << "queue_delivery_failures_total{" << labels << "} "
               << count.load() << "\n";
        }

        for (const auto& [dest, count] : data_->dead_letters) {
            std::string labels = "destination=\"" + dest + "\"";
            ss << "# HELP queue_dead_letters_total Total dead letters\n";
            ss << "# TYPE queue_dead_letters_total counter\n";
            ss << "queue_dead_letters_total{" << labels << "} "
               << count.load() << "\n";
        }
    }

    // Connection Metrics
    write_gauge_metric(ss, "mllp_active_connections",
                       "Current active MLLP connections", "",
                       static_cast<double>(data_->mllp_active_connections.load()));
    write_counter_metric(ss, "mllp_total_connections",
                         "Total MLLP connections", "",
                         data_->mllp_total_connections.load());
    write_gauge_metric(ss, "fhir_active_requests",
                       "Current active FHIR requests", "",
                       static_cast<double>(data_->fhir_active_requests.load()));

    {
        std::lock_guard<std::mutex> fhir_lock(
            const_cast<std::mutex&>(data_->fhir_mutex));
        for (const auto& [key, count] : data_->fhir_requests) {
            auto pos = key.find(':');
            std::string method = (pos != std::string::npos) ? key.substr(0, pos) : key;
            std::string resource = (pos != std::string::npos) ? key.substr(pos + 1) : "unknown";
            std::string labels = "method=\"" + method + "\",resource=\"" + resource + "\"";
            ss << "# HELP fhir_requests_total Total FHIR requests\n";
            ss << "# TYPE fhir_requests_total counter\n";
            ss << "fhir_requests_total{" << labels << "} "
               << count.load() << "\n";
        }
    }

    // System Metrics
    write_counter_metric(ss, "process_cpu_seconds_total",
                         "Total CPU time in seconds", "",
                         static_cast<uint64_t>(data_->process_cpu_seconds.load()));
    write_gauge_metric(ss, "process_resident_memory_bytes",
                       "Resident memory size in bytes", "",
                       static_cast<double>(data_->process_memory_bytes.load()));
    write_gauge_metric(ss, "process_open_fds",
                       "Number of open file descriptors", "",
                       static_cast<double>(data_->process_open_fds.load()));

    return ss.str();
}

}  // namespace pacs::bridge::monitoring
