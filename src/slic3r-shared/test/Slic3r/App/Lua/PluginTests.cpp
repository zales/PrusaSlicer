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

TEST_CASE("Plugin parameters are split into tabs by group", "[Plugin]")
{
    const auto make_params = [](std::initializer_list<std::optional<std::string>> groups)
    {
        PluginParamDefs params;
        for (const std::optional<std::string>& group : groups) {
            PluginParamDef param;
            param.name  = "p" + std::to_string(params.size());
            param.group = group;
            params.push_back(param);
        }
        return params;
    };

    SECTION("no groups declared leaves nothing to split")
    {
        CHECK(param_groups(make_params({std::nullopt, std::nullopt}), "General").empty());
    }

    SECTION("groups keep the order of their first appearance")
    {
        CHECK(
            param_groups(make_params({"Box", "Lid", "Box", "PCB"}), "General")
            == std::vector<std::string>{"Box", "Lid", "PCB"}
        );
    }

    SECTION("parameters without a group get a leading tab")
    {
        CHECK(
            param_groups(make_params({"Box", std::nullopt, "Lid"}), "General")
            == std::vector<std::string>{"General", "Box", "Lid"}
        );
    }

    SECTION("a group named like the ungrouped tab shares it")
    {
        CHECK(
            param_groups(make_params({"Box", "General", std::nullopt}), "General")
            == std::vector<std::string>{"General", "Box"}
        );
    }
}

TEST_CASE("Plugin parameter group is read and an empty one ignored", "[Plugin]")
{
    const PluginMeta meta = parse_meta(R"lua(
        info = {
            id = "groups",
            type = "project.plugin",
            params = {
                {name = "grouped", type = "int", default = 1, group = "Box"},
                {name = "empty", type = "int", default = 1, group = ""},
                {name = "none", type = "int", default = 1},
            }
        }
        function execute(params) end
    )lua");

    CHECK(param(meta, "grouped").group == "Box");
    CHECK_FALSE(param(meta, "empty").group.has_value());
    CHECK_FALSE(param(meta, "none").group.has_value());
}

TEST_CASE("Plugin choice parameter options", "[Plugin]")
{
    const PluginMeta meta = parse_meta(R"lua(
        info = {
            id = "choices",
            type = "project.plugin",
            params = {
                {name = "screw", type = "choice", default = "M4", values = {"M3", "M4", "M5"}},
                {name = "count", type = "choice", default = 4, values = {1, 2, 4, 2.5}},
                {name = "boss", type = "choice", default = "nut", values = {
                    {value = "selftap", label = "Self-tapping"},
                    {value = "nut"},
                    true,
                    {label = "no value"},
                }},
                {name = "empty", type = "choice", values = {}},
            }
        }
        function execute(params) end
    )lua");

    SECTION("a string is both the value and the label")
    {
        const PluginParamDef& screw = param(meta, "screw");
        REQUIRE(screw.options.size() == 3);
        CHECK(std::get<std::string>(screw.options[1].value) == "M4");
        CHECK(screw.options[1].label == "M4");
        CHECK(find_choice(screw, *screw.default_value) == size_t{1});
    }

    SECTION("numbers keep their type and match whether int or double")
    {
        const PluginParamDef& count = param(meta, "count");
        REQUIRE(count.options.size() == 4);
        CHECK(std::get<int>(count.options[2].value) == 4);
        CHECK(count.options[2].label == "4");
        CHECK(std::get<double>(count.options[3].value) == 2.5);
        CHECK(count.options[3].label == "2.5");
        CHECK(find_choice(count, *count.default_value) == size_t{2});
        CHECK(find_choice(count, PluginParamValue{3}) == std::nullopt);
        CHECK(find_choice(count, PluginParamValue{std::string{"4"}}) == std::nullopt);
    }

    SECTION("a table gives a separate label, unusable entries are skipped")
    {
        const PluginParamDef& boss = param(meta, "boss");
        REQUIRE(boss.options.size() == 2);
        CHECK(std::get<std::string>(boss.options[0].value) == "selftap");
        CHECK(boss.options[0].label == "Self-tapping");
        CHECK(boss.options[1].label == "nut");
        CHECK(find_choice(boss, *boss.default_value) == size_t{1});
    }

    SECTION("a choice without usable options falls back to a text field")
    {
        CHECK(param(meta, "empty").type == "string");
    }
}
