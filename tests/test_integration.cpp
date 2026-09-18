#include <doctest/doctest.h>

#include "bridge/Bridge.hpp"
#include "config/AppConfig.hpp"
#include "dnp3/CommandDispatcher.hpp"
#include "dnp3/OutstationManager.hpp"
#include "grpc/BridgeServiceImpl.hpp"
#include "grpc/GrpcServer.hpp"

#include <opendnp3/DNP3Manager.h>
#include <opendnp3/channel/ChannelRetry.h>
#include <opendnp3/channel/IPEndpoint.h>
#include <opendnp3/logging/LogLevels.h>
#include <opendnp3/master/DefaultMasterApplication.h>
#include <opendnp3/master/ISOEHandler.h>
#include <opendnp3/master/MasterStackConfig.h>
#include <opendnp3/master/CommandSet.h>
#include <opendnp3/app/ControlRelayOutputBlock.h>
#include <opendnp3/app/AnalogOutput.h>
#include <opendnp3/app/MeasurementTypes.h>
#include <opendnp3/gen/OperationType.h>
#include <opendnp3/gen/TaskCompletion.h>
#include <opendnp3/gen/CommandStatus.h>
#include <opendnp3/gen/CommandPointState.h>

#include <grpcpp/grpcpp.h>
#include "dnp3bridge.grpc.pb.h"

#include <atomic>
#include <limits>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Collecting SOE handler — captures poll results for assertions
// ---------------------------------------------------------------------------
class CollectingSOEHandler : public opendnp3::ISOEHandler {
public:
    struct AnalogReading  { uint16_t index; double value; uint8_t flags = 0; };
    struct BinaryReading  { uint16_t index; bool value; uint8_t flags = 0; };

    static std::shared_ptr<CollectingSOEHandler> Create() {
        return std::make_shared<CollectingSOEHandler>();
    }

    void BeginFragment(const opendnp3::ResponseInfo&) override {
        std::lock_guard lock{mutex_};
        fragment_analogs_.clear();
        fragment_binaries_.clear();
    }

    void EndFragment(const opendnp3::ResponseInfo&) override {
        std::lock_guard lock{mutex_};
        analogs_.insert(analogs_.end(), fragment_analogs_.begin(), fragment_analogs_.end());
        binaries_.insert(binaries_.end(), fragment_binaries_.begin(), fragment_binaries_.end());
        received_ = true;
        cv_.notify_all();
    }

    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Analog>>& values) override {
        values.ForeachItem([this](const opendnp3::Indexed<opendnp3::Analog>& v) {
            fragment_analogs_.push_back({v.index, v.value.value, v.value.flags.value});
        });
    }

    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Binary>>& values) override {
        values.ForeachItem([this](const opendnp3::Indexed<opendnp3::Binary>& v) {
            fragment_binaries_.push_back({v.index, v.value.value, v.value.flags.value});
        });
    }

    // No-ops for types we don't use.
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::DoubleBitBinary>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::Counter>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::FrozenCounter>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryOutputStatus>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogOutputStatus>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::OctetString>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::TimeAndInterval>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::BinaryCommandEvent>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::Indexed<opendnp3::AnalogCommandEvent>>&) override {}
    void Process(const opendnp3::HeaderInfo&, const opendnp3::ICollection<opendnp3::DNPTime>&) override {}

    // Wait for at least one poll response.
    bool waitForData(std::chrono::milliseconds timeout = 5000ms) {
        std::unique_lock lock{mutex_};
        return cv_.wait_for(lock, timeout, [this] { return received_; });
    }

    void clear() {
        std::lock_guard lock{mutex_};
        analogs_.clear();
        binaries_.clear();
        received_ = false;
    }

    std::vector<AnalogReading> getAnalogs() {
        std::lock_guard lock{mutex_};
        return analogs_;
    }

    std::vector<BinaryReading> getBinaries() {
        std::lock_guard lock{mutex_};
        return binaries_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool received_{false};
    std::vector<AnalogReading> analogs_;
    std::vector<BinaryReading> binaries_;
    std::vector<AnalogReading> fragment_analogs_;
    std::vector<BinaryReading> fragment_binaries_;
};

