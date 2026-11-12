#include "eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include "../desktop_menu_tooltip.hpp"
#include "../../../core/bundle/config_bundle_codec.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

class EbKeybindState {
public:
    static std::string& capturingId() {
        static std::string value;
        return value;
    }

    // Skip capture for one frame after click-to-start (avoids eating the click).
    static int& armDelay() {
        static int value = 0;
        return value;
    }

    // Chord being built: mods + optional primary key. Commit on primary release.
    static geode::KeyboardModifier& draftMods() {
        static geode::KeyboardModifier value = geode::KeyboardModifier::None;
        return value;
    }

    static cocos2d::enumKeyCodes& draftKey() {
        static cocos2d::enumKeyCodes value = cocos2d::KEY_None;
        return value;
    }

    static bool& haveDraftKey() {
        static bool value = false;
        return value;
    }

    static std::string& hint() {
        static std::string value;
        return value;
    }

    static bool& pendingClear() {
        static bool value = false;
        return value;
    }

    static bool& pendingCommit() {
        static bool value = false;
        return value;
    }

    static geode::Keybind& commitBind() {
        static geode::Keybind value;
        return value;
    }

    static float& unsupportedUntil() {
        static float value = 0.0f;
        return value;
    }

    static void resetDraft() {
        draftMods() = geode::KeyboardModifier::None;
        draftKey() = cocos2d::KEY_None;
        haveDraftKey() = false;
        hint().clear();
        pendingClear() = false;
        pendingCommit() = false;
        unsupportedUntil() = 0.0f;
    }

private:
    EbKeybindState() = delete;
};

class EbKeybindCapture {
public:
    static bool isModifierKey(cocos2d::enumKeyCodes key) {
        return key == cocos2d::KEY_Shift || key == cocos2d::KEY_LeftShift ||
               key == cocos2d::KEY_RightShift || key == cocos2d::KEY_Control ||
               key == cocos2d::KEY_LeftControl || key == cocos2d::KEY_RightControl ||
               key == cocos2d::KEY_Alt || key == cocos2d::KEY_LeftMenu ||
               key == cocos2d::KEY_RightMenu || key == cocos2d::KEY_LeftWindowsKey ||
               key == cocos2d::KEY_RightWindowsKey;
    }

    static bool isMouseKey(cocos2d::enumKeyCodes key) {
        return key == cocos2d::MOUSE_4 || key == cocos2d::MOUSE_5;
    }

    // Geode fromString ORs the self-mod bit onto Left/Right Shift/Ctrl/Alt/Win.
    // Live KeyboardInputEvent does the same. Storage + match must use that form or
    // listenForKeybindSettingPresses will miss the bind (close-only / open-broken).
    static geode::KeyboardModifier geodeNormalizeMods(cocos2d::enumKeyCodes key,
                                                     geode::KeyboardModifier mods) {
        geode::KeyboardModifier out = mods;
        if (key == cocos2d::KEY_LeftShift || key == cocos2d::KEY_RightShift ||
            key == cocos2d::KEY_Shift) {
            out |= geode::KeyboardModifier::Shift;
        } else if (key == cocos2d::KEY_LeftControl || key == cocos2d::KEY_RightControl ||
                   key == cocos2d::KEY_Control) {
            out |= geode::KeyboardModifier::Control;
        } else if (key == cocos2d::KEY_LeftMenu || key == cocos2d::KEY_RightMenu ||
                   key == cocos2d::KEY_Alt) {
            out |= geode::KeyboardModifier::Alt;
        } else if (key == cocos2d::KEY_LeftWindowsKey || key == cocos2d::KEY_RightWindowsKey) {
            out |= geode::KeyboardModifier::Super;
        }
        return out;
    }

    static bool isBindableKey(cocos2d::enumKeyCodes key) {
        if (key == cocos2d::KEY_None || key == cocos2d::KEY_Unknown ||
            key == cocos2d::KEY_Escape) {
            return false;
        }
        if (isMouseKey(key)) {
            return false;
        }
        // Probe Geode parse via Keybind ctor + toString round-trip.
        geode::Keybind const probe(key, geode::KeyboardModifier::None);
        std::string const text = probe.toString();
        if (text.empty()) {
            return false;
        }
        auto parsed = geode::Keybind::fromString(text);
        return parsed.isOk();
    }

