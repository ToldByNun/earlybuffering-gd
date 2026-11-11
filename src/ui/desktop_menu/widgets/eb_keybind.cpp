#include "eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
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

private:
    EbKeybindState() = delete;
};

class EbKeybindDraw {
public:
    static constexpr float kFieldPadX = 14.0f;
    static constexpr float kFieldPadY = 8.0f;
    static constexpr float kMinFieldW = 140.0f;
    static constexpr float kClearW = 28.0f;

    static bool isModifierKey(ImGuiKey key) {
        return key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl || key == ImGuiKey_LeftShift ||
               key == ImGuiKey_RightShift || key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
               key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper;
    }

    static bool isMouseKey(ImGuiKey key) {
        return key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY;
    }

    static char const* keyToken(ImGuiKey key) {
        switch (key) {
        case ImGuiKey_A:
            return "A";
        case ImGuiKey_B:
            return "B";
        case ImGuiKey_C:
            return "C";
        case ImGuiKey_D:
            return "D";
        case ImGuiKey_E:
            return "E";
        case ImGuiKey_F:
            return "F";
        case ImGuiKey_G:
            return "G";
        case ImGuiKey_H:
            return "H";
        case ImGuiKey_I:
            return "I";
        case ImGuiKey_J:
            return "J";
        case ImGuiKey_K:
            return "K";
        case ImGuiKey_L:
            return "L";
        case ImGuiKey_M:
            return "M";
        case ImGuiKey_N:
            return "N";
        case ImGuiKey_O:
            return "O";
        case ImGuiKey_P:
            return "P";
        case ImGuiKey_Q:
            return "Q";
        case ImGuiKey_R:
            return "R";
        case ImGuiKey_S:
            return "S";
        case ImGuiKey_T:
            return "T";
        case ImGuiKey_U:
            return "U";
        case ImGuiKey_V:
            return "V";
        case ImGuiKey_W:
            return "W";
        case ImGuiKey_X:
            return "X";
        case ImGuiKey_Y:
            return "Y";
        case ImGuiKey_Z:
            return "Z";
        // Geode Keyboard.cpp uses word digit names, not "0"…"9".
        case ImGuiKey_0:
            return "Zero";
        case ImGuiKey_1:
            return "One";
        case ImGuiKey_2:
            return "Two";
        case ImGuiKey_3:
            return "Three";
        case ImGuiKey_4:
            return "Four";
        case ImGuiKey_5:
            return "Five";
        case ImGuiKey_6:
            return "Six";
        case ImGuiKey_7:
            return "Seven";
        case ImGuiKey_8:
            return "Eight";
        case ImGuiKey_9:
            return "Nine";
        case ImGuiKey_F1:
            return "F1";
        case ImGuiKey_F2:
            return "F2";
        case ImGuiKey_F3:
            return "F3";
        case ImGuiKey_F4:
            return "F4";
        case ImGuiKey_F5:
            return "F5";
        case ImGuiKey_F6:
            return "F6";
        case ImGuiKey_F7:
            return "F7";
        case ImGuiKey_F8:
            return "F8";
        case ImGuiKey_F9:
            return "F9";
        case ImGuiKey_F10:
            return "F10";
        case ImGuiKey_F11:
            return "F11";
        case ImGuiKey_F12:
            return "F12";
        case ImGuiKey_Space:
            return "Space";
        case ImGuiKey_Tab:
            return "Tab";
        case ImGuiKey_Enter:
            return "Enter";
        case ImGuiKey_Insert:
            return "Insert";
        case ImGuiKey_Home:
            return "Home";
        case ImGuiKey_End:
            return "End";
        case ImGuiKey_PageUp:
            return "PageUp";
        case ImGuiKey_PageDown:
            return "PageDown";
        case ImGuiKey_LeftArrow:
            return "Left";
        case ImGuiKey_RightArrow:
            return "Right";
        case ImGuiKey_UpArrow:
            return "Up";
        case ImGuiKey_DownArrow:
            return "Down";
        // Match Geode Keyboard.cpp keyNameToCode tokens exactly.
        case ImGuiKey_LeftCtrl:
            return "LeftControl";
        case ImGuiKey_RightCtrl:
            return "RightControl";
        case ImGuiKey_LeftShift:
            return "LeftShift";
        case ImGuiKey_RightShift:
            return "RightShift";
        case ImGuiKey_LeftAlt:
            return "LeftMenu";
        case ImGuiKey_RightAlt:
            return "RightMenu";
        default:
            return nullptr;
        }
    }