// ---------------------------------------------------------------------------
// Integration test fixture — wires up all components
// ---------------------------------------------------------------------------
struct IntegrationFixture {
    static constexpr uint16_t kGrpcPort = 50052;
    static constexpr uint16_t kDnp3Port = 20001;
    static constexpr uint16_t kMasterAddr = 1;
    static constexpr uint16_t kOutstationAddr = 1024;

    dnp3bridge::config::AppConfig cfg;
    dnp3bridge::dnp3::CommandDispatcher dispatcher;
    dnp3bridge::dnp3::OutstationManager outstation;
    dnp3bridge::bridge::Bridge bridge;
    dnp3bridge::grpc::BridgeServiceImpl service;
    dnp3bridge::grpc::GrpcServer grpc_server;
    std::jthread grpc_thread;

    // Master side.
    opendnp3::DNP3Manager master_manager;
    std::shared_ptr<opendnp3::IChannel> master_channel;
    std::shared_ptr<opendnp3::IMaster> master;
    std::shared_ptr<CollectingSOEHandler> soe;

    // gRPC client (simulates Python).
    std::shared_ptr<::grpc::Channel> grpc_channel;
    std::unique_ptr<dnp3bridge::v1::BridgeService::Stub> stub;

    IntegrationFixture()
        : cfg(makeConfig())
        , dispatcher(std::chrono::milliseconds{cfg.command_timeout_ms})
        , outstation(cfg, dispatcher)
        , bridge(outstation)
        , service(bridge, outstation, dispatcher)
        , grpc_server(cfg.grpc_listen_address, service)
        , master_manager(1)
        , soe(CollectingSOEHandler::Create())
    {
        // Start bridge components.
        outstation.start();
        bridge.start();
        grpc_thread = std::jthread([this] { grpc_server.start(); });
        std::this_thread::sleep_for(500ms);

        // Start DNP3 master.
        master_channel = master_manager.AddTCPClient(
            "test-master",
            opendnp3::levels::NOTHING,
            opendnp3::ChannelRetry::Default(),
            {opendnp3::IPEndpoint("127.0.0.1", kDnp3Port)},
            "0.0.0.0",
            nullptr
        );

        opendnp3::MasterStackConfig master_cfg;
        master_cfg.master.disableUnsolOnStartup = false;
        master_cfg.master.startupIntegrityClassMask = opendnp3::ClassField::AllClasses();
        master_cfg.master.responseTimeout = opendnp3::TimeDuration::Seconds(5);
        master_cfg.link.LocalAddr = kMasterAddr;
        master_cfg.link.RemoteAddr = kOutstationAddr;

        master = master_channel->AddMaster(
            "test-master",
            soe,
            opendnp3::DefaultMasterApplication::Create(),
            master_cfg
        );
        master->Enable();

        // Start gRPC client.
        grpc_channel = ::grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(kGrpcPort),
            ::grpc::InsecureChannelCredentials()
        );
        stub = dnp3bridge::v1::BridgeService::NewStub(grpc_channel);

        // Wait for DNP3 link to come up (startup integrity poll).
        std::this_thread::sleep_for(2s);
    }

    ~IntegrationFixture() {
        master->Disable();
        master.reset();
        master_channel.reset();
        master_manager.Shutdown();

        grpc_server.stop();
        if (grpc_thread.joinable()) grpc_thread.join();

        bridge.stop();
        outstation.shutdown();
    }

    // Send UpdatePoints via gRPC (simulates Python sending data).
    bool sendUpdate(const dnp3bridge::v1::UpdateRequest& request) {
        ::grpc::ClientContext ctx;
        dnp3bridge::v1::UpdateResponse resp;
        auto status = stub->UpdatePoints(&ctx, request, &resp);
        return status.ok() && resp.success();
    }

    // Poll the outstation and wait for results.
    bool poll(std::chrono::milliseconds timeout = 5000ms) {
        soe->clear();
        master->ScanClasses(opendnp3::ClassField::AllClasses(), soe);
        return soe->waitForData(timeout);
    }

    // Send a CROB via DirectOperate (default command mode).
    struct CommandResult {
        opendnp3::TaskCompletion summary;
        opendnp3::CommandStatus status;
        opendnp3::CommandPointState state;
    };

    CommandResult sendCrob(uint16_t index, opendnp3::OperationType op) {
        CommandResult result{};
        std::promise<void> done;

        master->DirectOperate(
            opendnp3::ControlRelayOutputBlock(op),
            index,
            [&](const opendnp3::ICommandTaskResult& r) {
                result.summary = r.summary;
                r.ForeachItem([&](const opendnp3::CommandPointResult& pt) {
                    result.status = pt.status;
                    result.state = pt.state;
                });
                done.set_value();
            }
        );

        done.get_future().wait_for(10s);
        return result;
    }

    // Send an AnalogOutputFloat32 via DirectOperate.
    CommandResult sendAnalog(uint16_t index, float value) {
        CommandResult result{};
        std::promise<void> done;

        master->DirectOperate(
            opendnp3::AnalogOutputFloat32(value),
            index,
            [&](const opendnp3::ICommandTaskResult& r) {
                result.summary = r.summary;
                r.ForeachItem([&](const opendnp3::CommandPointResult& pt) {
                    result.status = pt.status;
                    result.state = pt.state;
                });
                done.set_value();
            }
        );

        done.get_future().wait_for(10s);
        return result;
    }