    static std::string modsPrefix(geode::KeyboardModifier mods) {
        std::string text;
        if (mods & geode::KeyboardModifier::Control) {
            text += "Ctrl+";
        }
        if (mods & geode::KeyboardModifier::Shift) {
            text += "Shift+";
        }
        if (mods & geode::KeyboardModifier::Alt) {
            text += "Alt+";
        }
        if (mods & geode::KeyboardModifier::Super) {
            text += "Win+";
        }
        return text;
    }

    static void updateHint() {
        if (EbKeybindState::unsupportedUntil() > 0.0f) {
            EbKeybindState::hint() = "Unsupported key";
            return;
        }
        if (!EbKeybindState::haveDraftKey()) {
            geode::KeyboardModifier const mods = EbKeybindState::draftMods();
            if (!mods) {
                EbKeybindState::hint() = "Press a key…";
                return;
            }
            EbKeybindState::hint() = modsPrefix(mods) + "…";
            return;
        }

        cocos2d::enumKeyCodes const key = EbKeybindState::draftKey();
        geode::KeyboardModifier const mods = EbKeybindState::draftMods();
        if (!isModifierKey(key)) {
            EbKeybindState::hint() = EbKeybind::canonicalKeybind(key, mods).toString();
            if (EbKeybindState::hint().empty()) {
                EbKeybindState::hint() = "Press a key…";
            }
            return;
        }

        // Solo modifier draft vs multiple mods waiting for a letter/F-key.
        geode::Keybind const solo = EbKeybind::canonicalKeybind(key, mods);
        geode::KeyboardModifier extra = solo.modifiers;
        if (key == cocos2d::KEY_LeftShift || key == cocos2d::KEY_RightShift ||
            key == cocos2d::KEY_Shift) {
            extra &= static_cast<uint8_t>(~static_cast<uint8_t>(geode::KeyboardModifier::Shift));
        } else if (key == cocos2d::KEY_LeftControl || key == cocos2d::KEY_RightControl ||
                   key == cocos2d::KEY_Control) {
            extra &= static_cast<uint8_t>(~static_cast<uint8_t>(geode::KeyboardModifier::Control));
        } else if (key == cocos2d::KEY_LeftMenu || key == cocos2d::KEY_RightMenu ||
                   key == cocos2d::KEY_Alt) {
            extra &= static_cast<uint8_t>(~static_cast<uint8_t>(geode::KeyboardModifier::Alt));
        } else if (key == cocos2d::KEY_LeftWindowsKey || key == cocos2d::KEY_RightWindowsKey) {
            extra &= static_cast<uint8_t>(~static_cast<uint8_t>(geode::KeyboardModifier::Super));
        }
        if (!extra) {
            EbKeybindState::hint() = solo.toString();
            if (EbKeybindState::hint().empty()) {
                EbKeybindState::hint() = "Press a key…";
            }
            return;
        }
        EbKeybindState::hint() = modsPrefix(mods) + "…";
    }

    static void flashUnsupported() {
        EbKeybindState::unsupportedUntil() = 1.15f;
        updateHint();
    }

    static void queueCommit(cocos2d::enumKeyCodes key, geode::KeyboardModifier mods) {
        if (!isBindableKey(key)) {
            flashUnsupported();
            return;
        }
        EbKeybindState::commitBind() = EbKeybind::canonicalKeybind(key, mods);
        EbKeybindState::pendingCommit() = true;
        EbKeybindState::pendingClear() = false;
    }

