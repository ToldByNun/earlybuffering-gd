#include "desktop_menu_fonts.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>

class EbDesktopMenuFontsState {
public:
    static ImFont*& uiFace() {
        static ImFont* font = nullptr;
        return font;
    }
    static ImFont*& headerFace() {
        static ImFont* font = nullptr;
        return font;
    }
    static ImFont*& capsFace() {
        static ImFont* font = nullptr;
        return font;
    }

private:
    EbDesktopMenuFontsState() = delete;
};

class EbDesktopMenuFontsPaths {
public:
    static std::filesystem::path findFile(char const* fileName) {
        auto* mod = geode::prelude::Mod::get();
        if (!mod || !fileName) {
            return {};
        }
        auto const root = mod->getResourcesDir();
        std::filesystem::path const candidates[] = {
            root / fileName,
            root / "fonts" / fileName,
            root / mod->getID() / fileName,
            root / mod->getID() / "fonts" / fileName,
        };
        for (auto const& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                return candidate;
            }
        }
        return {};
    }

private:
    EbDesktopMenuFontsPaths() = delete;
};

float EbDesktopMenuFonts::uiSize() {
    return 15.0f;
}

float EbDesktopMenuFonts::headerSize() {
    return 18.0f;
}

float EbDesktopMenuFonts::capsSize() {
    return 12.0f;
}

ImFont* EbDesktopMenuFonts::ui() {
    return EbDesktopMenuFontsState::uiFace();
}

ImFont* EbDesktopMenuFonts::header() {
    return EbDesktopMenuFontsState::headerFace();
}

ImFont* EbDesktopMenuFonts::caps() {
    return EbDesktopMenuFontsState::capsFace();
}

void EbDesktopMenuFonts::pushUi() {
    ImGui::PushFont(ui() ? ui() : ImGui::GetFont());
}

void EbDesktopMenuFonts::pushHeader() {
    ImGui::PushFont(header() ? header() : ImGui::GetFont());
}

void EbDesktopMenuFonts::pushCaps() {
    ImGui::PushFont(caps() ? caps() : ImGui::GetFont());
}

void EbDesktopMenuFonts::pop() {
    ImGui::PopFont();
}

void EbDesktopMenuFonts::load() {
    ImGuiIO& io = ImGui::GetIO();
    EbDesktopMenuFontsState::uiFace() = nullptr;
    EbDesktopMenuFontsState::headerFace() = nullptr;
    EbDesktopMenuFontsState::capsFace() = nullptr;

    auto const regularPath = EbDesktopMenuFontsPaths::findFile("Inter-Regular.ttf");
    auto const semiBoldPath = EbDesktopMenuFontsPaths::findFile("Inter-SemiBold.ttf");
    if (regularPath.empty()) {
        geode::log::warn("[EarlyBuffer] desktop menu: Inter-Regular.ttf not found in resources");
        return;
    }

    std::string const regular = regularPath.string();
    ImFont* uiFace = io.Fonts->AddFontFromFileTTF(regular.c_str(), uiSize());
    if (!uiFace) {
        geode::log::warn("[EarlyBuffer] desktop menu: failed to load Inter-Regular.ttf");
        return;
    }
    EbDesktopMenuFontsState::uiFace() = uiFace;
    io.FontDefault = uiFace;

    std::string const bold = semiBoldPath.empty() ? regular : semiBoldPath.string();
    EbDesktopMenuFontsState::headerFace() =
        io.Fonts->AddFontFromFileTTF(bold.c_str(), headerSize());
    EbDesktopMenuFontsState::capsFace() =
        io.Fonts->AddFontFromFileTTF(bold.c_str(), capsSize());

    if (!EbDesktopMenuFontsState::headerFace()) {
        EbDesktopMenuFontsState::headerFace() = uiFace;
    }
    if (!EbDesktopMenuFontsState::capsFace()) {
        EbDesktopMenuFontsState::capsFace() = uiFace;
    }
}

#else

void EbDesktopMenuFonts::load() {}

float EbDesktopMenuFonts::uiSize() {
    return 15.0f;
}

float EbDesktopMenuFonts::headerSize() {
    return 18.0f;
}

float EbDesktopMenuFonts::capsSize() {
    return 12.0f;
}

#endif
