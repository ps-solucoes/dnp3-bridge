#include "dnp3/OutstationManager.hpp"
#include "dnp3/ForwardingCommandHandler.hpp"

#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/DefaultOutstationApplication.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>

#include <spdlog/spdlog.h>

#include <unordered_map>

namespace dnp3bridge::dnp3 {

namespace {

opendnp3::PointClass toPointClass(const std::string& s) {
    static const std::unordered_map<std::string, opendnp3::PointClass> map = {
        {"class0", opendnp3::PointClass::Class0},
        {"class1", opendnp3::PointClass::Class1},
        {"class2", opendnp3::PointClass::Class2},
        {"class3", opendnp3::PointClass::Class3},
    };
    auto it = map.find(s);
    return it != map.end() ? it->second : opendnp3::PointClass::Class1;
}

opendnp3::StaticAnalogVariation toStaticAnalogVariation(const std::string& s) {
    static const std::unordered_map<std::string, opendnp3::StaticAnalogVariation> map = {
        {"Group30Var1", opendnp3::StaticAnalogVariation::Group30Var1},
        {"Group30Var2", opendnp3::StaticAnalogVariation::Group30Var2},
        {"Group30Var3", opendnp3::StaticAnalogVariation::Group30Var3},
        {"Group30Var4", opendnp3::StaticAnalogVariation::Group30Var4},
        {"Group30Var5", opendnp3::StaticAnalogVariation::Group30Var5},
        {"Group30Var6", opendnp3::StaticAnalogVariation::Group30Var6},
    };
    auto it = map.find(s);
    return it != map.end() ? it->second : opendnp3::StaticAnalogVariation::Group30Var1;
}

opendnp3::EventAnalogVariation toEventAnalogVariation(const std::string& s) {
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
    auto it = map.find(s);
    return it != map.end() ? it->second : opendnp3::EventAnalogVariation::Group32Var1;
}

opendnp3::StaticAnalogOutputStatusVariation toStaticAOVariation(const std::string& s) {
    static const std::unordered_map<std::string, opendnp3::StaticAnalogOutputStatusVariation> map = {
        {"Group40Var1", opendnp3::StaticAnalogOutputStatusVariation::Group40Var1},
        {"Group40Var2", opendnp3::StaticAnalogOutputStatusVariation::Group40Var2},
        {"Group40Var3", opendnp3::StaticAnalogOutputStatusVariation::Group40Var3},
        {"Group40Var4", opendnp3::StaticAnalogOutputStatusVariation::Group40Var4},
    };
    auto it = map.find(s);
    return it != map.end() ? it->second : opendnp3::StaticAnalogOutputStatusVariation::Group40Var1;
}

opendnp3::EventAnalogOutputStatusVariation toEventAOVariation(const std::string& s) {
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
    auto it = map.find(s);
    return it != map.end() ? it->second : opendnp3::EventAnalogOutputStatusVariation::Group42Var1;
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
        for (uint16_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.binary_input[i];
            if (!pc.event_class.empty()) pt.clazz = toPointClass(pc.event_class);
        }
    }

    for (const auto& pc : pt_cfg.binary_output_status) {
        for (uint16_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.binary_output_status[i];
            if (!pc.event_class.empty()) pt.clazz = toPointClass(pc.event_class);
        }
    }

    for (const auto& pc : pt_cfg.analog_input) {
        for (uint16_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.analog_input[i];
            if (!pc.event_class.empty()) pt.clazz = toPointClass(pc.event_class);
            pt.deadband = pc.deadband;
            if (!pc.static_variation.empty()) pt.svariation = toStaticAnalogVariation(pc.static_variation);
            if (!pc.event_variation.empty())  pt.evariation = toEventAnalogVariation(pc.event_variation);
        }
    }

    for (const auto& pc : pt_cfg.analog_output_status) {
        for (uint16_t i = pc.start; i <= pc.end; ++i) {
            auto& pt = db_config.analog_output_status[i];
            if (!pc.event_class.empty()) pt.clazz = toPointClass(pc.event_class);
            pt.deadband = pc.deadband;
            if (!pc.static_variation.empty()) pt.svariation = toStaticAOVariation(pc.static_variation);
            if (!pc.event_variation.empty())  pt.evariation = toEventAOVariation(pc.event_variation);
        }
    }

    spdlog::debug("Outstation database configured from JSON config");
}

} // anonymous namespace

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
        configureDefaultDatabase(db_config);
    } else {
        configureDatabaseFromConfig(db_config, cfg_.point_database);
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

void OutstationManager::updateAnalog(std::uint16_t index, double value) {
    if (!outstation_) return;
    spdlog::trace("Update analog[{}] = {}", index, value);
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Analog(value), index);
    outstation_->Apply(builder.Build());
}

void OutstationManager::updateBinary(std::uint16_t index, bool value) {
    if (!outstation_) return;
    spdlog::trace("Update binary[{}] = {}", index, value);
    opendnp3::UpdateBuilder builder;
    builder.Update(opendnp3::Binary(value), index);
    outstation_->Apply(builder.Build());
}

bool OutstationManager::isConnected() const {
    return connected_.load(std::memory_order_relaxed);
}

} // namespace dnp3bridge::dnp3
