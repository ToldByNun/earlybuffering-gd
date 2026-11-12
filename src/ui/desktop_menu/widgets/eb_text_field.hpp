#ifndef EB_TEXT_FIELD_HPP
#define EB_TEXT_FIELD_HPP

#include <cstddef>
#include <string>

// Themed single-line text field for the desktop ImGui shell (search / share forms).
class EbTextField {
public:
    // Returns true when `value` changed this frame. width <= 0 → full content width.
    static bool draw(char const* id, std::string& value, char const* placeholder = nullptr,
                     bool enabled = true, size_t maxChars = 128, float width = 0.0f);

private:
    EbTextField() = delete;
};

#endif
