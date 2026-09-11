#include "Slic3r/App/Lua/Plugin.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <fmt/format.h>
#include <ranges>
#include <spdlog/spdlog.h>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

namespace {
const std::unordered_map<PluginType, std::string> PLUGIN_TYPE_NAMES = {
    {PluginType::ProjectPlugin, "project.plugin"}
};

/**
 * @brief Value of a Lua number: an int when it is whole, a double otherwise.
 */
PluginParamValue number_value(double number)
{
    const double whole = std::round(number);
    if (whole == number && std::abs(whole) <= std::numeric_limits<int>::max()) {
        return static_cast<int>(whole);
    }
    return number;
}

/**
 * @brief Converts one entry of the "values" of a choice parameter, nullopt when it is unusable.
 *
 * An entry is a string, a number, or a table {value = <string or number>, label = <string>}.
 */
std::optional<PluginChoiceOption> choice_option(const sol::object& entry)
{
    sol::object value = entry;
    std::optional<std::string> label;
    if (entry.get_type() == sol::type::table) {
        const sol::table table = entry.as<sol::table>();
        value                  = table.get<sol::object>("value");
        label                  = table.get<std::optional<std::string>>("label");
    }

    PluginChoiceOption option;
    switch (value.get_type()) {
    case sol::type::string:
        option.value = value.as<std::string>();
        option.label = label.value_or(value.as<std::string>());
        break;
    case sol::type::number:
        option.value = number_value(value.as<double>());
        option.label = label.value_or(fmt::format("{}", value.as<double>()));
        break;
    default:
        return std::nullopt;
    }
    return option;
}
}

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s)
{
    auto r = PLUGIN_TYPE_NAMES | std::views::values;
    const auto it = std::ranges::find(r, s);
    if (it == r.end()) {
        return tl::unexpected{fmt::format("Unknown plugin type: {}", s)};
    }
    return it.base()->first;
}

std::string to_string(PluginType type)
{
    const auto it = PLUGIN_TYPE_NAMES.find(type);
    ASSERT(it != PLUGIN_TYPE_NAMES.end());
    return it->second;
}

std::vector<std::string>
param_groups(const PluginParamDefs& params, const std::string& ungrouped_title)
{
    const auto is_grouped = [](const PluginParamDef& param) { return param.group.has_value(); };
    if (std::ranges::none_of(params, is_grouped)) {
        return {};
    }

    std::vector<std::string> groups;
    if (!std::ranges::all_of(params, is_grouped)) {
        groups.push_back(ungrouped_title);
    }
    for (const PluginParamDef& param : params) {
        const std::string& group = param.group.has_value() ? *param.group : ungrouped_title;
        if (std::ranges::find(groups, group) == groups.end()) {
            groups.push_back(group);
        }
    }
    return groups;
}

std::optional<size_t> find_choice(const PluginParamDef& param, const PluginParamValue& value)
{
    const auto matches = [&value](const PluginChoiceOption& option)
    {
        return std::visit(
            []<typename A, typename B>(const A& lhs, const B& rhs) -> bool
            {
                constexpr bool lhs_number = std::is_same_v<A, int> || std::is_same_v<A, double>;
                constexpr bool rhs_number = std::is_same_v<B, int> || std::is_same_v<B, double>;
                if constexpr (lhs_number && rhs_number) {
                    return static_cast<double>(lhs) == static_cast<double>(rhs);
                } else if constexpr (std::is_same_v<A, B>) {
                    return lhs == rhs;
                } else {
                    return false;
                }
            },
            option.value,
            value
        );
    };
    const auto it = std::ranges::find_if(param.options, matches);
    if (it == param.options.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(it - param.options.begin());
}

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
)
{
    boost::system::error_code ec;

    // 1. Canonicalize the root
    fs::path canonical_root = fs::weakly_canonical(sandbox_path, ec);
    if (ec) {
        return false; // Root directory must exist and be accessible
    }

    // 2. Canonicalize the user-provided path
    // Use weakly_canonical to support paths to files that don't exist yet
    fs::path canonical_user = fs::weakly_canonical(tested_path, ec);
    if (ec) {
        return false;
    }

    // 3. Calculate relative path lexically
    // Note: lexically_relative does not touch the disk, so it doesn't take error_code
    fs::path relative = canonical_user.lexically_relative(canonical_root);

    // 4. Validate the result
    // An empty path or one starting with ".." indicates an escape
    if (relative.empty() || *relative.begin() == "..") {
        return false;
    }

    return true;
}