    static bool parseBind(std::string const& text, geode::Keybind& out) {
        if (text.empty()) {
            return false;
        }
        auto parsed = geode::Keybind::fromString(text);
        if (parsed.isOk()) {
            out = parsed.unwrap();
            return true;
        }
        return false;
    }

    static std::string buildBindString(ImGuiKey key) {
        char const* token = keyToken(key);
        if (!token) {
            return std::string();
        }
        if (isModifierKey(key)) {
            return std::string(token);
        }

        ImGuiIO const& io = ImGui::GetIO();
        std::string text;
        if (io.KeyCtrl) {
            text += "Ctrl+";
        }
        if (io.KeyShift) {
            text += "Shift+";
        }
        if (io.KeyAlt) {
            text += "Alt+";
        }
        text += token;
        return text;
    }

    static bool tryCapture(std::vector<geode::Keybind>* binds) {
        if (!binds) {
            return false;
        }
        if (EbKeybindState::armDelay() > 0) {
            EbKeybindState::armDelay() -= 1;
            return false;
        }
        // Esc is owned by the shell (cancel capture, else close menu).
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            bool const changed = !binds->empty();
            binds->clear();
            EbKeybind::cancelCapture();
            return changed;
        }

        for (int keyIndex = static_cast<int>(ImGuiKey_NamedKey_BEGIN);
             keyIndex < static_cast<int>(ImGuiKey_NamedKey_END); ++keyIndex) {
            ImGuiKey const key = static_cast<ImGuiKey>(keyIndex);
            if (key == ImGuiKey_Escape || key == ImGuiKey_Backspace || key == ImGuiKey_Delete) {
                continue;
            }
            if (isMouseKey(key)) {
                continue;
            }
            if (!ImGui::IsKeyPressed(key, false)) {
                continue;
            }
            // Wait for a real key — pure modifier presses alone are allowed
            // (RightShift is a valid hotkey), but ignore until key-down edge.
            std::string const text = buildBindString(key);
            geode::Keybind parsed;
            if (!parseBind(text, parsed)) {
                continue;
            }
            binds->clear();
            binds->push_back(parsed);
            EbKeybind::cancelCapture();
            return true;
        }
        return false;
    }

    static ImVec4 idleFill() {
        return EbDesktopMenuTheme::surface();
    }

    static ImVec4 hoverFill() {
        return ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
    }

    static ImVec4 captureFill() {
        return ImVec4(0.22f, 0.20f, 0.32f, 1.0f);
    }

    static bool drawRow(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                        bool enabled) {
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

        std::string const display = capturing ? std::string("Press a key…")
                                              : (binds->empty()
                                                     ? std::string("None")
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
        bool const hovered = enabled && ImGui::IsItemHovered();
        float const heat = enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(),
                                                                       hovered || capturing, 0.05f)
                                   : 0.0f;

        bool changed = false;
        if (capturing) {
            // Click away cancels capture.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) {
                EbKeybind::cancelCapture();
            } else {
                changed = tryCapture(binds);
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
                EbKeybindState::capturingId() = id;
                EbKeybindState::armDelay() = 1;
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
            valueColor =
                EbDesktopMenuMotion::lerp4(valueColor, EbDesktopMenuTheme::accentLight(), heat);
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
            ImVec4 clearColor =
                EbDesktopMenuMotion::lerp4(EbDesktopMenuTheme::muted(), EbDesktopMenuTheme::text(),
                                           heat);
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
}

bool EbKeybind::draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled) {
    return EbKeybindDraw::drawRow(id, label, binds, enabled);
}

bool EbKeybind::drawSetting(char const* key, char const* label, bool enabled) {
    if (!key || key[0] == '\0') {
        return false;
    }
    if (!EbDesktopMenuBind::exists(key)) {
        return false;
    }
    std::vector<geode::Keybind> binds = EbDesktopMenuBind::getKeybind(key);
    if (!EbKeybindDraw::drawRow(key, label, &binds, enabled)) {
        return false;
    }
    return EbDesktopMenuBind::setKeybind(key, binds);
}

#else

bool EbKeybind::isCapturing() {
    return false;
}

void EbKeybind::cancelCapture() {}

bool EbKeybind::draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled) {
    (void)id;
    (void)label;
    (void)binds;
    (void)enabled;
    return false;
}

bool EbKeybind::drawSetting(char const* key, char const* label, bool enabled) {
    (void)key;
    (void)label;
    (void)enabled;
    return false;
}

#endif
