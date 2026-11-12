#include "eb_element_list.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../../../config/stage_config.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_theme.hpp"
#include "eb_button.hpp"
#include "eb_slider.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

class EbElementListDraw {
public:
    static bool drawAssistOffRanges() {
        std::vector<EbStageConfig::PercentRange> ranges = EbStageConfig::assistDisableRanges();
        bool changed = false;

        if (ranges.empty()) {
            EbDesktopMenuFonts::pushUi();
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
            ImGui::TextUnformatted("No off ranges — assist runs across Min–Max.");
            ImGui::PopStyleColor();
            EbDesktopMenuFonts::pop();
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }

        int removeIndex = -1;
        for (int index = 0; index < static_cast<int>(ranges.size()); ++index) {
            ImGui::PushID(index);

            char fromLabel[32];
            char toLabel[32];
            std::snprintf(fromLabel, sizeof(fromLabel), "From %d", index + 1);
            std::snprintf(toLabel, sizeof(toLabel), "To %d", index + 1);

            float fromValue = ranges[static_cast<size_t>(index)].from;
            float toValue = ranges[static_cast<size_t>(index)].to;
            bool rowChanged = false;

            if (EbSlider::drawFloat("##from", fromLabel, &fromValue, 0.0f, 100.0f, 1, "%")) {
                rowChanged = true;
            }
            if (EbSlider::drawFloat("##to", toLabel, &toValue, 0.0f, 100.0f, 1, "%")) {
                rowChanged = true;
            }
            if (rowChanged) {
                if (fromValue > toValue) {
                    std::swap(fromValue, toValue);
                }
                ranges[static_cast<size_t>(index)].from = fromValue;
                ranges[static_cast<size_t>(index)].to = toValue;
                changed = true;
            }

            if (EbButton::draw("##remove", "Remove")) {
                removeIndex = index;
            }

            if (index + 1 < static_cast<int>(ranges.size())) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }

            ImGui::PopID();
        }

        if (removeIndex >= 0 && removeIndex < static_cast<int>(ranges.size())) {
            ranges.erase(ranges.begin() + removeIndex);
            changed = true;
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        bool const canAdd = static_cast<int>(ranges.size()) < EbStageConfig::ASSIST_OFF_MAX;
        if (EbButton::draw("##add-off-range", "Add range", canAdd,
                           canAdd ? "Add a percent hole where assist stays off."
                                  : "Soft cap of 32 ranges reached.")) {
            ranges.push_back({0.0f, 10.0f});
            changed = true;
        }

        if (!changed) {
            return false;
        }
        return EbStageConfig::setAssistDisableRanges(ranges);
    }

private:
    EbElementListDraw() = delete;
};

bool EbElementList::drawAssistOffRanges() {
    return EbElementListDraw::drawAssistOffRanges();
}

#else

bool EbElementList::drawAssistOffRanges() {
    return false;
}

#endif
