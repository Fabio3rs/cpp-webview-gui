#include "app/dev_ports.h"
#include <gtest/gtest.h>

TEST(DevPorts, ParsesValidPort) {
    EXPECT_EQ(app::dev_ports::parse("6123", "APP_VITE_PORT"), 6123);
}

TEST(DevPorts, RejectsInvalidPort) {
    EXPECT_THROW(app::dev_ports::parse("0", "APP_VITE_PORT"),
                 std::invalid_argument);
    EXPECT_THROW(app::dev_ports::parse("65536", "APP_VITE_PORT"),
                 std::invalid_argument);
    EXPECT_THROW(app::dev_ports::parse("abc", "APP_VITE_PORT"),
                 std::invalid_argument);
}

TEST(DevPorts, BuildsLoopbackOrigin) {
    EXPECT_EQ(app::dev_ports::vite_origin({6123, 6124}),
              "http://127.0.0.1:6123");
}
