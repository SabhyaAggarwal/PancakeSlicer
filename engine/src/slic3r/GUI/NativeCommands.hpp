#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ActionRegistry.hpp"

namespace Slic3r { namespace GUI {

struct NativeCommand
{
    std::string key;
    std::string title;
    std::string group;
    std::string input;
    std::string icon;
    std::function<AppActionRunResult(const std::string& param)> runner;
};

namespace NativeCommands {
const std::vector<NativeCommand>& catalog();

void rebuild_catalog();

AppActionRunResult run(const std::string& key, const std::string& param = {});

std::unique_ptr<AppAction> make_action(const NativeCommand& command);
}

}}
