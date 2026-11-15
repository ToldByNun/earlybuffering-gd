#include "desktop_menu_sidebar_material.hpp"
#if defined(EB_DESKTOP_IMGUI)
#include "desktop_menu_bind.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_slider.hpp"
#include <algorithm>
#include <matjson.hpp>

class EbSidebarMaterialSettings {
public:
    bool enabled = true;
    bool custom = false;
    float base[3] = {0.3f, 0.25f, 0.65f}, light[3] = {0.6f, 0.5f, 1.f};
    float speed = 1.f;
    static EbSidebarMaterialSettings load() {
        EbSidebarMaterialSettings result;
        auto parsed = matjson::parse(EbDesktopMenuBind::getString("sidebar-material", "{}"));
        if (parsed.isOk()) {
            auto value = parsed.unwrap();
            result.custom = value["custom"].asBool().unwrapOr(false);
            for (int i = 0; i < 3; ++i) {
                result.base[i] = std::clamp(
                    float(value["base" + std::to_string(i)].asDouble().unwrapOr(result.base[i])),
                    0.f, 1.f);
                result.light[i] = std::clamp(
                    float(value["light" + std::to_string(i)].asDouble().unwrapOr(result.light[i])),
                    0.f, 1.f);
            }
            result.enabled = value["enabled"].asBool().unwrapOr(true);
            result.speed = std::clamp(float(value["speed"].asDouble().unwrapOr(1)), 0.f, 3.f);
        }
        return result;
    }
    void save() const {
        auto value = matjson::Value::object();
        value["custom"] = custom;
        for (int i = 0; i < 3; ++i) {
            value["base" + std::to_string(i)] = base[i];
            value["light" + std::to_string(i)] = light[i];
        }
        value["enabled"] = enabled;
        value["speed"] = speed;
        EbDesktopMenuBind::setString("sidebar-material", value.dump());
    }

private:
};
ImVec4 EbDesktopMenuSidebarMaterial::color(bool light, ImVec4 fallback) {
    static int frame = -1;
    static EbSidebarMaterialSettings cached;
    if (frame != ImGui::GetFrameCount()) {
        frame = ImGui::GetFrameCount();
        cached = EbSidebarMaterialSettings::load();
    }
    if (!cached.custom)
        return fallback;
    auto const* rgb = light ? cached.light : cached.base;
    return ImVec4(rgb[0], rgb[1], rgb[2], fallback.w);
}
void EbDesktopMenuSidebarMaterial::paint(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding) {
    if (!list || max.x <= min.x || max.y <= min.y)
        return;
    auto settings = EbSidebarMaterialSettings::load();
    if (!settings.enabled)
        return;
    EbDesktopMenuSidebarSmoke::get().paint(list, min, max, rounding,
                                           float(ImGui::GetTime()) * settings.speed,
                                           ImDrawFlags_RoundCornersLeft);
}
void EbDesktopMenuSidebarMaterial::settings() {
    auto value = EbSidebarMaterialSettings::load();
    bool changed =
        EbCheckbox::draw("sidebar-classic", "Classic", &value.enabled, true,
                         "Animated sidebar material. Drawn below Blur and Liquid Glass.");
    char const* presets[] = {"Classic", "Still", "Custom"};
    int selected = value.custom ? 2 : value.speed == 1.f ? 0 : value.speed == 0.f ? 1 : 2;
    if (EbDropdown::draw("sidebar-preset", "Classic Preset", presets, 3, &selected) &&
        selected < 2) {
        value.custom = false;
        value.speed = selected == 0 ? 1.f : 0.f;
        changed = true;
    }
    changed |=
        EbSlider::drawFloat("sidebar-speed", "Animation Speed", &value.speed, 0, 3, 2, "x",
                            value.enabled, "Zero freezes the wave. Higher values animate faster.");
    if (EbColorPicker::draw("sidebar-base", "Base Color", value.base, value.enabled) |
        EbColorPicker::draw("sidebar-light", "Light Color", value.light, value.enabled)) {
        value.custom = true;
        changed = true;
    }
    if (EbButton::draw("sidebar-reset", "Reset Sidebar"))
        value = {}, changed = true;
    if (changed)
        value.save();
}
#endif
