#include "dnp3/OutstationManager.hpp"
#include "dnp3/ForwardingCommandHandler.hpp"

#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/gen/EventMode.h>
#include <opendnp3/outstation/UpdateBuilder.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dnp3bridge::dnp3 {

namespace {

template <typename E>
const std::unordered_map<std::string, E>& nameMap();

template <>
const std::unordered_map<std::string, opendnp3::PointClass>& nameMap() {
    static const std::unordered_map<std::string, opendnp3::PointClass> map = {
        {"class0", opendnp3::PointClass::Class0},
        {"class1", opendnp3::PointClass::Class1},
        {"class2", opendnp3::PointClass::Class2},
        {"class3", opendnp3::PointClass::Class3},
    };
    return map;
}

template <>
const std::unordered_map<std::string, opendnp3::StaticAnalogVariation>& nameMap() {
    static const std::unordered_map<std::string, opendnp3::StaticAnalogVariation> map = {
        {"Group30Var1", opendnp3::StaticAnalogVariation::Group30Var1},
        {"Group30Var2", opendnp3::StaticAnalogVariation::Group30Var2},
        {"Group30Var3", opendnp3::StaticAnalogVariation::Group30Var3},
        {"Group30Var4", opendnp3::StaticAnalogVariation::Group30Var4},
        {"Group30Var5", opendnp3::StaticAnalogVariation::Group30Var5},
        {"Group30Var6", opendnp3::StaticAnalogVariation::Group30Var6},
    };
    return map;
}

template <>
const std::unordered_map<std::string, opendnp3::EventAnalogVariation>& nameMap() {
    static const std::unordered_map<std::string, opendnp3::EventAnalogVariation> map = {
        {"Group32Var1", opendnp3::EventAnalogVariation::Group32Var1},
        {"Group32Var2", opendnp3::EventAnalogVariation::Group32Var2},
        {"Group32Var3", opendnp3::EventAnalogVariation::Group32Var3},
        {"Group32Var4", opendnp3::EventAnalogVariation::Group32Var4},
        {"Group32Var5", opendnp3::EventAnalogVariation::Group32Var5},
        {"Group32Var6", opendnp3::EventAnalogVariation::Group32Var6},
        {"Group32Var7", opendnp3::EventAnalogVariation::Group32Var7},
        {"Group32Var8", opendnp3::EventAnalogVariation::Group32Var8},
    };
    return map;
}

template <>
const std::unordered_map<std::string, opendnp3::StaticAnalogOutputStatusVariation>& nameMap() {
    static const std::unordered_map<std::string, opendnp3::StaticAnalogOutputStatusVariation> map = {
        {"Group40Var1", opendnp3::StaticAnalogOutputStatusVariation::Group40Var1},
        {"Group40Var2", opendnp3::StaticAnalogOutputStatusVariation::Group40Var2},
        {"Group40Var3", opendnp3::StaticAnalogOutputStatusVariation::Group40Var3},
        {"Group40Var4", opendnp3::StaticAnalogOutputStatusVariation::Group40Var4},
    };
    return map;
}

template <>
const std::unordered_map<std::string, opendnp3::EventAnalogOutputStatusVariation>& nameMap() {
    static const std::unordered_map<std::string, opendnp3::EventAnalogOutputStatusVariation> map = {
        {"Group42Var1", opendnp3::EventAnalogOutputStatusVariation::Group42Var1},
        {"Group42Var2", opendnp3::EventAnalogOutputStatusVariation::Group42Var2},
        {"Group42Var3", opendnp3::EventAnalogOutputStatusVariation::Group42Var3},
        {"Group42Var4", opendnp3::EventAnalogOutputStatusVariation::Group42Var4},
        {"Group42Var5", opendnp3::EventAnalogOutputStatusVariation::Group42Var5},
        {"Group42Var6", opendnp3::EventAnalogOutputStatusVariation::Group42Var6},
        {"Group42Var7", opendnp3::EventAnalogOutputStatusVariation::Group42Var7},
        {"Group42Var8", opendnp3::EventAnalogOutputStatusVariation::Group42Var8},
    };
    return map;
}

/// Resolves a configured name. Unknown names are rejected by
/// validatePointDatabase() before start(), so the fallback is unreachable in
/// practice and exists only to keep this total.
template <typename E>
E toEnum(const std::string& s, E fallback) {
    const auto& map = nameMap<E>();
    auto it = map.find(s);
    return it != map.end() ? it->second : fallback;
}

opendnp3::ClassField buildClassMask(const std::vector<std::string>& mask) {
    bool c0 = false, c1 = false, c2 = false, c3 = false;
    for (const auto& s : mask) {
        if (s == "class0") c0 = true;
        else if (s == "class1") c1 = true;
        else if (s == "class2") c2 = true;
        else if (s == "class3") c3 = true;
    }
    return opendnp3::ClassField(c0, c1, c2, c3);
}

void configureDefaultDatabase(opendnp3::DatabaseConfig& db_config) {
    // Binary Input: 11 points (indices 0-10), all Class 1
    for (uint16_t i = 0; i <= 10; ++i) {
        db_config.binary_input[i].clazz = opendnp3::PointClass::Class1;
    }

    // Binary Output Status: 20 points (indices 0-19), all Class 0 (no UR per CEMIG REQ-02)
    for (uint16_t i = 0; i <= 19; ++i) {
        db_config.binary_output_status[i].clazz = opendnp3::PointClass::Class0;
    }

    // Analog Input: 21 points (indices 0-20), all Class 2, 16-bit integer
    for (uint16_t i = 0; i <= 20; ++i) {
        auto& pt = db_config.analog_input[i];
        pt.clazz = opendnp3::PointClass::Class2;
        pt.svariation = opendnp3::StaticAnalogVariation::Group30Var2;
        pt.evariation = opendnp3::EventAnalogVariation::Group32Var2;
    }

    // Analog Output Status: 5 points (indices 0-4), all Class 2, 16-bit integer
    for (uint16_t i = 0; i <= 4; ++i) {
        auto& pt = db_config.analog_output_status[i];
        pt.clazz = opendnp3::PointClass::Class2;
        pt.svariation = opendnp3::StaticAnalogOutputStatusVariation::Group40Var2;
        pt.evariation = opendnp3::EventAnalogOutputStatusVariation::Group42Var2;
    }

    spdlog::debug("Outstation database configured (defaults): 11 BI, 20 BO, 21 AI, 5 AO");
}

void configureDatabaseFromConfig(opendnp3::DatabaseConfig& db_config,
                                  const config::PointDatabaseConfig& pt_cfg) {
    for (const auto& pc : pt_cfg.binary_input) {
        for (std::uint32_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.binary_input[static_cast<std::uint16_t>(i)];
            if (!pc.event_class.empty()) pt.clazz = toEnum(pc.event_class, opendnp3::PointClass::Class1);
        }
    }

    for (const auto& pc : pt_cfg.binary_output_status) {
        for (std::uint32_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.binary_output_status[static_cast<std::uint16_t>(i)];
            if (!pc.event_class.empty()) pt.clazz = toEnum(pc.event_class, opendnp3::PointClass::Class1);
        }
    }

    for (const auto& pc : pt_cfg.analog_input) {
        for (std::uint32_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.analog_input[static_cast<std::uint16_t>(i)];
            if (!pc.event_class.empty()) pt.clazz = toEnum(pc.event_class, opendnp3::PointClass::Class1);
            if (pc.deadband)             pt.deadband = *pc.deadband;
            if (!pc.static_variation.empty()) pt.svariation = toEnum(pc.static_variation, opendnp3::StaticAnalogVariation::Group30Var1);
            if (!pc.event_variation.empty())  pt.evariation = toEnum(pc.event_variation, opendnp3::EventAnalogVariation::Group32Var1);
        }
    }

    for (const auto& pc : pt_cfg.analog_output_status) {
        for (std::uint32_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.analog_output_status[static_cast<std::uint16_t>(i)];
            if (!pc.event_class.empty()) pt.clazz = toEnum(pc.event_class, opendnp3::PointClass::Class1);
            if (pc.deadband)             pt.deadband = *pc.deadband;
            if (!pc.static_variation.empty()) pt.svariation = toEnum(pc.static_variation, opendnp3::StaticAnalogOutputStatusVariation::Group40Var1);
            if (!pc.event_variation.empty())  pt.evariation = toEnum(pc.event_variation, opendnp3::EventAnalogOutputStatusVariation::Group42Var1);
        }
    }

    spdlog::debug("Outstation database configured from JSON config");
}

const char* varName(opendnp3::StaticAnalogVariation v) {
    return opendnp3::StaticAnalogVariationSpec::to_string(v);
}
const char* varName(opendnp3::EventAnalogVariation v) {
    return opendnp3::EventAnalogVariationSpec::to_string(v);
}
const char* varName(opendnp3::StaticAnalogOutputStatusVariation v) {
    return opendnp3::StaticAnalogOutputStatusVariationSpec::to_string(v);
}
const char* varName(opendnp3::EventAnalogOutputStatusVariation v) {
    return opendnp3::EventAnalogOutputStatusVariationSpec::to_string(v);
}

/// Logs the effective config of one measurement type, coalescing consecutive
/// indices that share a description into ranges.
template <typename Points, typename Describe>
void logPointRuns(const char* label, const Points& points, Describe describe) {
    if (points.empty()) return;

    auto it = points.begin();
    std::uint16_t run_start = it->first;
    std::uint16_t prev      = it->first;
    std::string   desc      = describe(it->second);

    auto flush = [&] {
        if (run_start == prev) {
            spdlog::info("  {}[{}] {}", label, run_start, desc);
        } else {
            spdlog::info("  {}[{}..{}] {}", label, run_start, prev, desc);
        }
    };

    for (++it; it != points.end(); ++it) {
        auto next_desc = describe(it->second);
        if (it->first != prev + 1 || next_desc != desc) {
            flush();
            run_start = it->first;
            desc      = std::move(next_desc);
        }
        prev = it->first;
    }
    flush();
}

/// Dumps what opendnp3 actually received, so a misconfigured (or unconfigured)
/// deployment is diagnosable from the logs alone.
void logDatabaseSummary(const opendnp3::DatabaseConfig& db) {
    spdlog::info("DNP3 database: {} BI, {} BO status, {} AI, {} AO status",
                 db.binary_input.size(), db.binary_output_status.size(),
                 db.analog_input.size(), db.analog_output_status.size());

    auto binary_desc = [](const auto& pt) {
        return fmt::format("class={}", opendnp3::PointClassSpec::to_string(pt.clazz));
    };
    auto analog_desc = [](const auto& pt) {
        return fmt::format("class={} deadband={} static={} event={}",
                           opendnp3::PointClassSpec::to_string(pt.clazz),
                           pt.deadband,
                           varName(pt.svariation),
                           varName(pt.evariation));
    };

    logPointRuns("BI", db.binary_input, binary_desc);
    logPointRuns("BO", db.binary_output_status, binary_desc);
    logPointRuns("AI", db.analog_input, analog_desc);
    logPointRuns("AO", db.analog_output_status, analog_desc);
}

/// Maps Python's coarse quality to DNP3 flag bits.
///
/// NOTE: Uncertain maps to plain ONLINE, i.e. it is indistinguishable from Good
/// on the wire. DNP3 has no direct equivalent; the candidates were LOCAL_FORCED
/// and REFERENCE_ERR, and CEMIG chose to leave it as ONLINE. Revisit with the
/// stakeholder if uncertain data needs to be visible at the SCADA.
std::uint8_t toFlagBits(bridge::Quality q) {
    switch (q) {
        case bridge::Quality::Good:      return static_cast<std::uint8_t>(opendnp3::AnalogQuality::ONLINE);
        case bridge::Quality::Uncertain: return static_cast<std::uint8_t>(opendnp3::AnalogQuality::ONLINE);
        case bridge::Quality::Bad:       return 0;  // ONLINE cleared == offline in DNP3
        case bridge::Quality::Restart:   return static_cast<std::uint8_t>(opendnp3::AnalogQuality::RESTART);
    }
    return static_cast<std::uint8_t>(opendnp3::AnalogQuality::ONLINE);
}

/// Builds "unknown value \"x\" (expected one of: a, b, c)" for a bad enum string.
template <typename E>
std::string unknownValue(const std::string& field, const std::string& value) {
    std::vector<std::string> names;
    names.reserve(nameMap<E>().size());
    for (const auto& [name, _] : nameMap<E>()) names.push_back(name);
    std::sort(names.begin(), names.end());

    return fmt::format("{}: unknown value \"{}\" (expected one of: {})",
                       field, value, fmt::join(names, ", "));
}

/// A reversed range silently configures nothing, and every update to those
/// points is then dropped by opendnp3 without a diagnostic.
auto checkRange(const config::PointConfig& pc, const std::string& prefix)
    -> std::expected<void, std::string>
{
    if (pc.start > pc.end) {
        return std::unexpected{fmt::format(
            "{}.range: start {} is greater than end {}", prefix, pc.start, pc.end)};
    }
    return {};
}

/// Checks one optional enum-valued string, if it was set at all.
template <typename E>
auto checkEnum(const std::string& value, const std::string& field)
    -> std::expected<void, std::string>
{
    if (value.empty() || nameMap<E>().contains(value)) return {};
    return std::unexpected{unknownValue<E>(field, value)};
}

/// Validates the `class` of every section, plus the variations of the analog
/// sections (variations on binary entries are not applied, so not checked here).
template <typename StaticVar, typename EventVar>
auto checkAnalogSection(const std::vector<config::PointConfig>& entries, const char* section)
    -> std::expected<void, std::string>
{
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto prefix = fmt::format("point_database.{}[{}]", section, i);
        if (auto r = checkRange(entries[i], prefix); !r) return r;
        if (auto r = checkEnum<opendnp3::PointClass>(entries[i].event_class, prefix + ".class"); !r)
            return r;
        if (auto r = checkEnum<StaticVar>(entries[i].static_variation, prefix + ".static_variation"); !r)
            return r;
        if (auto r = checkEnum<EventVar>(entries[i].event_variation, prefix + ".event_variation"); !r)
            return r;
    }
    return {};
}

