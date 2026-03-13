#pragma once

#include "DataModel.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <opendnp3/master/IMaster.h>
#include <opendnp3/master/ISOEHandler.h>

#include <memory>

namespace dnp3sim {

ftxui::Component buildRoot(
    DataModel& model,
    std::shared_ptr<opendnp3::IMaster>& master,
    std::shared_ptr<opendnp3::ISOEHandler>& soeHandler,
    ftxui::ScreenInteractive& screen);

} // namespace dnp3sim