    static void onPress(cocos2d::enumKeyCodes key, geode::KeyboardModifier mods) {
        if (key == cocos2d::KEY_Escape) {
            EbKeybind::cancelCapture();
            return;
        }
        if (key == cocos2d::KEY_Backspace || key == cocos2d::KEY_Delete) {
            EbKeybindState::pendingClear() = true;
            EbKeybindState::pendingCommit() = false;
            return;
        }
        if (isMouseKey(key) || key == cocos2d::KEY_None || key == cocos2d::KEY_Unknown) {
            flashUnsupported();
            return;
        }

        if (isModifierKey(key)) {
            // Keep chord mods; only treat as solo-mod draft when no primary key yet.
            EbKeybindState::draftMods() = mods;
            if (!EbKeybindState::haveDraftKey() || isModifierKey(EbKeybindState::draftKey())) {
                EbKeybindState::draftKey() = key;
                EbKeybindState::haveDraftKey() = true;
            }
            EbKeybindState::unsupportedUntil() = 0.0f;
            updateHint();
            return;
        }

        if (!isBindableKey(key)) {
            flashUnsupported();
            return;
        }

        // Primary key — mods from the event (Ctrl/Shift/Alt held).
        EbKeybindState::draftKey() = key;
        EbKeybindState::draftMods() = mods;
        EbKeybindState::haveDraftKey() = true;
        EbKeybindState::unsupportedUntil() = 0.0f;
        updateHint();
    }

    static void onRelease(cocos2d::enumKeyCodes key, geode::KeyboardModifier mods) {
        if (!EbKeybindState::haveDraftKey()) {
            if (isModifierKey(key)) {
                EbKeybindState::draftMods() = mods;
                updateHint();
            }
            return;
        }

        bool const draftIsMod = isModifierKey(EbKeybindState::draftKey());
        if (key != EbKeybindState::draftKey()) {
            // Modifier released while a primary key is still held — refresh chord.
            if (isModifierKey(key) && !draftIsMod) {
                EbKeybindState::draftMods() = mods;
                updateHint();
            }
            return;
        }

        // Release commits. canonicalKeybind applies Geode self-mod bits for modifiers.
        geode::KeyboardModifier const commitMods =
            draftIsMod ? geode::KeyboardModifier::None : EbKeybindState::draftMods();
        queueCommit(EbKeybindState::draftKey(), commitMods);
    }

    static void note(geode::KeyboardInputData const& data) {
        if (!EbKeybind::isCapturing()) {
            return;
        }
        if (EbKeybindState::armDelay() > 0) {
            return;
        }
        if (data.action == geode::KeyboardInputData::Action::Repeat) {
            return;
        }
        if (data.action == geode::KeyboardInputData::Action::Press) {
            onPress(data.key, data.modifiers);
            return;
        }
        if (data.action == geode::KeyboardInputData::Action::Release) {
            onRelease(data.key, data.modifiers);
        }
    }

private:
    EbKeybindCapture() = delete;
};

class EbKeybindDraw {
public:
    static constexpr float kFieldPadX = 14.0f;
    static constexpr float kFieldPadY = 8.0f;
    static constexpr float kMinFieldW = 140.0f;
    static constexpr float kClearW = 28.0f;

    static ImVec4 idleFill() {
        return EbDesktopMenuTheme::surface();
    }

    static ImVec4 hoverFill() {
        return ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
    }

    static ImVec4 captureFill() {
        return ImVec4(0.22f, 0.20f, 0.32f, 1.0f);
    }

    static bool consumeCaptureResult(std::vector<geode::Keybind>* binds) {
        if (!binds) {
            return false;
        }
        if (EbKeybindState::armDelay() > 0) {
            EbKeybindState::armDelay() -= 1;
        }
        if (EbKeybindState::unsupportedUntil() > 0.0f) {
            EbKeybindState::unsupportedUntil() =
                (std::max)(0.0f, EbKeybindState::unsupportedUntil() - ImGui::GetIO().DeltaTime);
            if (EbKeybindState::unsupportedUntil() <= 0.0f) {
                EbKeybindCapture::updateHint();
            }
        }
        if (EbKeybindState::pendingClear()) {
            bool const changed = !binds->empty();
            binds->clear();
            EbKeybind::cancelCapture();
            return changed;
        }
        if (EbKeybindState::pendingCommit()) {
            binds->clear();
            binds->push_back(EbKeybindState::commitBind());
            EbKeybind::cancelCapture();
            return true;
        }
        return false;
    }

