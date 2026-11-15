#include "desktop_menu_background.hpp"
#if defined(EB_DESKTOP_IMGUI)
#include "desktop_menu_bind.hpp"
#include "desktop_menu_motion.hpp"
#include "desktop_menu_sidebar_wave_gpu.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_slider.hpp"
#include <algorithm>
#include <cmath>
#include <matjson.hpp>
class EbBackgroundSettings {
public:
    bool enabled = false, blur = false;
    float radius = 12.f, opacity = 1.f, quality = 0.5f;
    float amount = 0.35f;
    float rgb[3] = {0, 0, 0};
    static EbBackgroundSettings load() {
        EbBackgroundSettings result;
        auto parsed = matjson::parse(EbDesktopMenuBind::getString("background-material", "{}"));
        if (parsed.isErr() || !parsed.unwrap().isObject())
            return result;
        auto value = parsed.unwrap();
        result.enabled = value["enabled"].asBool().unwrapOr(false);
        auto read = [&](std::string key, float fallback) {
            double number = value[key].asDouble().unwrapOr(fallback);
            return std::isfinite(number) ? std::clamp(float(number), 0.f, 1.f) : fallback;
        };
        result.amount = read("amount", 0.35f);
        result.blur = value["blur"].asBool().unwrapOr(false);
        result.radius = read("radius", 0.375f) * 32.f;
        result.opacity = read("opacity", 1.f);
        result.quality = std::clamp(read("quality", 0.5f), 0.25f, 1.f);
        for (int i = 0; i < 3; ++i)
            result.rgb[i] = read("color" + std::to_string(i), 0);
        return result;
    }
    void save() const {
        auto value = matjson::Value::object();
        value["blur"] = blur;
        value["radius"] = radius / 32.f;
        value["opacity"] = opacity;
        value["quality"] = quality;
        value["enabled"] = enabled;
        value["amount"] = amount;
        for (int i = 0; i < 3; ++i)
            value["color" + std::to_string(i)] = rgb[i];
        EbDesktopMenuBind::setString("background-material", value.dump());
    }

private:
};
ImVec4 EbDesktopMenuBackground::dim(float openAlpha) {
    auto value = EbBackgroundSettings::load();
    static int frame = -1;
    static float strength = 0.f;
    if (frame != ImGui::GetFrameCount()) {
        frame = ImGui::GetFrameCount();
        strength =
            EbDesktopMenuMotion::smoothFloat(0xEBBA001u, value.enabled ? value.amount : 0.f, 0.12f);
    }
    return ImVec4(value.rgb[0], value.rgb[1], value.rgb[2],
                  strength * std::clamp(openAlpha, 0.f, 1.f));
}
void EbDesktopMenuBackground::paint(float openAlpha) {
    auto color = dim(openAlpha);
    auto value = EbBackgroundSettings::load();
    float blend =
        EbDesktopMenuMotion::smoothFloat(0xEBBA002u, value.blur ? value.opacity : 0.f, 0.12f);
    if (blend > 0.001f && value.radius > 0 && EbDesktopMenuSidebarWaveGpu::ensureReady()) {
        auto* viewport = ImGui::GetMainViewport();
        EbDesktopMenuSidebarWaveGpu::PaintRequest request;
        request.background = true;
        request.classic = 0;
        request.blur = true;
        request.min = viewport->Pos;
        request.max =
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        request.blurRadius = value.radius;
        request.opacity = blend;
        request.alpha = openAlpha;
        request.renderScale = value.quality;
        request.tint = 0;
        request.rounding = 0;
        EbDesktopMenuSidebarWaveGpu::paint(ImGui::GetBackgroundDrawList(), request);
        return;
    }
    if (color.w <= 0)
        return;
    auto* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
        ImGui::ColorConvertFloat4ToU32(color));
}
void EbDesktopMenuBackground::settings() {
    auto value = EbBackgroundSettings::load();
    static int type = 0;
    char const* types[] = {"Dimmed", "Blur"};
    EbDropdown::draw("background-type", "Type", types, 2, &type);
    bool changed = false;
    if (type == 0) {
        changed = EbCheckbox::draw("background-dim", "Enable Dimmed", &value.enabled, true,
                                   "Tint the game behind the entire menu. Sidebar materials and "
                                   "controls stay above this layer.");
        changed |= EbSlider::drawFloat("background-dim-amount", "Dim Strength", &value.amount, 0, 1,
                                       2, "", value.enabled,
                                       "Zero leaves the game visible. One replaces it with the "
                                       "selected color. Fades with menu opening and closing.");
        changed |=
            EbColorPicker::draw("background-dim-color", "Dim Color", value.rgb, value.enabled);
    } else {
        changed |= EbCheckbox::draw("background-blur-enable", "Enable", &value.blur);
        changed |= EbSlider::drawFloat("background-blur-radius", "Blur Radius", &value.radius, 0,
                                       32, 1, " px", value.blur);
        changed |= EbSlider::drawFloat("background-blur-opacity", "Blur Opacity", &value.opacity, 0,
                                       1, 2, "", value.blur);
        changed |= EbSlider::drawFloat(
            "background-blur-quality", "Render Scale", &value.quality, 0.25f, 1, 2, "x", value.blur,
            "Lower resolution reduces GPU work. Half resolution is the default.");
    }
    if (EbButton::draw("background-reset", "Reset Background")) {
        value = {};
        changed = true;
    }
    if (changed)
        value.save();
}
#endif
