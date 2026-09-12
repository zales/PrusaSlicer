#include "Slic3r/App/Lua/PluginDialog.hpp"

#include "Slic3r/App/Imgui/DoubleSlider.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <algorithm>

namespace Slic3r::App::Lua {

/**
 * @brief The value of the previous run, unless it does not hold a @p T.
 *
 * The values of the previous run are matched to the parameters by name only. When the plugin
 * has changed the type of a parameter in the meantime, its old value is ignored so that the
 * control falls back to the declared default instead of throwing on std::get.
 */
template <typename T>
std::optional<PluginParamValue> init_value_of_type(std::optional<PluginParamValue> init_value)
{
    if (init_value.has_value() && !std::holds_alternative<T>(*init_value)) {
        return std::nullopt;
    }
    return init_value;
}

namespace {

class StringControl : public Details::IParamControl
{
public:
    StringControl(const PluginParamDef& param_def, std::optional<PluginParamValue> init_value) :
        m_param_def(param_def), m_init_value(init_value_of_type<std::string>(std::move(init_value)))
    {}

    PluginParamValue value() const override
    {
        return m_textfield->text();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        std::string val;
        if (m_init_value.has_value()) {
            val = std::get<std::string>(m_init_value.value());
        } else {
            val = std::visit(
                []<typename T>(const T& val) -> std::string
                {
                    if constexpr (std::is_same_v<T, std::string>) {
                        return val;
                    }
                    return "";
                },
                m_param_def.default_value.value_or("")
            );
        }
        m_textfield = parent.emplace_back<Yoga::InputTextField>();
        m_textfield->set_text(val);
        return *m_textfield;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::InputTextField* m_textfield{nullptr};
};

/**
 * @brief Drop down over the options of a "choice" parameter.
 */
class ChoiceControl : public Details::IParamControl
{
public:
    ChoiceControl(const PluginParamDef& param_def, std::optional<PluginParamValue> init_value) :
        m_param_def(param_def),
        m_init_value(std::move(init_value))
    {}

    PluginParamValue value() const override
    {
        return m_param_def.options.at(static_cast<size_t>(m_combo->current_index())).value;
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        std::vector<std::string> labels;
        labels.reserve(m_param_def.options.size());
        for (const PluginChoiceOption& option : m_param_def.options) {
            labels.push_back(option.label);
        }

        // Naming the combo box also picks its string constructor over the initializer_list one.
        // Yoga::Object names must not contain '_', it appends its own "_<n>" to keep them unique.
        std::string name = m_param_def.name;
        std::ranges::replace(name, '_', '-');
        m_combo = parent.emplace_back<Yoga::ComboBox>(name);
        m_combo->set_items(labels);

        // the value of the previous run wins over the declared default, an unknown value selects
        // the first option
        const std::optional<PluginParamValue>& selected =
            m_init_value.has_value() ? m_init_value : m_param_def.default_value;
        const std::optional<size_t> index =
            selected.has_value() ? find_choice(m_param_def, *selected) : std::nullopt;
        m_combo->set_current_index(static_cast<int>(index.value_or(0)));
        return *m_combo;
    }

private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::ComboBox* m_combo{nullptr};
};

class BoolControl : public Details::IParamControl
{
public:
    BoolControl(const PluginParamDef& param_def, std::optional<PluginParamValue> init_value) :
        m_param_def(param_def),
        m_init_value(init_value_of_type<bool>(std::move(init_value)))
    {}

    PluginParamValue value() const override
    {
        return m_toggle_button->checked();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        m_toggle_button = parent.emplace_back<Yoga::ToggleButton>(m_param_def.label);
        bool val{false};
        if (m_init_value.has_value()) {
            val = std::get<bool>(m_init_value.value());
        } else {
            val = std::visit(
                []<typename T>(const T& val) -> bool
                {
                    if constexpr (std::is_same_v<T, bool>) {
                        return val;
                    }
                    return false;
                },
                m_param_def.default_value.value_or(false)
            );
        }
        m_toggle_button->set_checked(val);
        return *m_toggle_button;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::ToggleButton* m_toggle_button{nullptr};
};

template <typename ValidatorType, typename NumType>
class NumberControl : public Details::IParamControl
{
public:
    NumberControl(
        const PluginParamDef& param_def,
        std::optional<PluginParamValue> init_value
    ) :
        m_param_def(param_def),
        m_init_value(init_value_of_type<NumType>(std::move(init_value)))
    {}

