#include "DisassemblyParser.hpp"

#include <cctype>

namespace {

[[nodiscard]] bool isSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

[[nodiscard]] bool isHexDigit(const char character) {
    return std::isxdigit(static_cast<unsigned char>(character)) != 0;
}

} // namespace

ParsedDisassembly DisassemblyParser::parse(const std::string_view dump) const {
    ParsedDisassembly result;
    std::size_t lineStart = 0;

    while (lineStart < dump.size()) {
        const std::size_t lineEnd = dump.find('\n', lineStart);
        const std::size_t lineLength = lineEnd == std::string_view::npos ? dump.size() - lineStart : lineEnd - lineStart;
        std::string_view line = dump.substr(lineStart, lineLength);
        const std::size_t commentStart = line.find(';');

        if (commentStart != std::string_view::npos) line = line.substr(0, commentStart);

        line = this->trim(line);
        const std::size_t addressEnd = line.find_first_of(" \t");

        if (addressEnd != std::string_view::npos && line.starts_with(".text:")) {
            std::string_view content = this->trim(line.substr(addressEnd));
            std::size_t cursor = 0;

            while (cursor < content.size()) {
                const std::size_t tokenEnd = content.find_first_of(" \t", cursor);
                const std::size_t tokenLength = tokenEnd == std::string_view::npos ? content.size() - cursor : tokenEnd - cursor;
                const std::string_view token = content.substr(cursor, tokenLength);

                if (!this->isHexByte(token)) break;

                this->appendWithSpace(result.hexBytes, token);

                if (tokenEnd == std::string_view::npos) {
                    cursor = content.size();
                    break;
                }

                const std::size_t nextTokenStart = content.find_first_not_of(" \t", tokenEnd);

                if (nextTokenStart == std::string_view::npos) {
                    cursor = content.size();
                    break;
                }

                // IDA separates the byte column from the instruction column with padding.
                // This prevents data directives such as "dd" from being parsed as a hex byte.
                if (nextTokenStart - tokenEnd > 1) {
                    cursor = nextTokenStart;
                    break;
                }

                cursor = nextTokenStart;
            }

            const std::string_view assembly = this->trim(content.substr(cursor));

            if (!assembly.empty()) this->appendWithNewline(result.pseudoAssembly, assembly);
        }

        if (lineEnd == std::string_view::npos) break;

        lineStart = lineEnd + 1;
    }

    return result;
}

bool DisassemblyParser::isHexByte(const std::string_view token) {
    return token.size() == 2 && isHexDigit(token[0]) && isHexDigit(token[1]);
}

std::string_view DisassemblyParser::trim(std::string_view value) {
    while (!value.empty() && isSpace(value.front())) value.remove_prefix(1);
    while (!value.empty() && isSpace(value.back())) value.remove_suffix(1);

    return value;
}

void DisassemblyParser::appendWithSpace(std::string& target, const std::string_view value) {
    if (!target.empty()) target.push_back(' ');

    target.append(value);
}

void DisassemblyParser::appendWithNewline(std::string& target, const std::string_view value) {
    if (!target.empty()) target.push_back('\n');

    target.append(value);
}
