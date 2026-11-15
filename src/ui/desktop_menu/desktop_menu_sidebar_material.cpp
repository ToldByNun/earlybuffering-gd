#include "desktop_menu_sidebar_material.hpp"
#include "../../core/platform/ui_capabilities.hpp"
#if defined(EB_DESKTOP_IMGUI)
#include "desktop_menu_bind.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_sidebar_wave_gpu.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_slider.hpp"
#include <algorithm>
#include <cmath>
#include <matjson.hpp>

class EbSidebarMaterialSettings {
public:
    bool enabled = true;
    bool custom = false;
    bool glass = false;
    float refraction = 12.f, highlight = 0.3f, edgeWidth = 20.f, diffusion = 4.f;
    bool blur = false;
    float radius = 8.f, opacity = 0.7f, tint = 0.35f;
    float baseBrightness = 1.f, lightBrightness = 1.f;
    float speed = 1.f;
    static EbSidebarMaterialSettings load() {
        EbSidebarMaterialSettings result;
        auto parsed = matjson::parse(EbDesktopMenuBind::getString("sidebar-material", "{}"));
        if (parsed.isOk() && parsed.unwrap().isObject()) {
            auto value = parsed.unwrap();
            result.glass = value["glass"].asBool().unwrapOr(false);
            result.refraction =
                std::clamp(float(value["refraction"].asDouble().unwrapOr(12)), 0.f, 32.f);
            result.highlight =
                std::clamp(float(value["highlight"].asDouble().unwrapOr(0.3)), 0.f, 1.f);
            result.edgeWidth =
                std::clamp(float(value["edgeWidth"].asDouble().unwrapOr(20)), 1.f, 64.f);
            result.diffusion =
                std::clamp(float(value["diffusion"].asDouble().unwrapOr(4)), 0.f, 32.f);
            result.blur = value["blur"].asBool().unwrapOr(false);
            result.radius = std::clamp(float(value["radius"].asDouble().unwrapOr(8)), 0.f, 32.f);
            result.opacity = std::clamp(float(value["opacity"].asDouble().unwrapOr(0.7)), 0.f, 1.f);
            result.tint = std::clamp(float(value["tint"].asDouble().unwrapOr(0.35)), 0.f, 1.f);
            result.custom = value["custom"].asBool().unwrapOr(false);
            auto const accent = EbDesktopMenuBind::getColor("desktop-accent-color", {123, 97, 255});
            float const peak = std::max({accent.r, accent.g, accent.b}) / 255.f;
            auto brightness = [&](char const* key, char const* legacy, float reference) {
                double oldPeak = 0;
                for (int i = 0; i < 3; ++i)
                    oldPeak = std::max(
                        oldPeak,
                        value[std::string(legacy) + std::to_string(i)].asDouble().unwrapOr(0));
                double fallback =
                    result.custom && oldPeak > 0 ? oldPeak / std::max(0.01f, reference) : 1.0;
                return std::clamp(float(value[key].asDouble().unwrapOr(fallback)), 0.f, 2.f);
            };
            result.baseBrightness = brightness("baseBrightness", "base", peak * 0.8544f);
            result.lightBrightness =
                brightness("lightBrightness", "light", peak + (1.f - peak) * 0.12f);
            for (float* number : {&result.refraction, &result.highlight, &result.edgeWidth,
                                  &result.diffusion, &result.radius, &result.opacity, &result.tint,
                                  &result.speed, &result.baseBrightness, &result.lightBrightness})
                if (!std::isfinite(*number))
                    *number = 0.f;
            result.enabled = value["enabled"].asBool().unwrapOr(true);
            result.speed = std::clamp(float(value["speed"].asDouble().unwrapOr(1)), 0.f, 3.f);
        }
        return result;
    }
    void save() const {
        auto value = matjson::Value::object();
        value["glass"] = glass;
        value["refraction"] = refraction;
        value["highlight"] = highlight;
        value["edgeWidth"] = edgeWidth;
        value["diffusion"] = diffusion;
        value["blur"] = blur;
        value["radius"] = radius;
        value["opacity"] = opacity;
        value["tint"] = tint;
        value["custom"] = custom;
        value["baseBrightness"] = baseBrightness;
        value["lightBrightness"] = lightBrightness;
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
    float const brightness = light ? cached.lightBrightness : cached.baseBrightness;
    return ImVec4(std::clamp(fallback.x * brightness, 0.f, 1.f),
                  std::clamp(fallback.y * brightness, 0.f, 1.f),
                  std::clamp(fallback.z * brightness, 0.f, 1.f), fallback.w);
}
void EbDesktopMenuSidebarMaterial::paint(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding) {
    if (!list || max.x <= min.x || max.y <= min.y)
        return;
    auto settings = EbSidebarMaterialSettings::load();
    if(!EbUiCapabilities::expensiveMaterials()) {
        settings.blur=false;
        settings.glass=false;
    }
    float classic = EbDesktopMenuMotion::smoothToward(0xEB5101u, settings.enabled, 0.12f);
    float blur = EbDesktopMenuMotion::smoothToward(0xEB5102u, settings.blur, 0.12f);
    float glass = EbDesktopMenuMotion::smoothToward(0xEB5103u, settings.glass, 0.12f);
    auto settle = [](float value) { return value < 0.001f ? 0.f : value > 0.999f ? 1.f : value; };
    classic = settle(classic);
    blur = settle(blur);
    glass = settle(glass);
    if ((glass > 0 || blur > 0 || classic > 0 && classic < 1) &&
        EbDesktopMenuSidebarWaveGpu::ensureReady()) {
        EbDesktopMenuSidebarWaveGpu::PaintRequest request;
        request.min = min;
        request.max = max;
        request.rounding = rounding;
        request.timeSeconds = float(ImGui::GetTime()) * settings.speed;
        request.glass = glass > 0;
        request.alpha = std::max({classic, blur, glass});
        request.refraction = settings.refraction * glass;
        request.highlight = settings.highlight * glass;
        request.edgeWidth = settings.edgeWidth;
        request.blur = true;
        request.classic = classic;
        request.blurRadius = settings.diffusion * glass * (1.f - blur) + settings.radius * blur;
        request.opacity = settings.opacity * blur + (1.f - blur) * glass;
        request.tint = settings.tint * std::max(blur, glass);
        request.baseColor = EbDesktopMenuTheme::sidebarWaveDark();
        request.lightColor = EbDesktopMenuTheme::sidebarWaveLight();
        EbDesktopMenuSidebarWaveGpu::paint(list, request);
        return;
    }
    if (classic <= 0)
        return;
    EbDesktopMenuSidebarSmoke::get().paint(list, min, max, rounding,
                                           float(ImGui::GetTime()) * settings.speed,
                                           ImDrawFlags_RoundCornersLeft);
}
void EbDesktopMenuSidebarMaterial::settings() {
    auto value = EbSidebarMaterialSettings::load();
    static int type = 0;
    char const* types[] = {"Classic", "Blur", "Liquid Glass"};
    EbDropdown::draw("sidebar-type", "Type", types, 3, &type, nullptr, true,
                     "Choose which material to edit. Enable switches are independent. Layer order "
                     "is Classic, Blur, Liquid Glass.");
    ImGui::TextDisabled("Active: %s%s%s", value.enabled ? "Classic " : "",
                        value.blur ? "Blur " : "", value.glass ? "Liquid Glass" : "");
    bool changed = false;
    if (type == 0) {
        changed = EbCheckbox::draw("sidebar-classic", "Enable", &value.enabled, true,
                                   "Animated sidebar material. Drawn below Blur and Liquid Glass.");
        char const* presets[] = {"Classic", "Still", "Custom"};
        int selected = value.custom ? 2 : value.speed == 1.f ? 0 : value.speed == 0.f ? 1 : 2;
        if (EbDropdown::draw("sidebar-preset", "Classic Preset", presets, 3, &selected) &&
            selected < 2) {
            value.custom = false;
            value.baseBrightness = value.lightBrightness = 1.f;
            value.speed = selected == 0 ? 1.f : 0.f;
            changed = true;
        }
        changed |= EbSlider::drawFloat("sidebar-speed", "Animation Speed", &value.speed, 0, 3, 2,
                                       "x", value.enabled,
                                       "Zero freezes the wave. Higher values animate faster.");
        bool const baseChanged = EbSlider::drawFloat(
            "sidebar-base-brightness", "Base Brightness", &value.baseBrightness, 0, 2, 2, "x",
            value.enabled,
            "Brightness of the darker Classic wave color, derived from the UI accent.");
        bool const lightChanged = EbSlider::drawFloat(
            "sidebar-light-brightness", "Light Brightness", &value.lightBrightness, 0, 2, 2, "x",
            value.enabled,
            "Brightness of the lighter Classic wave color, derived from the UI accent.");
        if (baseChanged || lightChanged) {
            value.custom = true;
            changed = true;
        }
    } else if (type == 1) {
        changed |= EbCheckbox::draw("sidebar-blur", "Enable", &value.blur, true,
                                    "Soften the material below sidebar text. Classic remains the "
                                    "lower layer when enabled.");
        changed |= EbSlider::drawFloat("sidebar-radius", "Blur Radius", &value.radius, 0, 32, 1,
                                       " px", value.blur);
        changed |= EbSlider::drawFloat("sidebar-opacity", "Blur Opacity", &value.opacity, 0, 1, 2,
                                       "", value.blur);
        changed |=
            EbSlider::drawFloat("sidebar-tint", "Automatic Tint Strength", &value.tint, 0, 1, 2, "",
                                value.blur && !value.enabled,
                                "Tint is applied automatically without Classic. Classic already "
                                "supplies the colored base.");
    }
    if (type == 2) {
        changed |=
            EbCheckbox::draw("sidebar-glass", "Enable", &value.glass, true,
                             "Refract the lower sidebar material. Combines with Classic and Blur.");
        changed |= EbSlider::drawFloat("glass-refraction", "Refraction", &value.refraction, 0, 32,
                                       1, " px", value.glass);
        changed |= EbSlider::drawFloat("glass-highlight", "Edge Highlight", &value.highlight, 0, 1,
                                       2, "", value.glass);
        changed |= EbSlider::drawFloat("glass-width", "Edge Width", &value.edgeWidth, 1, 64, 1,
                                       " px", value.glass);
        changed |=
            EbSlider::drawFloat("glass-diffusion", "Diffusion", &value.diffusion, 0, 32, 1, " px",
                                value.glass && !value.blur,
                                "When Blur is enabled its radius supplies the shared diffusion.");
        changed |=
            EbSlider::drawFloat("glass-tint", "Automatic Tint Strength", &value.tint, 0, 1, 2, "",
                                value.glass && !value.enabled,
                                "Shared with Blur. Classic already provides the colored base.");
    }
    if (EbButton::draw("sidebar-reset", "Reset Sidebar"))
        value = {}, changed = true;
    if (changed)
        value.save();
}
#endif
