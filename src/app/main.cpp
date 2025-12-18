/**
 * @file main.cpp
 * @brief PACS Bridge CLI executable entrypoint
 *
 * Provides a command-line interface to run the PACS Bridge server
 * for Phase 2 MPPS-to-HL7 workflow.
 *
 * Usage:
 *   pacs_bridge --config <path>           Start with configuration file
 *   pacs_bridge --help                    Show help message
 *   pacs_bridge --version                 Show version information
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/175
 */

#include "pacs/bridge/bridge_server.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view VERSION = "0.1.0";
constexpr std::string_view PROGRAM_NAME = "pacs_bridge";

void print_version() {
    std::cout << PROGRAM_NAME << " version " << VERSION << "\n";
    std::cout << "PACS Bridge - HL7-DICOM Healthcare Integration Gateway\n";
    std::cout << "https://github.com/kcenon/pacs_bridge\n";
}

void print_usage() {
    std::cout << "Usage: " << PROGRAM_NAME << " [OPTIONS]\n\n";
    std::cout << "PACS Bridge - Phase 2 MPPS to HL7 Integration Server\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config <path>    Path to configuration file (YAML/JSON)\n";
    std::cout << "  -h, --help             Show this help message\n";
    std::cout << "  -v, --version          Show version information\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << PROGRAM_NAME << " --config /etc/pacs_bridge/config.yaml\n";
    std::cout << "  " << PROGRAM_NAME << " -c ./config.yaml\n";
    std::cout << "\n";
    std::cout << "Configuration:\n";
    std::cout << "  The configuration file should contain:\n";
    std::cout << "    - pacs: PACS system connection settings (MPPS SCP)\n";
    std::cout << "    - hl7.outbound_destinations: HL7 message destinations\n";
    std::cout << "    - queue: Message queue persistence settings\n";
    std::cout << "    - routing_rules: Message routing rules\n";
    std::cout << "\n";
    std::cout << "Signals:\n";
    std::cout << "  SIGINT  (Ctrl+C)    Graceful shutdown\n";
    std::cout << "  SIGTERM             Graceful shutdown\n";
}

struct cli_options {
    std::filesystem::path config_path;
    bool show_help = false;
    bool show_version = false;
    bool valid = true;
    std::string error_message;
};

cli_options parse_args(int argc, char* argv[]) {
    cli_options opts;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            return opts;
        }

        if (arg == "-v" || arg == "--version") {
            opts.show_version = true;
            return opts;
        }

        if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                opts.valid = false;
                opts.error_message = "Missing argument for --config";
                return opts;
            }
            opts.config_path = argv[++i];
            continue;
        }

        // Unknown argument
        opts.valid = false;
        opts.error_message = "Unknown argument: " + std::string(arg);
        return opts;
    }

    return opts;
}

}  // namespace

int main(int argc, char* argv[]) {
    auto opts = parse_args(argc, argv);

    if (!opts.valid) {
        std::cerr << "Error: " << opts.error_message << "\n\n";
        print_usage();
        return EXIT_FAILURE;
    }

    if (opts.show_version) {
        print_version();
        return EXIT_SUCCESS;
    }

    if (opts.show_help) {
        print_usage();
        return EXIT_SUCCESS;
    }

    if (opts.config_path.empty()) {
        std::cerr << "Error: Configuration file required\n\n";
        print_usage();
        return EXIT_FAILURE;
    }

    if (!std::filesystem::exists(opts.config_path)) {
        std::cerr << "Error: Configuration file not found: "
                  << opts.config_path << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Starting PACS Bridge " << VERSION << "...\n";
    std::cout << "Configuration: " << opts.config_path << "\n";

    try {
        pacs::bridge::bridge_server server(opts.config_path);

        auto start_result = server.start();
        if (!start_result) {
            std::cerr << "Failed to start server: "
                      << pacs::bridge::to_string(start_result.error()) << "\n";
            return EXIT_FAILURE;
        }

        std::cout << "PACS Bridge '" << server.name() << "' started successfully\n";
        std::cout << "Press Ctrl+C to shutdown...\n";

        // Block until shutdown signal
        server.wait_for_shutdown();

        std::cout << "\nShutdown signal received, stopping server...\n";

        // Get final statistics before shutdown
        auto stats = server.get_statistics();
        std::cout << "Final statistics:\n";
        std::cout << "  MPPS events received: " << stats.mpps_events_received << "\n";
        std::cout << "  Workflow executions:  " << stats.workflow_executions << "\n";
        std::cout << "  Messages delivered:   " << stats.queue_total_delivered << "\n";
        std::cout << "  Uptime:               " << stats.uptime.count() << "s\n";

        server.stop();

        std::cout << "PACS Bridge stopped successfully\n";
        return EXIT_SUCCESS;

    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