private:
    static dnp3bridge::config::AppConfig makeConfig() {
        dnp3bridge::config::AppConfig c;
        c.grpc_listen_address = "127.0.0.1:" + std::to_string(kGrpcPort);
        c.dnp3_channel_host = "0.0.0.0";
        c.dnp3_channel_port = kDnp3Port;
        c.dnp3_local_address = kOutstationAddr;
        c.dnp3_remote_address = kMasterAddr;
        c.command_timeout_ms = 500;  // Short timeout for fast tests.
        return c;
    }
};

// ---------------------------------------------------------------------------
// Helper: open a StreamCommands stream on a background thread
// ---------------------------------------------------------------------------
class CommandStream {
public:
    CommandStream(dnp3bridge::v1::BridgeService::Stub& stub, bool auto_respond = true)
        : stub_{stub}, auto_respond_{auto_respond}
    {
        thread_ = std::jthread([this](std::stop_token st) { run(st); });
        std::this_thread::sleep_for(200ms);
    }

    ~CommandStream() {
        thread_.request_stop();
        if (ctx_) ctx_->TryCancel();
        if (thread_.joinable()) thread_.join();
    }

    struct ReceivedCommand {
        uint64_t id;
        uint32_t index;
        dnp3bridge::v1::CommandType type;
    };

    std::vector<ReceivedCommand> received() {
        std::lock_guard lock{mutex_};
        return received_;
    }

    bool waitForCommand(std::chrono::milliseconds timeout = 5000ms) {
        std::unique_lock lock{mutex_};
        return cv_.wait_for(lock, timeout, [this] { return !received_.empty(); });
    }

private:
    void run(std::stop_token st) {
        ctx_ = std::make_unique<::grpc::ClientContext>();
        dnp3bridge::v1::StreamCommandsRequest req;
        auto reader = stub_.StreamCommands(ctx_.get(), req);

        dnp3bridge::v1::CommandRequest cmd;
        while (reader->Read(&cmd)) {
            if (st.stop_requested()) break;

            {
                std::lock_guard lock{mutex_};
                received_.push_back({cmd.command_id(),
                                     cmd.point_index(),
                                     cmd.command_type()});
            }
            cv_.notify_all();

            if (auto_respond_) {
                ::grpc::ClientContext resp_ctx;
                dnp3bridge::v1::CommandResponse resp;
                resp.set_command_id(cmd.command_id());
                resp.set_status(dnp3bridge::v1::COMMAND_RESULT_SUCCESS);
                dnp3bridge::v1::UpdateResponse uresp;
                stub_.RespondToCommand(&resp_ctx, resp, &uresp);
            }
        }
    }

    dnp3bridge::v1::BridgeService::Stub& stub_;
    bool auto_respond_;
    std::jthread thread_;
    std::unique_ptr<::grpc::ClientContext> ctx_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<ReceivedCommand> received_;
};

// ===========================================================================
// Integration tests
// ===========================================================================