struct SafeFileResolver
{
    fs::path plugin_path;
    std::string operator()(const std::string& path) const
    {
        auto p = plugin_path.parent_path() / path;
        if (!is_secure_path(p)) {
            throw Biz::Lua::LuaException{
                fmt::format(
                    "Plugin '{}' uses insecure path {}",
                    plugin_path.string(),
                    path.c_str()
                ),
                plugin_path.string()
            };
        }
        return p.string();
    }

private:
    bool is_secure_path(const fs::path& user_path) const {
        const fs::path root = plugin_path.parent_path();
        return is_path_in_sandbox(root, user_path);
    }
};

Plugin::ParseResult
Plugin::parse(Biz::Lua::LuaEngine& lua, const std::string& id_prefix, const std::string& path)
{
    auto& state = lua.state();
    if (!state["info"].is<sol::table>()) {
        return tl::unexpected{"Missing info table"};
    }

    sol::table info = state["info"];
    PluginMeta meta;
    meta.id = id_prefix + info["id"].get<std::string>();
    auto type_result = parse_plugin_type(info["type"].get<std::string>());
    if (!type_result.has_value()) {
        return tl::unexpected{type_result.error()};
    }
    meta.type = type_result.value();
    meta.title = info.get<std::optional<std::string>>("title");

    if (meta.type != PluginType::ProjectPlugin) {
        return tl::unexpected{fmt::format("Unsupported plugin type '{}'", to_string(meta.type))};
    }

    if (info["menu"].valid()) {
        std::vector<std::string> menu_items;
        std::string menu = info["menu"];
        for (const auto menu_item : std::views::split(menu, '/')) {
            menu_items.emplace_back(menu_item.begin(), menu_item.end());
        }
        meta.menu = std::move(menu_items);
    }

    if (info["params"].valid()) {
        sol::table args = info["params"];
        args.for_each([&meta](const sol::object&, const sol::table& p)
        {
            auto name = p.get<std::string>("name");
            auto label = p.get_or<std::string>("label", name);
            auto type = p.get<std::string>("type");
            // Convert the default according to the declared type. Letting sol2 choose the
            // variant alternative stores 2.4 as the int 2: its int check accepts any Lua
            // number, and int comes before double in PluginParamValue.
            std::optional<PluginParamValue> value;
            const sol::object default_value = p.get<sol::object>("default");
            switch (default_value.get_type()) {
            case sol::type::boolean:
                value = default_value.as<bool>();
                break;
            case sol::type::number:
                if (type == "int") {
                    value = static_cast<int>(std::lround(default_value.as<double>()));
                } else {
                    value = default_value.as<double>();
                }
                break;
            case sol::type::string:
                value = default_value.as<std::string>();
                break;
            default:
                break;
            }
            std::optional<std::string> group = p.get<std::optional<std::string>>("group");
            if (group.has_value() && group->empty()) {
                group.reset();
            }
            PluginChoiceOptions options;
            if (const auto values = p.get<std::optional<sol::table>>("values")) {
                // indexed rather than for_each, so the options keep the order they are written in
                for (size_t i = 1; i <= values->size(); ++i) {
                    std::optional<PluginChoiceOption> option =
                        choice_option(values->get<sol::object>(i));
                    if (option.has_value()) {
                        options.push_back(std::move(*option));
                    } else {
                        SPDLOG_WARN(
                            "Plugin param '{}': ignoring entry {} of 'values', it is neither a string, "
                            "a number nor {{value = ..., label = ...}}",
                            name,
                            i
                        );
                    }
                }
            }
            if (type == "choice" && options.empty()) {
                SPDLOG_WARN(
                    "Plugin param '{}' is a choice without any usable 'values', showing a text field",
                    name
                );
                type = "string";
            }
            meta.params.emplace_back(name, label, type, value, group, options);
        });
    }

    if (!state["execute"].is<sol::function>()) {
        return tl::unexpected{"Missing execute() function"};
    }

    return Plugin{path, meta};
}

Plugin::Plugin(std::string path, PluginMeta meta) : m_path(std::move(path)), m_meta(std::move(meta))
{}

void Plugin::execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const
{
    SafeFileResolver resolver{m_path};
    lua.set_path_resolver(resolver);

    try {
        lua.run_file(m_path);
    } catch (Biz::Lua::LuaException& e) {
        throw;
    } catch (std::exception& e) {
        throw Biz::Lua::LuaException{e.what(), m_path};
    }

    sol::table opts = lua.state().create_table();
    for (const auto& [name, value] : params) {
        opts[name] = value;
    }
    sol::protected_function fn = lua.state()["execute"];
    if (const sol::protected_function_result ret = fn(opts); !ret.valid()) {
        const sol::error err = ret;
        SPDLOG_ERROR("Error executing script {}: {}", m_path, err.what());
    }

    lua.set_path_resolver(nullptr);

    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

}
