#include "Ui.hpp"
#include "PointNames.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

#include <opendnp3/master/CommandSet.h>
#include <opendnp3/master/ICommandTaskResult.h>
#include <opendnp3/master/CommandResultCallbackT.h>
#include <opendnp3/app/ControlRelayOutputBlock.h>
#include <opendnp3/app/AnalogOutput.h>
#include <opendnp3/app/ClassField.h>
#include <opendnp3/gen/OperationType.h>
#include <opendnp3/gen/TaskCompletion.h>
#include <opendnp3/gen/CommandPointState.h>
#include <opendnp3/gen/CommandStatus.h>

#include <chrono>
#include <ctime>
#include <format>

using namespace ftxui;

namespace dnp3sim {

namespace {

std::string formatFlags(uint8_t flags) {
    return std::format("{:02X}", flags);
}

Color flagColor(uint8_t flags) {
    if (flags >= 0x04) return Color::Red;
    if (flags == 0x02) return Color::Yellow;
    return Color::GrayDark;
}

Element buildBinaryTable(const std::string& title,
                         const auto& nameArray,
                         const std::map<uint16_t, PointValue>& points,
                         std::chrono::steady_clock::time_point now) {
    std::vector<std::vector<Element>> rows;
    rows.push_back({text("#") | bold, text("Name") | bold, text("Val") | bold, text("Flg") | bold});

    for (uint16_t i = 0; i < static_cast<uint16_t>(nameArray.size()); ++i) {
        auto it = points.find(i);
        std::string valStr = "---";
        std::string flgStr = "--";
        Decorator valDeco = dim;
        Decorator flgDeco = dim;

        if (it != points.end() && it->second.received) {
            bool on = it->second.value > 0.5;
            valStr = on ? " 1 " : " 0 ";
            valDeco = on ? color(Color::Green) : dim;
            flgStr = formatFlags(it->second.flags);
            flgDeco = color(flagColor(it->second.flags));

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.last_update).count();
            if (elapsed < 1000) valDeco = valDeco | bold;
        }

        rows.push_back({
            text(std::format("{:2d}", i)),
            text(std::string(pointName(nameArray, i))),
            text(valStr) | valDeco,
            text(flgStr) | flgDeco,
        });
    }

    auto table = Table(rows);
    table.SelectAll().Border(EMPTY);
    table.SelectRow(0).Decorate(bold);
    table.SelectRow(0).SeparatorVertical(EMPTY);
    table.SelectRow(0).Border(EMPTY);

    return vbox({
        text(title) | bold | center,
        table.Render() | flex,
    });
}

Element buildAnalogTable(const std::string& title,
                         const auto& nameArray,
                         const std::map<uint16_t, PointValue>& points,
                         std::chrono::steady_clock::time_point now) {
    std::vector<std::vector<Element>> rows;
    rows.push_back({text("#") | bold, text("Name") | bold, text("Value") | bold, text("Flg") | bold});

    for (uint16_t i = 0; i < static_cast<uint16_t>(nameArray.size()); ++i) {
        auto it = points.find(i);
        std::string valStr = "---";
        std::string flgStr = "--";
        Decorator valDeco = dim;
        Decorator flgDeco = dim;

        if (it != points.end() && it->second.received) {
            valStr = std::format("{:>10.0f}", it->second.value);
            valDeco = color(Color::Cyan);
            flgStr = formatFlags(it->second.flags);
            flgDeco = color(flagColor(it->second.flags));

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.last_update).count();
            if (elapsed < 1000) valDeco = valDeco | bold;
        }

        rows.push_back({
            text(std::format("{:2d}", i)),
            text(std::string(pointName(nameArray, i))),
            text(valStr) | valDeco,
            text(flgStr) | flgDeco,
        });
    }

    auto table = Table(rows);
    table.SelectAll().Border(EMPTY);
    table.SelectRow(0).Decorate(bold);

    return vbox({
        text(title) | bold | center,
        table.Render() | flex,
    });
}