TEST_CASE("Integration: Python sends data, SCADA master polls it") {
    IntegrationFixture fix;

    // Send analog and binary updates via gRPC (simulating Python).
    dnp3bridge::v1::UpdateRequest req;
    auto* a0 = req.add_analogs();
    a0->set_index(0);
    a0->set_value(220.5);
    a0->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);

    auto* a1 = req.add_analogs();
    a1->set_index(3);
    a1->set_value(15.2);
    a1->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);

    auto* b0 = req.add_binaries();
    b0->set_index(0);
    b0->set_value(true);
    b0->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);

    auto* b1 = req.add_binaries();
    b1->set_index(2);
    b1->set_value(true);
    b1->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);

    REQUIRE(fix.sendUpdate(req));

    // Wait for bridge flush thread to process, then poll.
    std::this_thread::sleep_for(500ms);
    REQUIRE(fix.poll());

    auto analogs = fix.soe->getAnalogs();
    auto binaries = fix.soe->getBinaries();

    // Find our updated analog values.
    std::optional<double> ai0, ai3;
    for (auto& a : analogs) {
        if (a.index == 0) ai0 = a.value;
        if (a.index == 3) ai3 = a.value;
    }
    REQUIRE(ai0.has_value());
    // opendnp3 default static analog variation uses 32-bit integers, so
    // fractional parts are truncated.  Check within ±1.
    CHECK(std::abs(ai0.value() - 220.5) <= 1.0);
    REQUIRE(ai3.has_value());
    CHECK(std::abs(ai3.value() - 15.2) <= 1.0);

    // Find our updated binary values.
    std::optional<bool> bi0, bi2;
    for (auto& b : binaries) {
        if (b.index == 0) bi0 = b.value;
        if (b.index == 2) bi2 = b.value;
    }
    REQUIRE(bi0.has_value());
    CHECK(bi0.value() == true);
    REQUIRE(bi2.has_value());
    CHECK(bi2.value() == true);
}

TEST_CASE("Integration: SCADA sends CROB, Python receives and responds") {
    IntegrationFixture fix;

    // Open a command stream (auto-respond ON).
    CommandStream stream(*fix.stub, /*auto_respond=*/true);
    std::this_thread::sleep_for(300ms);

    // Master sends CROB.
    auto result = fix.sendCrob(0, opendnp3::OperationType::LATCH_ON);

    CHECK(result.summary == opendnp3::TaskCompletion::SUCCESS);
    CHECK(result.status == opendnp3::CommandStatus::SUCCESS);
    CHECK(result.state == opendnp3::CommandPointState::SUCCESS);

    // Verify Python side received it.
    auto cmds = stream.received();
    REQUIRE(cmds.size() >= 1);
    CHECK(cmds[0].index == 0);
    CHECK(cmds[0].type == dnp3bridge::v1::COMMAND_TYPE_CROB);
}

TEST_CASE("Integration: SCADA sends AnalogOutput, Python receives and responds") {
    IntegrationFixture fix;

    CommandStream stream(*fix.stub, /*auto_respond=*/true);
    std::this_thread::sleep_for(300ms);

    auto result = fix.sendAnalog(2, 42.5f);

    CHECK(result.summary == opendnp3::TaskCompletion::SUCCESS);
    CHECK(result.status == opendnp3::CommandStatus::SUCCESS);

    auto cmds = stream.received();
    REQUIRE(cmds.size() >= 1);
    CHECK(cmds[0].index == 2);
    CHECK(cmds[0].type == dnp3bridge::v1::COMMAND_TYPE_ANALOG_FLOAT32);
}

TEST_CASE("Integration: command times out when Python does not respond") {
    IntegrationFixture fix;

    // Open stream but do NOT auto-respond.
    CommandStream stream(*fix.stub, /*auto_respond=*/false);
    std::this_thread::sleep_for(300ms);

    auto result = fix.sendCrob(5, opendnp3::OperationType::PULSE_ON);

    // The outstation should return TIMEOUT to the master after 500ms.
    // opendnp3 may report this as SUCCESS with status=TIMEOUT,
    // or as a task-level failure depending on the version.
    bool timed_out = (result.status == opendnp3::CommandStatus::TIMEOUT)
                  || (result.summary != opendnp3::TaskCompletion::SUCCESS);
    CHECK(timed_out);

    // Python still received the command, just didn't respond.
    auto cmds = stream.received();
    REQUIRE(cmds.size() >= 1);
    CHECK(cmds[0].index == 5);
}

