#ifndef DISASSEMBLY_PARSER_HPP
#define DISASSEMBLY_PARSER_HPP

#include <string>
#include <string_view>

struct ParsedDisassembly {
public:
    std::string hexBytes;
    std::string pseudoAssembly;
};

class DisassemblyParser {
public:
    [[nodiscard]] ParsedDisassembly parse(std::string_view dump) const;

private:
    [[nodiscard]] static bool isHexByte(std::string_view token);
    [[nodiscard]] static std::string_view trim(std::string_view value);
    static void appendWithSpace(std::string& target, std::string_view value);
    static void appendWithNewline(std::string& target, std::string_view value);
};

#endif // DISASSEMBLY_PARSER_HPP
