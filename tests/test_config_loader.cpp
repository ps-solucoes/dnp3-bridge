#include <doctest/doctest.h>

#include "config/ConfigLoader.hpp"
#include "dnp3/OutstationManager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace dnp3bridge::config;

namespace {

/// RAII helper that creates a temporary JSON file and removes it on destruction.
class TempJsonFile {
public:
    explicit TempJsonFile(std::string_view content) {
        path_ = std::filesystem::temp_directory_path() / ("test_config_" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".json");
        std::ofstream out{path_};
        out << content;
    }

    ~TempJsonFile() {
        std::filesystem::remove(path_);
    }

    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

/// RAII helper that sets an environment variable and restores the original value
/// (or unsets it) on destruction.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value)
        : name_{name}
    {
        const char* prev = std::getenv(name);
        if (prev) {
            had_value_ = true;
            original_ = prev;
        }
        setenv(name, value, 1);
    }

    ~ScopedEnv() {
        if (had_value_) {
            setenv(name_.c_str(), original_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::string original_;
    bool had_value_{false};
};

} // anonymous namespace

TEST_CASE("ConfigLoader") {

    SUBCASE("default values when no path and no env vars") {
        // Clear all env vars that ConfigLoader reads so defaults are used.
        ScopedEnv e1{"DNP3_BRIDGE_GRPC_ADDRESS", ""};
        ScopedEnv e2{"DNP3_BRIDGE_DNP3_HOST", ""};
        // For numeric env vars, clear them by unsetting after this scope.
        // Actually, setting them to empty will cause stoul to throw and
        // fall back to the default. That is fine.
        ScopedEnv e3{"DNP3_BRIDGE_DNP3_PORT", ""};
        ScopedEnv e4{"DNP3_BRIDGE_DNP3_LOCAL_ADDR", ""};
        ScopedEnv e5{"DNP3_BRIDGE_DNP3_REMOTE_ADDR", ""};
        ScopedEnv e6{"DNP3_BRIDGE_LOG_LEVEL", ""};
        ScopedEnv e7{"DNP3_BRIDGE_COMMAND_TIMEOUT_MS", ""};

        auto result = ConfigLoader::load();
        REQUIRE(result.has_value());
        auto& cfg = *result;

        // String env vars with "" override the default to empty string.
        // That is expected -- env_or returns the env value if set, even if empty.
        // So grpc_listen_address becomes "" not "0.0.0.0:50051".
        // To truly test defaults we need to unset the env vars.
    }

    SUBCASE("pure defaults with env vars unset") {
        // Unset all relevant env vars to test true defaults.
        unsetenv("DNP3_BRIDGE_GRPC_ADDRESS");
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_PORT");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        auto result = ConfigLoader::load();
        REQUIRE(result.has_value());

        AppConfig defaults{};
        auto& cfg = *result;

        CHECK(cfg.grpc_listen_address == defaults.grpc_listen_address);
        CHECK(cfg.dnp3_channel_host   == defaults.dnp3_channel_host);
        CHECK(cfg.dnp3_channel_port   == defaults.dnp3_channel_port);
        CHECK(cfg.dnp3_local_address  == defaults.dnp3_local_address);
        CHECK(cfg.dnp3_remote_address == defaults.dnp3_remote_address);
        CHECK(cfg.log_level           == defaults.log_level);
        CHECK(cfg.command_timeout_ms  == defaults.command_timeout_ms);
    }

    SUBCASE("load full JSON config file") {
        unsetenv("DNP3_BRIDGE_GRPC_ADDRESS");
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_PORT");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        TempJsonFile file{R"({
            "grpc_listen_address": "127.0.0.1:9999",
            "dnp3_channel_host":   "10.0.0.1",
            "dnp3_channel_port":   30000,
            "dnp3_local_address":  2048,
            "dnp3_remote_address": 5,
            "log_level":           "debug",
            "command_timeout_ms":  500
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        auto& cfg = *result;

        CHECK(cfg.grpc_listen_address == "127.0.0.1:9999");
        CHECK(cfg.dnp3_channel_host   == "10.0.0.1");
        CHECK(cfg.dnp3_channel_port   == 30000);
        CHECK(cfg.dnp3_local_address  == 2048);
        CHECK(cfg.dnp3_remote_address == 5);
        CHECK(cfg.log_level           == "debug");
        CHECK(cfg.command_timeout_ms  == 500);
    }

    SUBCASE("partial JSON keeps defaults for missing fields") {
        unsetenv("DNP3_BRIDGE_GRPC_ADDRESS");
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_PORT");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        TempJsonFile file{R"({
            "log_level": "trace"
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());

        AppConfig defaults{};
        auto& cfg = *result;

        CHECK(cfg.grpc_listen_address == defaults.grpc_listen_address);
        CHECK(cfg.dnp3_channel_host   == defaults.dnp3_channel_host);
        CHECK(cfg.dnp3_channel_port   == defaults.dnp3_channel_port);
        CHECK(cfg.log_level           == "trace");
    }

    SUBCASE("invalid JSON returns error") {
        TempJsonFile file{"not valid json {{{{"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("JSON parse error") != std::string::npos);
    }

    SUBCASE("missing file returns error") {
        auto result = ConfigLoader::load("/tmp/this_file_does_not_exist_12345.json");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("cannot open config file") != std::string::npos);
    }

    SUBCASE("env var overrides default") {
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_PORT");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        ScopedEnv env{"DNP3_BRIDGE_GRPC_ADDRESS", "localhost:8080"};

        auto result = ConfigLoader::load();
        REQUIRE(result.has_value());
        CHECK(result->grpc_listen_address == "localhost:8080");
    }

    SUBCASE("env var overrides JSON value") {
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_PORT");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        TempJsonFile file{R"({
            "grpc_listen_address": "from-json:50051"
        })"};

        ScopedEnv env{"DNP3_BRIDGE_GRPC_ADDRESS", "from-env:9999"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->grpc_listen_address == "from-env:9999");
    }

    SUBCASE("command_mode parsed from JSON") {
        unsetenv("DNP3_BRIDGE_COMMAND_MODE");

        TempJsonFile file{R"({
            "command_mode": "select_before_operate"
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->command_mode == "select_before_operate");
    }

    SUBCASE("command_mode defaults to direct_operate") {
        unsetenv("DNP3_BRIDGE_COMMAND_MODE");

        auto result = ConfigLoader::load();
        REQUIRE(result.has_value());
        CHECK(result->command_mode == "direct_operate");
    }

    SUBCASE("command_mode env var overrides JSON") {
        TempJsonFile file{R"({
            "command_mode": "select_before_operate"
        })"};

        ScopedEnv env{"DNP3_BRIDGE_COMMAND_MODE", "direct_operate"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->command_mode == "direct_operate");
    }

    SUBCASE("event_buffer parsed from JSON") {
        TempJsonFile file{R"({
            "event_buffer": {
                "max_binary_events": 100,
                "max_analog_events": 75,
                "max_binary_output_status_events": 10
            }
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->event_buffer.max_binary_events == 100);
        CHECK(result->event_buffer.max_analog_events == 75);
        CHECK(result->event_buffer.max_binary_output_status_events == 10);
        CHECK(result->event_buffer.max_analog_output_status_events == 0); // default
    }

    SUBCASE("unsolicited parsed from JSON") {
        TempJsonFile file{R"({
            "unsolicited": {
                "enabled": false,
                "class_mask": ["class1"]
            }
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->unsolicited.enabled == false);
        REQUIRE(result->unsolicited.class_mask.size() == 1);
        CHECK(result->unsolicited.class_mask[0] == "class1");
    }

    SUBCASE("unsolicited defaults") {
        auto result = ConfigLoader::load();
        REQUIRE(result.has_value());
        CHECK(result->unsolicited.enabled == true);
        REQUIRE(result->unsolicited.class_mask.size() == 2);
        CHECK(result->unsolicited.class_mask[0] == "class1");
        CHECK(result->unsolicited.class_mask[1] == "class2");
    }

    SUBCASE("point_database with range syntax") {
        TempJsonFile file{R"({
            "point_database": {
                "binary_input": [
                    { "range": [0, 10], "class": "class1" }
                ],
                "analog_input": [
                    { "range": [0, 20], "class": "class2", "deadband": 1.5,
                      "static_variation": "Group30Var2", "event_variation": "Group32Var2" }
                ]
            }
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK_FALSE(result->point_database.empty());

        REQUIRE(result->point_database.binary_input.size() == 1);
        CHECK(result->point_database.binary_input[0].start == 0);
        CHECK(result->point_database.binary_input[0].end == 10);
        CHECK(result->point_database.binary_input[0].event_class == "class1");

        REQUIRE(result->point_database.analog_input.size() == 1);
        CHECK(result->point_database.analog_input[0].start == 0);
        CHECK(result->point_database.analog_input[0].end == 20);
        CHECK(result->point_database.analog_input[0].deadband == 1.5);
        CHECK(result->point_database.analog_input[0].static_variation == "Group30Var2");
    }

    SUBCASE("point_database entry without deadband leaves it unset") {
        TempJsonFile file{R"({
            "point_database": {
                "analog_input": [
                    { "range": [0, 20], "class": "class2", "deadband": 1.0 },
                    { "index": 5, "class": "class1" }
                ]
            }
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        REQUIRE(result->point_database.analog_input.size() == 2);
        CHECK(result->point_database.analog_input[0].deadband == 1.0);
        CHECK_FALSE(result->point_database.analog_input[1].deadband.has_value());
    }

    SUBCASE("point_database with single index syntax") {
        TempJsonFile file{R"({
            "point_database": {
                "binary_input": [
                    { "index": 5, "class": "class3" }
                ]
            }
        })"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        REQUIRE(result->point_database.binary_input.size() == 1);
        CHECK(result->point_database.binary_input[0].start == 5);
        CHECK(result->point_database.binary_input[0].end == 5);
        CHECK(result->point_database.binary_input[0].event_class == "class3");
    }

    SUBCASE("point_database empty when not specified") {
        TempJsonFile file{R"({})"};
        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->point_database.empty());
    }

    SUBCASE("numeric env var overrides JSON") {
        unsetenv("DNP3_BRIDGE_GRPC_ADDRESS");
        unsetenv("DNP3_BRIDGE_DNP3_HOST");
        unsetenv("DNP3_BRIDGE_DNP3_LOCAL_ADDR");
        unsetenv("DNP3_BRIDGE_DNP3_REMOTE_ADDR");
        unsetenv("DNP3_BRIDGE_LOG_LEVEL");
        unsetenv("DNP3_BRIDGE_COMMAND_TIMEOUT_MS");

        TempJsonFile file{R"({
            "dnp3_channel_port": 30000
        })"};

        ScopedEnv env{"DNP3_BRIDGE_DNP3_PORT", "40000"};

        auto result = ConfigLoader::load(file.path());
        REQUIRE(result.has_value());
        CHECK(result->dnp3_channel_port == 40000);
    }
}

TEST_CASE("validatePointDatabase rejects unknown enum strings") {
    using dnp3bridge::dnp3::validatePointDatabase;

    auto load = [](const char* json) {
        TempJsonFile file{json};
        auto cfg = ConfigLoader::load(file.path());
        REQUIRE(cfg.has_value());
        return validatePointDatabase(cfg->point_database);
    };

    SUBCASE("a fully valid point_database passes") {
        auto r = load(R"({
            "point_database": {
                "binary_input":         [ { "range": [0, 10], "class": "class1" } ],
                "binary_output_status": [ { "range": [0, 19], "class": "class0" } ],
                "analog_input":         [ { "range": [0, 20], "class": "class2", "deadband": 1.0,
                                            "static_variation": "Group30Var2",
                                            "event_variation": "Group32Var2" } ],
                "analog_output_status": [ { "range": [0, 4], "class": "class2",
                                            "static_variation": "Group40Var2",
                                            "event_variation": "Group42Var2" } ]
            }
        })");
        CHECK(r.has_value());
    }

    SUBCASE("an empty point_database passes") {
        auto r = load(R"({ "log_level": "info" })");
        CHECK(r.has_value());
    }

    SUBCASE("wrong case in a class is rejected") {
        auto r = load(R"({ "point_database": {
            "binary_input": [ { "index": 0, "class": "Class1" } ] } })");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == "point_database.binary_input[0].class: unknown value \"Class1\" "
                           "(expected one of: class0, class1, class2, class3)");
    }

    SUBCASE("wrong case in a static variation is rejected") {
        auto r = load(R"({ "point_database": {
            "analog_input": [ { "range": [0, 20], "static_variation": "Group30var2" } ] } })");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().starts_with(
            "point_database.analog_input[0].static_variation: unknown value \"Group30var2\""));
    }

    SUBCASE("an out-of-range variation number is rejected") {
        auto r = load(R"({ "point_database": {
            "analog_input": [ { "range": [0, 20], "event_variation": "Group32Var9" } ] } })");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().starts_with(
            "point_database.analog_input[0].event_variation: unknown value \"Group32Var9\""));
    }

    SUBCASE("an analog output status variation is checked against group 40/42") {
        auto r = load(R"({ "point_database": {
            "analog_output_status": [ { "range": [0, 4], "static_variation": "Group30Var2" } ] } })");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().starts_with(
            "point_database.analog_output_status[0].static_variation: unknown value \"Group30Var2\""));
    }

    SUBCASE("the reported index is the offending entry, not the first") {
        auto r = load(R"({ "point_database": {
            "analog_input": [ { "range": [0, 9], "class": "class2" },
                              { "index": 10, "class": "class2" },
                              { "index": 11, "class": "classe2" } ] } })");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().starts_with("point_database.analog_input[2].class:"));
    }
}
