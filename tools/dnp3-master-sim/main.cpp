#include "DataModel.hpp"
#include "PointNames.hpp"
#include "SoeHandler.hpp"
#include "Ui.hpp"

#include <opendnp3/DNP3Manager.h>
#include <opendnp3/channel/ChannelRetry.h>
#include <opendnp3/channel/IChannelListener.h>
#include <opendnp3/channel/IPEndpoint.h>
#include <opendnp3/logging/LogLevels.h>
#include <opendnp3/master/DefaultMasterApplication.h>
#include <opendnp3/master/MasterStackConfig.h>

#include <ftxui/component/screen_interactive.hpp>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

using namespace opendnp3;

struct Args {
    std::string host = "127.0.0.1";
    uint16_t port = 20000;
    uint16_t localAddr = 1;
    uint16_t remoteAddr = 1024;
};

static Args parseArgs(int argc, char* argv[])
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) a.host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) a.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--local-addr" && i + 1 < argc) a.localAddr = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--remote-addr" && i + 1 < argc) a.remoteAddr = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: dnp3-master-sim [options]\n"
                      << "  --host <ip>          Outstation host (default: 127.0.0.1)\n"
                      << "  --port <port>        Outstation port (default: 20000)\n"
                      << "  --local-addr <addr>  DNP3 master address (default: 1)\n"
                      << "  --remote-addr <addr> DNP3 outstation address (default: 1024)\n";
            std::exit(0);
        }
    }
    return a;
}

/// Channel listener that updates the DataModel connection state.
class ChannelListener final : public IChannelListener
{
public:
    ChannelListener(dnp3sim::DataModel& model, std::function<void()> notify)
        : model_(model), notify_(std::move(notify))
    {}

    void OnStateChange(ChannelState state) override
    {
        {
            std::lock_guard lock(model_.mutex);
            model_.connected = (state == ChannelState::OPEN);
            model_.addLog(std::string("Channel: ") + ChannelStateSpec::to_human_string(state));
        }
        notify_();
    }

private:
    dnp3sim::DataModel& model_;
    std::function<void()> notify_;
};

int main(int argc, char* argv[])
{
    auto args = parseArgs(argc, argv);

    dnp3sim::DataModel model;

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto notify = [&screen] { screen.PostEvent(ftxui::Event::Custom); };

    // Declared after screen so that implicit Shutdown() in destructor
    // runs before screen is destroyed (stack LIFO destruction order).
    DNP3Manager manager(1);

    auto channelListener = std::make_shared<ChannelListener>(model, notify);

    auto channel = manager.AddTCPClient(
        "master-sim",
        levels::NORMAL,
        ChannelRetry::Default(),
        {IPEndpoint(args.host, args.port)},
        "0.0.0.0",
        channelListener
    );

    MasterStackConfig config;
    config.master.disableUnsolOnStartup = false;
    config.master.startupIntegrityClassMask = ClassField::AllClasses();
    config.link.LocalAddr = args.localAddr;
    config.link.RemoteAddr = args.remoteAddr;

    auto soeHandler = dnp3sim::SoeHandler::Create(model, notify);
    auto master = channel->AddMaster(
        "master-sim",
        soeHandler,
        DefaultMasterApplication::Create(),
        config
    );

    master->Enable();

    auto root = dnp3sim::buildRoot(model, master, soeHandler, screen);
    screen.Loop(root);

    manager.Shutdown();
    return 0;
}
