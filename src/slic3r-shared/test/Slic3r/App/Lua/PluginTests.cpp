#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/Log.hpp"
#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/Biz/Lua/LuaEngine.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <variant>

#include <boost/nowide/fstream.hpp>

using namespace Slic3r::App::Lua;
using Catch::Matchers::WithinAbs;

namespace {

/**
 * @brief Parse the metadata of a plugin script the way PluginBundle does: run it, then read info.
 */
PluginMeta parse_meta(const std::string& script)
{
    const Tests::TestTempDir dir;
    const std::string path = (dir.path() / "plugin.lua").string();
    {
        boost::nowide::ofstream file(path);
        file << script;
    }

    Slic3r::Biz::Lua::LuaEngine lua;
    REQUIRE(lua.run_file(path).valid());

    const Plugin::ParseResult plugin = Plugin::parse(lua, "test.", path);
    REQUIRE(plugin.has_value());
    return plugin->meta();
}

const PluginParamDef& param(const PluginMeta& meta, const std::string& name)
{
    const auto it = std::ranges::find(meta.params, name, &PluginParamDef::name);
    REQUIRE(it != meta.params.end());
    return *it;
}

template <typename T>
T default_of(const PluginMeta& meta, const std::string& name)
{
    const std::optional<PluginParamValue>& value = param(meta, name).default_value;
    REQUIRE(value.has_value());
    REQUIRE(std::holds_alternative<T>(*value));
    return std::get<T>(*value);
}

} // namespace

TEST_CASE("Plugin parameter defaults keep their declared type", "[Plugin]")
{
    const PluginMeta meta = parse_meta(R"lua(
        info = {
            id = "defaults",
            type = "project.plugin",
            params = {
                {name = "fraction", type = "float", default = 2.4},
                {name = "whole", type = "float", default = 80},
                {name = "count", type = "int", default = 3},
                {name = "rounded", type = "int", default = 2.6},
                {name = "flag", type = "bool", default = true},
                {name = "text", type = "string", default = "abc"},
                {name = "missing", type = "float"},
            }
        }
        function execute(params) end
    )lua");

    CHECK_THAT(default_of<double>(meta, "fraction"), WithinAbs(2.4, 1e-12));
    CHECK(default_of<double>(meta, "whole") == 80.);
    CHECK(default_of<int>(meta, "count") == 3);
    CHECK(default_of<int>(meta, "rounded") == 3);
    CHECK(default_of<bool>(meta, "flag"));
    CHECK(default_of<std::string>(meta, "text") == "abc");
    CHECK_FALSE(param(meta, "missing").default_value.has_value());
}
