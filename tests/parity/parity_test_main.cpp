// Parity harness GoogleTest test environment hook.
//
// The og_test_parity binary uses tests/integration_main.cpp as its main()
// entry point, so this file does not define `main`. It registers a
// GoogleTest environment that brings the per-process parity bootstrap
// (PhysFS scenario search paths, hooks installation) up exactly once,
// after integration_main's io_init() has already started PhysFS.

#include "parity_bootstrap.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

class ParityEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        if (!scope_)
            scope_ = std::make_unique<og::parity::BootstrapScope>("og_test_parity");
    }

    void TearDown() override
    {
        scope_.reset();
    }

private:
    std::unique_ptr<og::parity::BootstrapScope> scope_;
};

// AddGlobalTestEnvironment runs after gtest is initialised by
// integration_main.cpp (RUN_ALL_TESTS triggers Environment::SetUp before
// the first test). Stored in an inline namespace variable so the static
// initializer runs at binary load.
[[maybe_unused]] const auto* g_parity_env =
    ::testing::AddGlobalTestEnvironment(new ParityEnvironment);

} // namespace
