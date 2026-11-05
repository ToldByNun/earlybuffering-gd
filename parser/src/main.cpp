#include "DisassemblyParser.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(const int argumentCount, char* arguments[]) {
    if (argumentCount != 2) {
        std::cerr << "Usage: ida_dump_parser <ida-dump.txt>\n";
        return 1;
    }

    std::ifstream input(arguments[1], std::ios::binary);

    if (!input) {
        std::cerr << "Could not open input file: " << arguments[1] << '\n';
        return 1;
    }

    const std::string dump(std::istreambuf_iterator<char>(input), {});
    const DisassemblyParser parser;
    const ParsedDisassembly parsed = parser.parse(dump);

    std::cout << "1. " << parsed.hexBytes << " bytes\n\n";
    std::cout << "2. pseudo\n" << parsed.pseudoAssembly << '\n';

    return 0;
}