auto checkBinarySection(const std::vector<config::PointConfig>& entries, const char* section)
    -> std::expected<void, std::string>
{
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto prefix = fmt::format("point_database.{}[{}]", section, i);
        if (auto r = checkRange(entries[i], prefix); !r) return r;
        if (auto r = checkEnum<opendnp3::PointClass>(entries[i].event_class, prefix + ".class"); !r)
            return r;
    }
    return {};
}

} // anonymous namespace

auto validatePointDatabase(const config::PointDatabaseConfig& db)
    -> std::expected<void, std::string>
{
    if (auto r = checkBinarySection(db.binary_input, "binary_input"); !r) return r;
    if (auto r = checkBinarySection(db.binary_output_status, "binary_output_status"); !r) return r;
    if (auto r = checkAnalogSection<opendnp3::StaticAnalogVariation,
                                    opendnp3::EventAnalogVariation>(
            db.analog_input, "analog_input"); !r) return r;
    if (auto r = checkAnalogSection<opendnp3::StaticAnalogOutputStatusVariation,
                                    opendnp3::EventAnalogOutputStatusVariation>(
            db.analog_output_status, "analog_output_status"); !r) return r;
    return {};
}

OutstationManager::OutstationManager(const config::AppConfig& cfg, CommandDispatcher& dispatcher)
    : cfg_{cfg}
    , dispatcher_{dispatcher}
{}

