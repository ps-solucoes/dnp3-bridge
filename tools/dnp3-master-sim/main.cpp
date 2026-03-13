#include "PointNames.hpp"
#include "SoeHandler.hpp"

#include <opendnp3/DNP3Manager.h>
#include <opendnp3/channel/ChannelRetry.h>
#include <opendnp3/channel/IPEndpoint.h>
#include <opendnp3/channel/PrintingChannelListener.h>
#include <opendnp3/logging/LogLevels.h>
#include <opendnp3/master/DefaultMasterApplication.h>
#include <opendnp3/master/MasterStackConfig.h>
#include <opendnp3/master/CommandSet.h>
#include <opendnp3/app/ControlRelayOutputBlock.h>
#include <opendnp3/app/AnalogOutput.h>
#include <opendnp3/gen/OperationType.h>
#include <opendnp3/gen/TaskCompletion.h>
#include <opendnp3/gen/CommandPointState.h>
#include <opendnp3/gen/CommandStatus.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>

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

static void printMenu()
{
    std::cout << "\n=== DNP3 Master Simulator ===\n"
              << "  1  Integrity poll (all classes)\n"
              << "  2  Class 1 poll\n"
              << "  3  Class 2 poll\n"
              << "  4  Send CROB (binary output command)\n"
              << "  5  Send Analog Output (float32 setpoint)\n"
              << "  q  Quit\n"
              << "> " << std::flush;
}

static void commandCallback(const ICommandTaskResult& result)
{
    std::cout << "Command result: " << TaskCompletionSpec::to_human_string(result.summary) << "\n";
    result.ForeachItem([](const CommandPointResult& r) {
        std::cout << "  index=" << r.index
                  << " state=" << CommandPointStateSpec::to_human_string(r.state)
                  << " status=" << CommandStatusSpec::to_human_string(r.status) << "\n";
    });
    std::cout << std::flush;
}

static void doCrob(std::shared_ptr<IMaster>& master)
{
    std::cout << "\nBinary Output points:\n";
    for (uint16_t i = 0; i < dnp3sim::kBinaryOutputNames.size(); ++i) {
        std::cout << "  " << i << "  " << dnp3sim::kBinaryOutputNames[i] << "\n";
    }
    std::cout << "Index (0-19): " << std::flush;
    uint16_t index;
    if (!(std::cin >> index) || index >= 20) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid index.\n";
        return;
    }

    std::cout << "Operation type:\n"
              << "  0  NUL\n"
              << "  1  PULSE_ON\n"
              << "  2  PULSE_OFF\n"
              << "  3  LATCH_ON\n"
              << "  4  LATCH_OFF\n"
              << "Choice: " << std::flush;
    int opChoice;
    if (!(std::cin >> opChoice) || opChoice < 0 || opChoice > 4) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid operation.\n";
        return;
    }
    std::cin.ignore(10000, '\n');

    auto opType = static_cast<OperationType>(opChoice);
    ControlRelayOutputBlock crob(opType);

    std::cout << "Sending SelectAndOperate CROB index=" << index
              << " (" << dnp3sim::pointName(dnp3sim::kBinaryOutputNames, index)
              << ") op=" << OperationTypeSpec::to_human_string(opType) << "\n";

    master->SelectAndOperate(crob, index, commandCallback);
}

static void doAnalogOutput(std::shared_ptr<IMaster>& master)
{
    std::cout << "\nAnalog Output points:\n";
    for (uint16_t i = 0; i < dnp3sim::kAnalogOutputNames.size(); ++i) {
        std::cout << "  " << i << "  " << dnp3sim::kAnalogOutputNames[i] << "\n";
    }
    std::cout << "Index (0-4): " << std::flush;
    uint16_t index;
    if (!(std::cin >> index) || index >= 5) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid index.\n";
        return;
    }

    std::cout << "Value (float): " << std::flush;
    float value;
    if (!(std::cin >> value)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid value.\n";
        return;
    }
    std::cin.ignore(10000, '\n');

    std::cout << "Sending DirectOperate AnalogOutputFloat32 index=" << index
              << " (" << dnp3sim::pointName(dnp3sim::kAnalogOutputNames, index)
              << ") value=" << value << "\n";

    master->DirectOperate(AnalogOutputFloat32(value), index, commandCallback);
}

int main(int argc, char* argv[])
{
    auto args = parseArgs(argc, argv);

    std::cout << "DNP3 Master Simulator\n"
              << "Connecting to " << args.host << ":" << args.port
              << " (local=" << args.localAddr << " remote=" << args.remoteAddr << ")\n";

    DNP3Manager manager(1);

    auto channel = manager.AddTCPClient(
        "master-sim",
        levels::NORMAL,
        ChannelRetry::Default(),
        {IPEndpoint(args.host, args.port)},
        "0.0.0.0",
        PrintingChannelListener::Create()
    );

    MasterStackConfig config;
    config.master.disableUnsolOnStartup = false;
    config.master.startupIntegrityClassMask = ClassField::AllClasses();
    config.link.LocalAddr = args.localAddr;
    config.link.RemoteAddr = args.remoteAddr;

    auto soeHandler = dnp3sim::SoeHandler::Create();
    auto master = channel->AddMaster(
        "master-sim",
        soeHandler,
        DefaultMasterApplication::Create(),
        config
    );

    master->Enable();

    std::cout << "Master enabled. Waiting for connection...\n";

    // Interactive loop.
    bool running = true;
    while (running) {
        printMenu();

        std::string choice;
        if (!std::getline(std::cin, choice)) break;
        if (choice.empty()) continue;

        switch (choice[0]) {
        case '1':
            std::cout << "Sending integrity poll (all classes)...\n";
            master->ScanClasses(ClassField::AllClasses(), soeHandler);
            break;
        case '2':
            std::cout << "Sending Class 1 poll...\n";
            master->ScanClasses(ClassField(ClassField::CLASS_1), soeHandler);
            break;
        case '3':
            std::cout << "Sending Class 2 poll...\n";
            master->ScanClasses(ClassField(ClassField::CLASS_2), soeHandler);
            break;
        case '4':
            doCrob(master);
            break;
        case '5':
            doAnalogOutput(master);
            break;
        case 'q':
        case 'Q':
            running = false;
            break;
        default:
            std::cout << "Unknown option.\n";
            break;
        }

        // Give async operations a moment to complete before reprinting menu.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "Shutting down...\n";
    manager.Shutdown();
    return 0;
}