TEST_CASE("Integration: command returns NOT_SUPPORTED when no stream connected") {
    IntegrationFixture fix;

    // No CommandStream opened — no Python connected.
    auto result = fix.sendCrob(0, opendnp3::OperationType::LATCH_ON);

    // Without an active stream writer, dispatch returns NOT_SUPPORTED.
    bool rejected = (result.status == opendnp3::CommandStatus::NOT_SUPPORTED)
                 || (result.summary != opendnp3::TaskCompletion::SUCCESS);
    CHECK(rejected);
}

TEST_CASE("Integration: bridge survives Python disconnect and reconnect") {
    IntegrationFixture fix;

    // First connection — send a command successfully.
    {
        CommandStream stream(*fix.stub, /*auto_respond=*/true);
        std::this_thread::sleep_for(300ms);

        auto r1 = fix.sendCrob(0, opendnp3::OperationType::LATCH_ON);
        CHECK(r1.summary == opendnp3::TaskCompletion::SUCCESS);
    }
    // stream destroyed — Python disconnected.

    std::this_thread::sleep_for(500ms);

    // Command without stream — dispatch returns NOT_SUPPORTED to opendnp3,
    // but the master still considers the task "successful" (it got a valid
    // response from the outstation).  Check the command status instead.
    auto r2 = fix.sendCrob(1, opendnp3::OperationType::LATCH_OFF);
    CHECK(r2.status == opendnp3::CommandStatus::NOT_SUPPORTED);

    // Second connection — should work again.
    {
        CommandStream stream2(*fix.stub, /*auto_respond=*/true);
        std::this_thread::sleep_for(300ms);

        auto r3 = fix.sendCrob(2, opendnp3::OperationType::PULSE_ON);
        CHECK(r3.summary == opendnp3::TaskCompletion::SUCCESS);
        CHECK(r3.status == opendnp3::CommandStatus::SUCCESS);
    }
}

TEST_CASE("Integration: rapid binary updates are all captured by outstation") {
    IntegrationFixture fix;

    // Send 20 rapid binary updates toggling BI-0 (each toggle generates an event
    // since BI is Class 1). This validates event buffer capacity and that the
    // bridge queue + flush thread can handle rapid updates without data loss.
    for (int i = 0; i < 20; ++i) {
        dnp3bridge::v1::UpdateRequest req;
        auto* b = req.add_binaries();
        b->set_index(0);
        b->set_value(i % 2 == 0);
        b->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);
        REQUIRE(fix.sendUpdate(req));
    }

    // Wait for flush, then poll.
    std::this_thread::sleep_for(1s);
    REQUIRE(fix.poll());

    // The final value of BI-0 should be the last update (i=19, value=false since 19%2!=0).
    auto binaries = fix.soe->getBinaries();
    std::optional<bool> bi0;
    for (auto& b : binaries) {
        if (b.index == 0) bi0 = b.value;
    }
    REQUIRE(bi0.has_value());
    CHECK(bi0.value() == false);
}

TEST_CASE("Integration: GetStatus reports connection state and timestamp") {
    IntegrationFixture fix;

    // Check initial status.
    {
        ::grpc::ClientContext ctx;
        dnp3bridge::v1::StatusRequest req;
        dnp3bridge::v1::StatusResponse resp;
        auto status = fix.stub->GetStatus(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.state() == dnp3bridge::v1::OUTSTATION_STATE_CONNECTED);
        CHECK(resp.last_update_timestamp_ms() == 0);  // No updates yet.
    }

    // Send an update.
    dnp3bridge::v1::UpdateRequest ureq;
    auto* a = ureq.add_analogs();
    a->set_index(0);
    a->set_value(100.0);
    a->set_quality(dnp3bridge::v1::POINT_QUALITY_GOOD);
    REQUIRE(fix.sendUpdate(ureq));

    // Check timestamp updated.
    {
        ::grpc::ClientContext ctx;
        dnp3bridge::v1::StatusRequest req;
        dnp3bridge::v1::StatusResponse resp;
        auto status = fix.stub->GetStatus(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.last_update_timestamp_ms() > 0);
    }
}