    static bool drawRow(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                        bool enabled, char const* tooltip) {
        if (!id || id[0] == '\0' || !label || !binds) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        bool const capturing = EbKeybindState::capturingId() == id;
        float const avail = ImGui::GetContentRegionAvail().x;
        float const textH = ImGui::GetFontSize();
        float const fieldH = textH + kFieldPadY * 2.0f;
        float const rowH = (std::max)(fieldH, textH + 10.0f);
        float const rounding = 8.0f;

        std::string const display =
            capturing ? std::string(EbKeybind::captureHint())
                      : (binds->empty() ? std::string("None")
                                        : EbConfigBundleCodec::keybindsToString(*binds));
        float fieldW = ImGui::CalcTextSize(display.c_str()).x + kFieldPadX * 2.0f + kClearW;
        if (fieldW < kMinFieldW) {
            fieldW = kMinFieldW;
        }
        if (fieldW > avail * 0.62f) {
            fieldW = avail * 0.62f;
        }

        ImVec2 const rowMin = ImGui::GetCursorScreenPos();
        ImVec2 const fieldMin(rowMin.x + avail - fieldW, rowMin.y + (rowH - fieldH) * 0.5f);
        ImVec2 const fieldMax(fieldMin.x + fieldW, fieldMin.y + fieldH);

        bool pressed = false;
        if (enabled) {
            pressed = ImGui::InvisibleButton("##eb-key-row", ImVec2(avail, rowH));
        } else {
            ImGui::Dummy(ImVec2(avail, rowH));
        }
        if (!capturing) {
            EbDesktopMenuTooltip::show(tooltip);
        }
        bool const hovered = enabled && ImGui::IsItemHovered();
        float const heat = enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(),
                                                                       hovered || capturing, 0.05f)
                                   : 0.0f;

        bool changed = false;
        if (capturing) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) {
                EbKeybind::cancelCapture();
            } else {
                changed = consumeCaptureResult(binds);
            }
        } else if (pressed && enabled) {
            ImVec2 const mouse = ImGui::GetIO().MousePos;
            bool const hitClear = !binds->empty() && mouse.x >= fieldMax.x - kClearW &&
                                  mouse.x <= fieldMax.x && mouse.y >= fieldMin.y &&
                                  mouse.y <= fieldMax.y;
            if (hitClear) {
                binds->clear();
                changed = true;
            } else {
                EbKeybindState::resetDraft();
                EbKeybindState::capturingId() = id;
                EbKeybindState::armDelay() = 1;
                EbKeybindState::hint() = "Press a key…";
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 labelColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else {
            labelColor = EbDesktopMenuMotion::lerp4(labelColor, EbDesktopMenuTheme::text(), heat);
        }
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(rowMin.x, rowMin.y + (rowH - textH) * 0.5f),
                          EbDesktopMenuTheme::toU32(labelColor), label);

        ImVec4 fill = idleFill();
        if (!enabled) {
            fill.w = 0.75f;
        } else if (capturing) {
            fill = EbDesktopMenuMotion::lerp4(fill, captureFill(), heat);
        } else {
            fill = EbDesktopMenuMotion::lerp4(fill, hoverFill(), heat);
        }
        drawList->AddRectFilled(fieldMin, fieldMax, EbDesktopMenuTheme::toU32(fill), rounding);

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        stroke.w = enabled ? 0.7f : 0.4f;
        if (enabled) {
            ImVec4 hot = capturing ? EbDesktopMenuTheme::accentLight()
                                   : ImVec4(0.55f, 0.57f, 0.66f, 0.95f);
            if (capturing) {
                hot.w = 0.9f;
            }
            stroke = EbDesktopMenuMotion::lerp4(stroke, hot, heat);
        }
        drawList->AddRect(fieldMin, fieldMax, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        ImVec4 valueColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            valueColor = ImVec4(0.62f, 0.64f, 0.70f, 0.85f);
        } else if (capturing) {
            if (EbKeybindState::unsupportedUntil() > 0.0f) {
                valueColor = EbDesktopMenuTheme::danger();
            } else {
                valueColor = EbDesktopMenuMotion::lerp4(valueColor, EbDesktopMenuTheme::accentLight(),
                                                       heat);
            }
        } else {
            valueColor = EbDesktopMenuMotion::lerp4(valueColor, EbDesktopMenuTheme::text(), heat);
        }
        ImVec2 const valueSize = ImGui::CalcTextSize(display.c_str());
        float const valueMaxX = fieldMax.x - (binds->empty() || capturing ? kFieldPadX : kClearW);
        drawList->PushClipRect(fieldMin, ImVec2(valueMaxX, fieldMax.y), true);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(fieldMin.x + kFieldPadX,
                                 fieldMin.y + (fieldH - valueSize.y) * 0.5f),
                          EbDesktopMenuTheme::toU32(valueColor), display.c_str());
        drawList->PopClipRect();

        if (enabled && !binds->empty() && !capturing) {
            ImVec4 clearColor = EbDesktopMenuMotion::lerp4(
                EbDesktopMenuTheme::muted(), EbDesktopMenuTheme::text(), heat);
            ImVec2 const clearSize = ImGui::CalcTextSize("×");
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(fieldMax.x - kClearW * 0.5f - clearSize.x * 0.5f,
                                     fieldMin.y + (fieldH - clearSize.y) * 0.5f),
                              EbDesktopMenuTheme::toU32(clearColor), "×");
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed;
    }

