#pragma once

#include <variant>
#include <string>
#include <string_view>
#include <map>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

#include "Slic3r/Biz/Lua/LuaEngine.hpp"

namespace Slic3r::App::Lua {

enum class PluginType
{
    ProjectPlugin
};

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s);
std::string to_string(PluginType type);

using PluginParamValue = std::variant<bool, int, double, std::string>;
using PluginParamValueMap = std::map<std::string, PluginParamValue>;

/**
 * @brief One of the options offered by a "choice" parameter.
 */
struct PluginChoiceOption
{
    /** Value handed to the plugin's execute() when the option is picked. */
    PluginParamValue value;
    /** Text shown in the drop down. */
    std::string label;
};

using PluginChoiceOptions = std::vector<PluginChoiceOption>;

struct PluginParamDef
{
    std::string name;
    std::string label;
    std::string type;
    std::optional<PluginParamValue> default_value;
    /** Tab the parameter is shown on, see param_groups(). */
    std::optional<std::string> group;
    /** Options of a "choice" parameter, in the order they are offered. */
    PluginChoiceOptions options;
};

using PluginParamDefs = std::vector<PluginParamDef>;

/**
 * @brief Titles of the tabs the parameters of a plugin dialog are split into.
 *
 * Groups are listed in the order they first appear in @p params. Parameters without a group are
 * collected under @p ungrouped_title, which then comes first. Returns an empty list when no
 * parameter declares a group, i.e. when there is nothing to split.
 */
std::vector<std::string>
param_groups(const PluginParamDefs& params, const std::string& ungrouped_title);

/**
 * @brief Index of the option of a "choice" parameter that holds @p value.
 *
 * Numbers match by value, no matter whether they are stored as int or double.
 */
std::optional<size_t> find_choice(const PluginParamDef& param, const PluginParamValue& value);

struct PluginMeta
{
    std::string id;
    PluginType type;
    std::optional<std::string> title;
    std::vector<std::string> menu;
    PluginParamDefs params;
};


class Plugin
{
public:
    const PluginMeta& meta() const { return m_meta; }
    PluginMeta& meta() { return m_meta; }
    const std::string& path() const { return m_path; }

    void execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const;

    using ParseResult = tl::expected<Plugin, std::string>;
    static ParseResult
    parse(Biz::Lua::LuaEngine& lua, const std::string& id_prefix, const std::string& path);

private:
    Plugin(std::string  path, PluginMeta  meta);

private:
    std::string m_path;
    PluginMeta m_meta;
};

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
);

}