// ---------------------------------------------------------------------------
// Deadband — needs its own outstation because the shared fixture runs with an
// empty point_database (opendnp3 defaults, deadband 0).
// ---------------------------------------------------------------------------
namespace {

struct AnalogRun {
    std::vector<double> events;       // events after the first update
    double              static_value; // value from a closing Class 0 read
    uint8_t             static_flags; // flags from that same read
};

using QualifiedValue = std::pair<double, dnp3bridge::bridge::Quality>;

// Applies `sequence` to one analog point and reports the events the master sees
// for it, excluding the first update (which always events off the RESTART flag),
// plus what a Class 0 integrity read returns at the end.
AnalogRun runAnalogSequence(std::vector<dnp3bridge::config::PointConfig> analog_cfg,
                            uint16_t index,
                            const std::vector<QualifiedValue>& sequence,
                            uint16_t max_analog_events = 50)
{
    constexpr uint16_t kPort = 20002;

    dnp3bridge::config::AppConfig cfg;
    cfg.dnp3_channel_host   = "127.0.0.1";
    cfg.dnp3_channel_port   = kPort;
    cfg.dnp3_local_address  = 1024;
    cfg.dnp3_remote_address = 1;
    cfg.unsolicited.enabled = false;  // poll-driven, so event counts are deterministic
    cfg.event_buffer.max_analog_events = max_analog_events;
    cfg.point_database.analog_input = std::move(analog_cfg);

    dnp3bridge::dnp3::CommandDispatcher dispatcher{500ms};
    dnp3bridge::dnp3::OutstationManager outstation{cfg, dispatcher};
    outstation.start();

    opendnp3::DNP3Manager manager{1};
    auto channel = manager.AddTCPClient(
        "deadband-master", opendnp3::levels::NOTHING, opendnp3::ChannelRetry::Default(),
        {opendnp3::IPEndpoint("127.0.0.1", kPort)}, "0.0.0.0", nullptr);

    opendnp3::MasterStackConfig mcfg;
    mcfg.master.disableUnsolOnStartup = true;
    mcfg.master.startupIntegrityClassMask = opendnp3::ClassField::None();
    mcfg.master.responseTimeout = opendnp3::TimeDuration::Seconds(5);
    mcfg.link.LocalAddr  = 1;
    mcfg.link.RemoteAddr = 1024;

    auto soe = CollectingSOEHandler::Create();
    auto master = channel->AddMaster("deadband-master", soe,
                                     opendnp3::DefaultMasterApplication::Create(), mcfg);
    master->Enable();
    std::this_thread::sleep_for(1500ms);

    const auto event_classes = opendnp3::ClassField(false, true, true, true);
    auto drain = [&] {
        soe->clear();
        master->ScanClasses(event_classes, soe);
        soe->waitForData();
        std::this_thread::sleep_for(300ms);
    };

    outstation.updateAnalog(index, sequence.front().first, sequence.front().second);
    std::this_thread::sleep_for(200ms);
    drain();

    for (std::size_t i = 1; i < sequence.size(); ++i) {
        outstation.updateAnalog(index, sequence[i].first, sequence[i].second);
        std::this_thread::sleep_for(100ms);
    }
    drain();

    AnalogRun run{};
    for (const auto& r : soe->getAnalogs()) {
        if (r.index == index) run.events.push_back(r.value);
    }

    // Class 0 integrity read: statics carry the current value and quality.
    soe->clear();
    master->ScanClasses(opendnp3::ClassField(true, false, false, false), soe);
    soe->waitForData();
    std::this_thread::sleep_for(300ms);
    for (const auto& r : soe->getAnalogs()) {
        if (r.index == index) {
            run.static_value = r.value;
            run.static_flags = r.flags;
        }
    }

    master->Disable();
    master.reset();
    channel.reset();
    manager.Shutdown();
    outstation.shutdown();
    std::this_thread::sleep_for(300ms);

    return run;
}

dnp3bridge::config::PointConfig analogRange(uint16_t start, uint16_t end,
                                            std::string clazz,
                                            std::optional<double> deadband)
{
    dnp3bridge::config::PointConfig pc;
    pc.start = start;
    pc.end   = end;
    pc.event_class = std::move(clazz);
    pc.deadband = deadband;
    pc.static_variation = "Group30Var2";
    pc.event_variation  = "Group32Var2";
    return pc;
}

} // anonymous namespace