private:
    EbKeybindDraw() = delete;
};

bool EbKeybind::isCapturing() {
    return !EbKeybindState::capturingId().empty();
}

void EbKeybind::cancelCapture() {
    EbKeybindState::capturingId().clear();
    EbKeybindState::armDelay() = 0;
    EbKeybindState::resetDraft();
}

void EbKeybind::noteKeyboard(geode::KeyboardInputData const& data) {
    EbKeybindCapture::note(data);
}

char const* EbKeybind::captureHint() {
    if (EbKeybindState::hint().empty()) {
        return "Press a key…";
    }
    return EbKeybindState::hint().c_str();
}

geode::Keybind EbKeybind::canonicalKeybind(cocos2d::enumKeyCodes key,
                                           geode::KeyboardModifier mods) {
    return geode::Keybind(key, EbKeybindCapture::geodeNormalizeMods(key, mods));
}

geode::Keybind EbKeybind::canonicalKeybind(geode::Keybind const& bind) {
    return canonicalKeybind(bind.key, bind.modifiers);
}

bool EbKeybind::draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled, char const* tooltip) {
    return EbKeybindDraw::drawRow(id, label, binds, enabled, tooltip);
}

bool EbKeybind::drawSetting(char const* key, char const* label, bool enabled) {
    if (!key || key[0] == '\0') {
        return false;
    }
    if (!EbDesktopMenuBind::exists(key)) {
        return false;
    }
    std::vector<geode::Keybind> binds = EbDesktopMenuBind::getKeybind(key);
    // Repair binds saved without Geode self-mod bits (e.g. RightShift+None).
    bool repaired = false;
    for (geode::Keybind& bind : binds) {
        geode::Keybind const canon = canonicalKeybind(bind);
        if (!(canon == bind)) {
            bind = canon;
            repaired = true;
        }
    }
    if (repaired) {
        EbDesktopMenuBind::setKeybind(key, binds);
    }
    std::string const tip = EbDesktopMenuTooltip::descriptionForKey(key);
    if (!EbKeybindDraw::drawRow(key, label, &binds, enabled, tip.c_str())) {
        return false;
    }
    return EbDesktopMenuBind::setKeybind(key, binds);
}

#else

bool EbKeybind::isCapturing() {
    return false;
}

void EbKeybind::cancelCapture() {}

void EbKeybind::noteKeyboard(geode::KeyboardInputData const&) {}

char const* EbKeybind::captureHint() {
    return "Press a key…";
}

geode::Keybind EbKeybind::canonicalKeybind(cocos2d::enumKeyCodes key,
                                           geode::KeyboardModifier mods) {
    return geode::Keybind(key, mods);
}

geode::Keybind EbKeybind::canonicalKeybind(geode::Keybind const& bind) {
    return bind;
}

bool EbKeybind::draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled, char const* tooltip) {
    (void)id;
    (void)label;
    (void)binds;
    (void)enabled;
    (void)tooltip;
    return false;
}

bool EbKeybind::drawSetting(char const* key, char const* label, bool enabled) {
    (void)key;
    (void)label;
    (void)enabled;
    return false;
}

#endif