OutstationManager::~OutstationManager() {
    shutdown();
}

void OutstationManager::start() {
    manager_ = std::make_unique<opendnp3::DNP3Manager>(2);
    spdlog::debug("DNP3Manager created with 2 threads");

    channel_ = manager_->AddTCPServer(
        "server",
        opendnp3::levels::NORMAL,
        opendnp3::ServerAcceptMode::CloseExisting,
        opendnp3::IPEndpoint(cfg_.dnp3_channel_host, cfg_.dnp3_channel_port),
        nullptr
    );
    spdlog::debug("TCP server channel created");

    opendnp3::DatabaseConfig db_config;

    if (cfg_.point_database.empty()) {
        spdlog::warn("No 'point_database' in config -- using built-in defaults; "
                     "all analog deadbands are 0, so every value change generates an event");
        configureDefaultDatabase(db_config);
    } else {
        configureDatabaseFromConfig(db_config, cfg_.point_database);
    }

    logDatabaseSummary(db_config);

    // Keep the effective deadbands: updateAnalog() applies them itself (see
    // PointState) rather than delegating to EventMode::Detect.
    analog_deadband_.clear();
    analog_state_.clear();
    binary_state_.clear();
    for (const auto& [index, pt] : db_config.analog_input) {
        analog_deadband_[index] = pt.deadband;
    }

    const auto& eb = cfg_.event_buffer;
    opendnp3::OutstationStackConfig stack_cfg(std::move(db_config));
    stack_cfg.outstation.eventBufferConfig = opendnp3::EventBufferConfig(
        eb.max_binary_events,
        eb.max_double_binary_events,
        eb.max_analog_events,
        eb.max_counter_events,
        eb.max_frozen_counter_events,
        eb.max_binary_output_status_events,
        eb.max_analog_output_status_events,
        eb.max_octet_string_events
    );

    stack_cfg.outstation.params.allowUnsolicited = cfg_.unsolicited.enabled;
    stack_cfg.outstation.params.unsolClassMask = buildClassMask(cfg_.unsolicited.class_mask);
    stack_cfg.link.LocalAddr  = cfg_.dnp3_local_address;
    stack_cfg.link.RemoteAddr = cfg_.dnp3_remote_address;

    auto cmd_mode = (cfg_.command_mode == "select_before_operate")
        ? CommandMode::SelectBeforeOperate
        : CommandMode::DirectOperate;

    outstation_ = channel_->AddOutstation(
        "outstation",
        ForwardingCommandHandler::Create(dispatcher_, cmd_mode),
        opendnp3::DefaultOutstationApplication::Create(),
        stack_cfg
    );

    outstation_->Enable();
    connected_ = true;

    spdlog::info("Outstation started on {}:{}", cfg_.dnp3_channel_host, cfg_.dnp3_channel_port);
}

