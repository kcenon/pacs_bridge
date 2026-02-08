# Contributing Guidelines

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Testing Requirements](#testing-requirements)
- [Pull Request Process](#pull-request-process)
- [Issue Guidelines](#issue-guidelines)

---

## Getting Started

Thank you for your interest in contributing to PACS Bridge! This document provides guidelines for contributing to the project.

### Types of Contributions

| Type | Description |
|------|-------------|
| Bug Fixes | Fix existing issues |
| Features | Add new functionality |
| Documentation | Improve docs |
| Tests | Add test coverage |
| Performance | Optimize code |

### Before You Start

1. Check existing issues and PRs
2. Discuss major changes in an issue first
3. Familiarize yourself with the codebase

---

## Development Setup

### Prerequisites

| Software | Version | Notes |
|----------|---------|-------|
| C++ Compiler | GCC 13+, Clang 14+, MSVC 2022+ | C++23 support required |
| CMake | 3.20+ | Build system |
| Git | Latest | Version control |
| vcpkg | Latest | Package management |

### Clone and Build

```bash
# Clone the repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Clone dependencies (optional for full build)
cd ..
git clone https://github.com/kcenon/common_system.git
git clone https://github.com/kcenon/thread_system.git
git clone https://github.com/kcenon/logger_system.git
git clone https://github.com/kcenon/container_system.git
git clone https://github.com/kcenon/network_system.git
git clone https://github.com/kcenon/monitoring_system.git
git clone https://github.com/kcenon/pacs_system.git

# Build
cd pacs_bridge
cmake -B build -DBRIDGE_BUILD_TESTS=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

### IDE Setup

#### VS Code

Recommended extensions:

```json
{
  "recommendations": [
    "ms-vscode.cpptools",
    "ms-vscode.cmake-tools",
    "xaver.clang-format",
    "ms-vscode.cpptools-extension-pack"
  ]
}
```

Settings:

```json
{
  "cmake.configureOnOpen": true,
  "cmake.buildDirectory": "${workspaceFolder}/build",
  "C_Cpp.default.cppStandard": "c++23"
}
```

#### CLion

1. Open project as CMake project
2. Set C++ standard to C++23
3. Configure CMake options in Settings

---

## Coding Standards

### Code Style

PACS Bridge follows a consistent code style:

#### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | snake_case | `hl7_message` |
| Functions | snake_case | `parse_message` |
| Variables | snake_case | `message_count` |
| Constants | UPPER_SNAKE | `MAX_CONNECTIONS` |
| Namespaces | snake_case | `pacs::bridge` |
| Template params | PascalCase | `typename MessageType` |

#### File Organization

```cpp
// file_name.h

#pragma once

#include <system_headers>

#include "project_headers.h"

namespace pacs::bridge {

/**
 * @brief Brief description of the class.
 *
 * Detailed description if needed.
 */
class my_class {
public:
    // Constructors/destructors
    my_class();
    ~my_class();

    // Public methods
    void public_method();

private:
    // Private members
    int member_variable_;
};

}  // namespace pacs::bridge
```

#### Modern C++ Practices

Use modern C++ features appropriately:

```cpp
// Use auto where type is obvious
auto message = parser.parse(raw_data);

// Use range-based for loops
for (const auto& segment : message.segments()) {
    process(segment);
}

// Use smart pointers
auto handler = std::make_unique<adt_handler>(cache);

// Use std::optional for optional values
std::optional<patient_record> lookup(const std::string& id);

// Use std::expected or result<T> for error handling
result<hl7_message> parse(const std::string& raw);

// Use structured bindings
auto [success, value] = try_operation();

// Use concepts for type constraints
template<MessageHandler T>
void register_handler(T&& handler);
```

### Documentation

#### Header Files

```cpp
/**
 * @brief Parse an HL7 message from raw string.
 *
 * Parses the raw HL7 message string and returns a structured
 * hl7_message object.
 *
 * @param raw_message The raw HL7 message string with MLLP framing removed.
 * @return result<hl7_message> The parsed message or error.
 *
 * @throws std::invalid_argument If the message format is invalid.
 *
 * @example
 * @code
 * hl7_parser parser;
 * auto result = parser.parse(raw_message);
 * if (result) {
 *     process(result.value());
 * }
 * @endcode
 */
result<hl7_message> parse(const std::string& raw_message);
```

#### Implementation Files

```cpp
// Complex logic should be documented inline
void process_message(const hl7_message& message) {
    // Extract the message type from MSH-9
    auto message_type = message.get_field("MSH", 9);

    // Route based on message type
    // ADT messages go to patient cache
    // ORM messages go to MWL manager
    if (message_type.starts_with("ADT")) {
        route_to_cache(message);
    } else if (message_type.starts_with("ORM")) {
        route_to_mwl(message);
    }
}
```

### Error Handling

Use the result type for error handling:

```cpp
// Define result type
template<typename T>
using result = std::expected<T, error>;

// Return errors, don't throw
result<hl7_message> parse(const std::string& raw) {
    if (raw.empty()) {
        return std::unexpected(error{
            .code = "PACS-3001",
            .message = "Empty message"
        });
    }
    // ...
}

// Handle errors at call site
auto result = parser.parse(raw);
if (!result) {
    log_error(result.error());
    return;
}
auto& message = result.value();
```

---

## Testing Requirements

### Test Coverage

| Coverage Level | Target |
|---------------|--------|
| Line Coverage | 80% |
| Branch Coverage | 70% |
| Function Coverage | 90% |

### Test Types

| Type | Location | Purpose |
|------|----------|---------|
| Unit Tests | `tests/` | Test individual components |
| Integration Tests | `tests/integration/` | Test component interaction |
| Stress Tests | `tests/stress/` | Performance testing |

#### Integration Test Categories (Issue #161, #162)

| Test File | Focus Area | Labels |
|-----------|------------|--------|
| `mllp_connection_test.cpp` | Connection setup/teardown, timeout, reconnection | `integration;mllp;connection` |
| `pacs_worklist_test.cpp` | Worklist query, status updates | `integration;pacs;worklist` |
| `e2e_scenario_test.cpp` | Complete imaging workflow scenarios | `integration;e2e;scenario` |
| `mpps_integration_test.cpp` | MPPS N-CREATE/N-SET flows | `integration;mpps` |
| `failover_test.cpp` | RIS failover scenarios | `integration;failover` |
| `disaster_recovery_test.cpp` | Network failures, message loss, retry logic, resilience | `integration;disaster-recovery;resilience` |

#### pacs_system Integration Tests (Issue #188)

Tests for pacs_bridge <-> pacs_system database operations:

| Test File | Focus Area | Labels |
|-----------|------------|--------|
| `mwl_database_test.cpp` | MWL add/query/update/cancel with database | `integration;pacs_system;mwl` |
| `mpps_persistence_test.cpp` | MPPS record persistence and status updates | `integration;pacs_system;mpps;persistence` |
| `pacs_system_e2e_test.cpp` | Complete workflow: HL7->MWL->MPPS->HL7 | `integration;pacs_system;e2e;workflow` |

Shared test infrastructure in `tests/integration/pacs_system_test_base.h`:
- `mwl_test_data_generator` - Creates test MWL items
- `mpps_test_data_generator` - Creates test MPPS datasets
- `pacs_system_test_fixture` - Common setup with in-memory SQLite

### Writing Tests

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_parser.h"

namespace pacs::bridge::test {

class HL7ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<hl7_parser>();
    }

    std::unique_ptr<hl7_parser> parser_;
};

TEST_F(HL7ParserTest, ParseValidMessage) {
    // Arrange
    std::string raw = "MSH|^~\\&|...";

    // Act
    auto result = parser_->parse(raw);

    // Assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get_segment_count(), 3);
}

TEST_F(HL7ParserTest, ReturnsErrorForEmptyMessage) {
    // Arrange
    std::string raw = "";

    // Act
    auto result = parser_->parse(raw);

    // Assert
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "PACS-3001");
}

}  // namespace pacs::bridge::test
```

### Running Tests

```bash
# Build with tests
cmake -B build -DBRIDGE_BUILD_TESTS=ON
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
./build/bin/hl7_test --gtest_filter="HL7ParserTest.*"

# Run tests by label
ctest --test-dir build -L "emr"           # EMR tests only
ctest --test-dir build -L "integration"   # Integration tests
ctest --test-dir build -L "phase5"        # Phase 5 (EMR) tests

# Run with coverage
cmake -B build -DBRIDGE_BUILD_TESTS=ON -DBRIDGE_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
lcov --capture --directory build --output-file coverage.info
```

### EMR Integration Tests

EMR integration tests require additional setup:

```bash
# Start HAPI FHIR server for integration tests
docker-compose -f tests/docker-compose.test.yml up -d

# Run EMR E2E tests with FHIR server
PACS_BRIDGE_EMR_E2E_TESTS=1 \
HAPI_FHIR_URL=http://localhost:8080/fhir \
ctest --test-dir build -L "emr_integration"

# Stop test infrastructure
docker-compose -f tests/docker-compose.test.yml down
```

Test fixtures are located in `tests/fixtures/`:
- `fhir_resources/`: FHIR R4 resource examples (Patient, DiagnosticReport, etc.)
- `mock_responses/`: Mock EMR server responses (OAuth tokens, errors)

---

## Pull Request Process

### Branch Naming

Use descriptive branch names:

```
feature/123-add-fhir-subscription
bugfix/456-fix-mllp-timeout
docs/improve-api-documentation
refactor/extract-mapping-module
```

### Commit Messages

Follow conventional commits:

```
type(scope): subject

body

footer
```

Types:

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `docs` | Documentation |
| `style` | Formatting |
| `refactor` | Code restructure |
| `test` | Add tests |
| `chore` | Maintenance |

Examples:

```
feat(hl7): add SIU message handler

Implement SIU^S12, SIU^S13, and SIU^S14 message handling
for scheduling workflows.

Closes #123
```

```
fix(mllp): resolve connection timeout issue

Increase default timeout and add retry logic for
unstable network conditions.

Fixes #456
```

### PR Checklist

Before submitting:

- [ ] Code follows style guidelines
- [ ] Tests pass locally
- [ ] New code has tests
- [ ] Documentation updated
- [ ] No merge conflicts
- [ ] PR description is complete

### PR Template

```markdown
## Summary

Brief description of changes.

## Changes

- Change 1
- Change 2
- Change 3

## Testing

How was this tested?

## Related Issues

Closes #123

## Checklist

- [ ] Tests pass
- [ ] Documentation updated
- [ ] Code review requested
```

### Review Process

1. Create PR against `main` branch
2. Automated checks run (CI/CD)
3. Request review from maintainers
4. Address review feedback
5. Maintainer approves and merges

---

## Issue Guidelines

### Bug Reports

Include:

1. **Environment** - OS, compiler, version
2. **Steps to Reproduce** - Clear steps
3. **Expected Behavior** - What should happen
4. **Actual Behavior** - What happens
5. **Logs/Screenshots** - Supporting info

Template:

```markdown
## Environment

- OS: Ubuntu 22.04
- Compiler: GCC 13.1
- PACS Bridge Version: 1.0.0

## Steps to Reproduce

1. Step one
2. Step two
3. Step three

## Expected Behavior

Description of expected behavior.

## Actual Behavior

Description of actual behavior.

## Logs

```
Error message or log output
```

## Additional Context

Any other relevant information.
```

### Feature Requests

Include:

1. **Problem** - What problem does this solve?
2. **Solution** - Proposed solution
3. **Alternatives** - Other approaches considered
4. **Context** - Use case and background

Template:

```markdown
## Problem

Description of the problem or need.

## Proposed Solution

Description of the proposed feature.

## Alternatives Considered

Other approaches that were considered.

## Use Case

How would this feature be used?

## Additional Context

Any other relevant information.
```

---

## Code of Conduct

### Our Standards

- Be respectful and inclusive
- Accept constructive criticism
- Focus on what's best for the community
- Show empathy towards others

### Reporting Issues

Report unacceptable behavior to: kcenon@naver.com

---

## Getting Help

- **Questions**: Open a discussion
- **Bugs**: Open an issue
- **Features**: Open an issue first

---

## Related Documentation

- [Architecture Overview](architecture.md) - System architecture
- [Module Descriptions](modules.md) - Module details
- [API Reference](../api/hl7-messages.md) - API specifications