    PluginParamValue value() const override
    {
        // Parse what the field shows instead of asking its validator. The validator is only
        // updated when the field loses focus, which never happens to a field on a tab that gets
        // hidden while it is being edited. The field itself uses a default validator as well.
        ValidatorType validator;
        validator.process(m_textfield->text());
        return validator.value();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        std::string s_val;
        if (m_init_value.has_value()) {
            s_val = std::to_string(std::get<NumType>(m_init_value.value()));
        } else {
            s_val = std::visit(
                []<typename T>(const T& val) -> std::string
                {
                    if constexpr (std::is_same_v<T, std::string>) {
                        return val;
                    }
                    if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                        return std::to_string(val);
                    }
                    return "0";
                },
                m_param_def.default_value.value_or("0")
            );
        }
        m_textfield = parent.emplace_back<Yoga::InputTextField>();
        auto validator = std::make_unique<ValidatorType>();

        m_textfield->set_validator(std::move(validator));
        m_textfield->set_text(s_val);
        return *m_textfield;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::InputTextField* m_textfield{nullptr};
};

}

PluginDialog::PluginDialog(ProcessFunction process_function)
    : m_process_function(std::move(process_function))
{
    append_tab("");

    this->set_closable(true);
    content_item()->set_modal(true);
    content()->set_gap(5);
}

void
PluginDialog::show_plugin(const PluginMeta& plugin_meta, const PluginParamValueMap& param_values)
{
    using namespace Yoga;

    m_meta = plugin_meta;

    m_param_controls.clear();
    m_pages.clear();
    m_current_page = nullptr;
    while (!content()->items().empty()) {
        content()->remove(content()->get_item(0));
    }

    // the previously shown plugin may have had several tabs
    while (tab_count() > 0) {
        remove_tab(tab_count() - 1);
    }

    content()->set_orientation(Orientation::Vertical);

    // TRN Title of the plugin dialog tab with the parameters that do not belong to any group
    const std::string ungrouped_title = Biz::_u8L("General");
    std::vector<std::string> groups   = param_groups(plugin_meta.params, ungrouped_title);
    const bool grouped                = !groups.empty();
    if (!grouped) {
        // a plugin without groups gets a single tab named after it
        groups.push_back(plugin_meta.title.value_or(plugin_meta.id));
    }
    for (const std::string& group : groups) {
        append_tab(group);
        Item* page = content()->emplace_back<Item>();
        page->set_orientation(Orientation::Vertical);
        page->set_gap(5);
        m_pages.push_back(page);
    }

    const float row_padding = 5.f;
    const float row_gap = 10.f;
    const Paddings button_padding(15.f, 5.f);

    for (const auto& param : plugin_meta.params) {
        size_t page = 0;
        if (grouped) {
            const std::string& group = param.group.has_value() ? *param.group : ungrouped_title;
            page = static_cast<size_t>(std::ranges::find(groups, group) - groups.begin());
        }
        m_current_page = m_pages.at(page);

        auto init_it = param_values.find(param.name);
        auto init_value =
            init_it == param_values.end() ? std::nullopt : std::make_optional(init_it->second);
        if (param.type == "string") {
            emplace_string_param(param, init_value);
        } else if (param.type == "float") {
            emplace_float_param(param, init_value);
        } else if (param.type == "int") {
            emplace_int_param(param, init_value);
        } else if (param.type == "bool") {
            emplace_bool_param(param, init_value);
        } else if (param.type == "choice") {
            emplace_choice_param(param, init_value);
        } else {
            PANIC("Unsupported param type");
        }
    }
    // the buttons belong to the dialog, not to one of the pages
    m_current_page = nullptr;

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_flex_grow(1.f);
    buttons_row->set_padding(row_padding);
    buttons_row->set_gap(row_gap);
    buttons_row->set_justify_content(YGJustifyCenter);

    constexpr int button_height = 40;

    LayoutButton* ok_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Run"));
    ok_btn->set_content_padding(button_padding);
    ok_btn->callbacks().action = [this]()
    {
        if (m_process_function) {
            PluginParamValueMap param_values;
            collect_values(param_values);
            m_process_function(m_meta.value(), param_values);
        }
        close_action();
    };
    ok_btn->set_flex_grow(2);
    ok_btn->set_background_color(Platform::Color::AccentPrimary);
    ok_btn->set_label_font_type(Render::ImguiFontType::Bold);
    ok_btn->set_min_height(button_height);


    LayoutButton* cancel_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_btn->set_content_padding(button_padding);
    cancel_btn->callbacks().action = [this]()
    { close_action(); };
    cancel_btn->set_flex_grow(1);
    cancel_btn->set_min_height(button_height);

    content()->set_min_width(400);

    set_current_tab(0);

    open();
}

void PluginDialog::on_tab_selected(int current_index)
{
    for (size_t i = 0; i < m_pages.size(); ++i) {
        m_pages[i]->set_visible(static_cast<int>(i) == current_index);
    }
}

void PluginDialog::emplace_string_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row = emplace_row(param.label.c_str());
    auto [it, _] =
        m_param_controls.emplace(param.name, std::make_unique<StringControl>(param, default_value));
    style_control(it->second->emplace_control(row));
}

void PluginDialog::
    emplace_choice_param(const PluginParamDef& param, std::optional<PluginParamValue> default_value)
{
    auto& row = emplace_row(param.label.c_str());
    auto [it, _] =
        m_param_controls.emplace(param.name, std::make_unique<ChoiceControl>(param, default_value));
    style_control(it->second->emplace_control(row));
}

void PluginDialog::emplace_float_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row     = emplace_row(param.label.c_str());
    using Control = NumberControl<Yoga::IntValidator, int>;
    auto [it, _]  = m_param_controls.emplace(
        param.name,
        std::make_unique<Control>(param, std::move(default_value))
    );
    style_control(it->second->emplace_control(row));
}

void PluginDialog::emplace_int_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row     = emplace_row(param.label.c_str());
    using Control = NumberControl<Yoga::DoubleValidator, double>;
    auto [it, _]  = m_param_controls.emplace(
        param.name,
        std::make_unique<Control>(param, std::move(default_value))
    );
    style_control(it->second->emplace_control(row));
}

void PluginDialog::emplace_bool_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row = emplace_row();
    auto [it, _] = m_param_controls.emplace(
        param.name,
        std::make_unique<BoolControl>(param, std::move(default_value))
    );
    style_control(it->second->emplace_control(row));
}

Yoga::Item& PluginDialog::emplace_row(const char* label)
{
    Yoga::Item* parent = m_current_page != nullptr ? m_current_page : content();
    auto* row          = parent->emplace_back<Yoga::Item>();
    row->set_orientation(Yoga::Orientation::Horizontal);
    row->set_flex_grow(1.f);
    row->set_flex_shrink(0.f);
    row->set_justify_content(YGJustifySpaceBetween);
    row->set_align_items(YGAlignCenter);

    if (label) {
        row->emplace_back<Yoga::Text>(label)->set_flex_grow(1.f);
    }

    return *row;
}

void PluginDialog::style_control(Yoga::Item& ctrl)
{
    ctrl.set_flex_grow(3);
}

void PluginDialog::collect_values(PluginParamValueMap& param_values) const
{
    for (const auto& [k, v] : m_param_controls) {
        param_values[k] = v->value();
    }
}
}