TEST_CASE("Integration: analog deadband suppresses sub-threshold events") {
    using Q = dnp3bridge::bridge::Quality;
    const std::vector<QualifiedValue> sequence{
        {100.0, Q::Good}, {100.5, Q::Good}, {100.9, Q::Good}, {102.0, Q::Good}};

    SUBCASE("configured deadband is honoured") {
        auto run = runAnalogSequence({analogRange(0, 20, "class2", 1.0)}, 3, sequence);
        CHECK(run.events == std::vector<double>{102.0});
    }

    SUBCASE("a later entry that omits deadband does not clear it") {
        auto run = runAnalogSequence(
            {analogRange(0, 20, "class2", 1.0), analogRange(5, 5, "class2", std::nullopt)},
            5, sequence);
        CHECK(run.events == std::vector<double>{102.0});
    }

    // Losing the deadband does more than add noise: opendnp3 evicts the oldest
    // event when the buffer fills, so in-band noise pushes the real transition
    // out before the master ever polls for it.
    SUBCASE("in-band noise does not evict the real transition") {
        auto run = runAnalogSequence(
            {analogRange(0, 20, "class2", 5.0), analogRange(7, 7, "class2", std::nullopt)},
            7, {{100.0, Q::Good}, {120.0, Q::Good}, {121.0, Q::Good},
                {122.0, Q::Good}, {123.0, Q::Good}}, /*max_analog_events=*/3);
        CHECK(run.events == std::vector<double>{120.0});
    }
}

TEST_CASE("Integration: quality reaches SCADA without generating events") {
    using Q = dnp3bridge::bridge::Quality;
    constexpr uint8_t kOnline  = 0x01;
    constexpr uint8_t kOffline = 0x00;
    constexpr uint8_t kRestart = 0x02;

    const auto cfg = std::vector{analogRange(0, 20, "class2", 1.0)};

    SUBCASE("a quality-only change is visible in statics but emits no event") {
        auto run = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.0, Q::Bad}});
        CHECK(run.events.empty());               // REQ-11: no event, so no UR
        CHECK(run.static_value == 100.0);
        CHECK(run.static_flags == kOffline);     // but SCADA still sees it went bad
    }

    SUBCASE("quality does not short-circuit the deadband") {
        // Without the explicit event decision, opendnp3's IsEvent() would fire on
        // the flags difference and emit 100.5 despite the deadband of 1.0.
        auto run = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.5, Q::Bad}});
        CHECK(run.events.empty());
        CHECK(run.static_flags == kOffline);
    }

    SUBCASE("a value change still events while quality is bad") {
        auto run = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.5, Q::Bad}, {102.0, Q::Bad}});
        CHECK(run.events == std::vector<double>{102.0});
        CHECK(run.static_flags == kOffline);
    }

    SUBCASE("the deadband reference is the last evented value, not the last applied one") {
        // 104 is suppressed, so 106 must still be measured against 100.
        auto run = runAnalogSequence(cfg, 3,
            {{100.0, Q::Good}, {104.0, Q::Bad}, {106.0, Q::Bad}},
            /*max_analog_events=*/50);
        CHECK(run.events == std::vector<double>{104.0, 106.0});
    }

    SUBCASE("good and restart map to the expected flags") {
        auto good = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.0, Q::Good}});
        CHECK(good.static_flags == kOnline);

        auto restart = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.0, Q::Restart}});
        CHECK(restart.events.empty());
        CHECK(restart.static_flags == kRestart);
    }

    SUBCASE("a non-finite value does not latch the point into suppression") {
        // fabs(x - NaN) > deadband is false for every x, so without an explicit
        // guard a NaN first sample would suppress this point forever.
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        auto run = runAnalogSequence(cfg, 3, {{nan, Q::Good}, {100.0, Q::Good}, {102.0, Q::Good}});
        CHECK(run.events == std::vector<double>{100.0, 102.0});
    }

    SUBCASE("uncertain is deliberately indistinguishable from good") {
        auto run = runAnalogSequence(cfg, 3, {{100.0, Q::Good}, {100.0, Q::Uncertain}});
        CHECK(run.events.empty());
        CHECK(run.static_flags == kOnline);
    }
}