std::string wallClockNow() {
    auto now_wall = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now_wall);
    struct tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    return buf;
}

} // anonymous namespace

ftxui::Component buildRoot(
    DataModel& model,
    std::shared_ptr<opendnp3::IMaster> master,
    std::shared_ptr<opendnp3::ISOEHandler> soeHandler,
    ftxui::ScreenInteractive& screen)
{
    // Modal state
    auto show_crob_modal = std::make_shared<bool>(false);
    auto show_analog_modal = std::make_shared<bool>(false);
    auto crob_index = std::make_shared<int>(0);
    auto crob_op = std::make_shared<int>(0);
    auto analog_index = std::make_shared<int>(0);
    auto analog_value_str = std::make_shared<std::string>("0.0");

    auto notify = [&screen] { screen.PostEvent(Event::Custom); };

    auto commandCallback = [&model, notify](const opendnp3::ICommandTaskResult& result) {
        std::string summary = opendnp3::TaskCompletionSpec::to_human_string(result.summary);
        std::string details;
        result.ForeachItem([&](const opendnp3::CommandPointResult& r) {
            details += std::format("  idx={} status={}",
                r.index, opendnp3::CommandStatusSpec::to_human_string(r.status));
        });
        {
            std::lock_guard lock(model.mutex);
            model.addLog(std::format("Command result: {}{}", summary, details));
        }
        notify();
    };

    // --- Main content renderer ---
    auto content = Renderer([&model] {
        std::lock_guard lock(model.mutex);
        auto now = std::chrono::steady_clock::now();

        // Header
        auto conn_text = model.connected
            ? text(" CONNECTED ") | color(Color::Green) | bold
            : text(" DISCONNECTED ") | color(Color::Red) | bold;

        auto poll_info = model.last_poll_type.empty()
            ? text("")
            : text(std::format(" Last: {} at {} ", model.last_poll_type, model.last_poll_time));

        auto header = hbox({
            text(" DNP3 Master Simulator ") | bold,
            separator(),
            conn_text,
            separator(),
            poll_info | dim,
            filler(),
        }) | borderLight;

        // Left column: Binary Inputs + Binary Outputs
        auto bi_table = buildBinaryTable("Binary Inputs", kBinaryInputNames, model.binary_inputs, now);
        auto bo_table = buildBinaryTable("Binary Output Status", kBinaryOutputNames, model.binary_outputs, now);
        auto left = vbox({bi_table, separator(), bo_table}) | flex;

        // Right column: Analog Inputs + Analog Outputs
        auto ai_table = buildAnalogTable("Analog Inputs", kAnalogInputNames, model.analog_inputs, now);
        auto ao_table = buildAnalogTable("Analog Output Status", kAnalogOutputNames, model.analog_outputs, now);
        auto right = vbox({ai_table, separator(), ao_table}) | flex;

        // Tables side by side
        auto tables = hbox({left, separator(), right}) | flex;

        // Event log (last ~8 entries)
        Elements log_lines;
        size_t start = model.event_log.size() > 8 ? model.event_log.size() - 8 : 0;
        for (size_t i = start; i < model.event_log.size(); ++i) {
            auto& entry = model.event_log[i];
            Decorator deco = dim;
            if (entry.text.find("SUCCESS") != std::string::npos
                || entry.text.find("Received") != std::string::npos) {
                deco = color(Color::Green);
            } else if (entry.text.find("TIMEOUT") != std::string::npos
                       || entry.text.find("error") != std::string::npos
                       || entry.text.find("FAIL") != std::string::npos) {
                deco = color(Color::Red);
            }
            log_lines.push_back(text(entry.text) | deco);
        }
        if (log_lines.empty()) {
            log_lines.push_back(text(" (no events)") | dim);
        }
        auto log_box = vbox(log_lines) | borderLight | size(HEIGHT, EQUAL, 10);

        // Action bar
        auto actions = hbox({
            text(" [I]") | bold, text(" Integrity  "),
            text("[1]") | bold, text(" Cl.1  "),
            text("[2]") | bold, text(" Cl.2  "),
            text("[C]") | bold, text(" CROB  "),
            text("[A]") | bold, text(" Analog  "),
            text("[Q]") | bold, text(" Quit"),
        }) | borderLight;

        return vbox({header, tables, log_box, actions});
    });

    // --- CROB modal ---
    auto crob_op_entries = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"NUL", "PULSE_ON", "PULSE_OFF", "LATCH_ON", "LATCH_OFF"});
    auto crob_radiobox = Radiobox(crob_op_entries.get(), crob_op.get());

    auto crob_modal_renderer = Renderer(crob_radiobox,
        [&model, crob_index, crob_op, crob_op_entries, crob_radiobox] {
            std::string pt_name = pointName(kBinaryOutputNames, static_cast<uint16_t>(*crob_index));
            return vbox({
                text(" Send CROB Command ") | bold | center,
                separator(),
                hbox({text(" Point: "), text(std::format("{} - {}", *crob_index, pt_name)) | bold}),
                text(" (Up/Down to change point)") | dim,
                separator(),
                text(" Operation:"),
                crob_radiobox->Render() | border,
                separator(),
                hbox({text(" Enter") | bold, text("=Send  "), text("Esc") | bold, text("=Cancel")}),
            }) | borderHeavy | size(WIDTH, EQUAL, 45) | size(HEIGHT, EQUAL, 16) | clear_under | center;
        });

    constexpr int kMaxCrobIndex = static_cast<int>(kBinaryOutputNames.size()) - 1;

    auto crob_modal_component = CatchEvent(crob_modal_renderer,
        [&model, master, crob_index, crob_op, crob_op_entries, show_crob_modal, commandCallback, notify](Event event) {
            if (event == Event::Escape) {
                *show_crob_modal = false;
                return true;
            }
            if (event == Event::ArrowUp) {
                *crob_index = (*crob_index > 0) ? *crob_index - 1 : kMaxCrobIndex;
                return true;
            }
            if (event == Event::ArrowDown) {
                *crob_index = (*crob_index < kMaxCrobIndex) ? *crob_index + 1 : 0;
                return true;
            }
            if (event == Event::Return) {
                static constexpr opendnp3::OperationType kOpTypes[] = {
                    opendnp3::OperationType::NUL,
                    opendnp3::OperationType::PULSE_ON,
                    opendnp3::OperationType::PULSE_OFF,
                    opendnp3::OperationType::LATCH_ON,
                    opendnp3::OperationType::LATCH_OFF,
                };
                opendnp3::ControlRelayOutputBlock crob(kOpTypes[*crob_op]);
                uint16_t idx = static_cast<uint16_t>(*crob_index);
                {
                    std::lock_guard lock(model.mutex);
                    model.addLog(std::format("Sending CROB idx={} ({}) op={}",
                        idx, pointName(kBinaryOutputNames, idx), (*crob_op_entries)[*crob_op]));
                }
                master->SelectAndOperate(crob, idx, commandCallback);
                *show_crob_modal = false;
                return true;
            }
            return false;
        });

    // --- Analog Output modal ---
    auto analog_input_option = InputOption();
    analog_input_option.multiline = false;
    auto analog_input = Input(analog_value_str.get(), "0", analog_input_option);

    auto analog_modal_renderer = Renderer(analog_input,
        [&model, analog_index, analog_value_str, analog_input] {
            std::string pt_name = pointName(kAnalogOutputNames, static_cast<uint16_t>(*analog_index));
            return vbox({
                text(" Send Analog Output ") | bold | center,
                separator(),
                hbox({text(" Point: "), text(std::format("{} - {}", *analog_index, pt_name)) | bold}),
                text(" (Up/Down to change point)") | dim,
                separator(),
                hbox({text(" Value: "), analog_input->Render() | border | size(WIDTH, EQUAL, 15)}),
                separator(),
                hbox({text(" Enter") | bold, text("=Send  "), text("Esc") | bold, text("=Cancel")}),
            }) | borderHeavy | size(WIDTH, EQUAL, 45) | size(HEIGHT, EQUAL, 12) | clear_under | center;
        });

    constexpr int kMaxAnalogIndex = static_cast<int>(kAnalogOutputNames.size()) - 1;

    auto analog_modal_component = CatchEvent(analog_modal_renderer,
        [&model, master, analog_index, analog_value_str, show_analog_modal, commandCallback, notify](Event event) {
            if (event == Event::Escape) {
                *show_analog_modal = false;
                return true;
            }
            if (event == Event::ArrowUp) {
                *analog_index = (*analog_index > 0) ? *analog_index - 1 : kMaxAnalogIndex;
                return true;
            }
            if (event == Event::ArrowDown) {
                *analog_index = (*analog_index < kMaxAnalogIndex) ? *analog_index + 1 : 0;
                return true;
            }
            if (event == Event::Return) {
                int16_t val = 0;
                try {
                    val = static_cast<int16_t>(std::stoi(*analog_value_str));
                } catch (...) {
                    std::lock_guard lock(model.mutex);
                    model.addLog("Invalid analog value");
                    *show_analog_modal = false;
                    return true;
                }
                uint16_t idx = static_cast<uint16_t>(*analog_index);
                {
                    std::lock_guard lock(model.mutex);
                    model.addLog(std::format("Sending AnalogOutput idx={} ({}) value={}",
                        idx, pointName(kAnalogOutputNames, idx), val));
                }
                master->DirectOperate(opendnp3::AnalogOutputInt16(val), idx, commandCallback);
                *show_analog_modal = false;
                return true;
            }
            return false;
        });

    // --- Tab container for modal overlay ---
    auto tab_index = std::make_shared<int>(0);

    auto main_container = Container::Tab({
        content,
        crob_modal_component,
        analog_modal_component,
    }, tab_index.get());

    // --- Key event handling ---
    auto root = CatchEvent(main_container,
        [&model, master, soeHandler, &screen, tab_index,
         show_crob_modal, show_analog_modal, commandCallback, notify](Event event) {
            // Update tab index based on modal state
            if (*show_crob_modal) {
                *tab_index = 1;
            } else if (*show_analog_modal) {
                *tab_index = 2;
            } else {
                *tab_index = 0;
            }

            // Only handle keys when no modal is open
            if (*show_crob_modal || *show_analog_modal) return false;

            if (event.is_character()) {
                char c = event.character()[0];

                switch (c) {
                case 'i':
                case 'I': {
                    auto ts = wallClockNow();
                    {
                        std::lock_guard lock(model.mutex);
                        model.last_poll_type = "Integrity";
                        model.last_poll_time = ts;
                        model.addLog("Sending integrity poll (all classes)...");
                    }
                    master->ScanClasses(opendnp3::ClassField::AllClasses(), soeHandler);
                    return true;
                }
                case '1': {
                    auto ts = wallClockNow();
                    {
                        std::lock_guard lock(model.mutex);
                        model.last_poll_type = "Class 1";
                        model.last_poll_time = ts;
                        model.addLog("Sending Class 1 poll...");
                    }
                    master->ScanClasses(opendnp3::ClassField(opendnp3::ClassField::CLASS_1), soeHandler);
                    return true;
                }
                case '2': {
                    auto ts = wallClockNow();
                    {
                        std::lock_guard lock(model.mutex);
                        model.last_poll_type = "Class 2";
                        model.last_poll_time = ts;
                        model.addLog("Sending Class 2 poll...");
                    }
                    master->ScanClasses(opendnp3::ClassField(opendnp3::ClassField::CLASS_2), soeHandler);
                    return true;
                }
                case 'c':
                case 'C':
                    *show_crob_modal = true;
                    *tab_index = 1;
                    return true;
                case 'a':
                case 'A':
                    *show_analog_modal = true;
                    *tab_index = 2;
                    return true;
                case 'q':
                case 'Q':
                    screen.Exit();
                    return true;
                }
            }
            return false;
        });

    return root;
}

} // namespace dnp3sim