void OutstationManager::shutdown() {
    connected_ = false;
    outstation_.reset();
    channel_.reset();
    if (manager_) {
        manager_->Shutdown();
        manager_.reset();
        spdlog::info("Outstation shutdown complete");
    }
}

void OutstationManager::updateAnalog(std::uint16_t index, double value, bridge::Quality quality) {
    if (!outstation_) return;

    const auto flag_bits = toFlagBits(quality);
    auto&      state     = analog_state_[index];

    const auto deadband_it = analog_deadband_.find(index);
    const auto deadband    = deadband_it != analog_deadband_.end() ? deadband_it->second : 0.0;

    // Value alone decides; quality is carried but never triggers. A non-finite
    // value fails every `>` comparison, so guard it explicitly -- otherwise a
    // single NaN latches the point into Suppress for the life of the process.
    const bool is_event = !state.seen
                       || !std::isfinite(value)
                       || !std::isfinite(state.last_evented)
                       || std::fabs(value - state.last_evented) > deadband;

    spdlog::trace("Update analog[{}] = {} flags=0x{:02x}{}",
                  index, value, flag_bits, is_event ? " (event)" : "");

    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Analog(value, opendnp3::Flags(flag_bits)), index,
                   is_event ? opendnp3::EventMode::Force : opendnp3::EventMode::Suppress);
    outstation_->Apply(builder.Build());

    if (is_event) state.last_evented = value;
    state.seen = true;
}

void OutstationManager::updateBinary(std::uint16_t index, bool value, bridge::Quality quality) {
    if (!outstation_) return;

    const auto flag_bits = toFlagBits(quality);
    auto&      state     = binary_state_[index];

    // Binaries have no deadband: every value change is an event (REQ-01).
    const bool is_event = !state.seen || value != state.last_evented;

    spdlog::trace("Update binary[{}] = {} flags=0x{:02x}{}",
                  index, value, flag_bits, is_event ? " (event)" : "");

    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Binary(value, opendnp3::Flags(flag_bits)), index,
                   is_event ? opendnp3::EventMode::Force : opendnp3::EventMode::Suppress);
    outstation_->Apply(builder.Build());

    if (is_event) state.last_evented = value;
    state.seen = true;
}

bool OutstationManager::isConnected() const {
    return connected_.load(std::memory_order_relaxed);
}

} // namespace dnp3bridge::dnp3
