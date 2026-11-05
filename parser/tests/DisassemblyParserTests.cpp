#include "DisassemblyParser.hpp"

#include <cassert>
#include <string_view>

int main() {
    constexpr std::string_view dump = R"(.text:0000000140C66750                                                 ; header
.text:0000000140C66750                                                 calcNextState proc near ; metadata
.text:0000000140C66750 40 53                                                           push rbx
.text:0000000140C66752 48 83 EC 20                                                     sub rsp, 20h ; stack space
.text:0000000140C66756                                                 loc_140C66756:
.text:0000000140C66756 C3                                                              retn
.text:0000000140C66757 98 67 C6 00                                                     jpt_140C66756 dd offset loc_140C66756
)";

    const DisassemblyParser parser;
    const ParsedDisassembly parsed = parser.parse(dump);

    assert(parsed.hexBytes == "40 53 48 83 EC 20 C3 98 67 C6 00");
    assert(parsed.pseudoAssembly == "calcNextState proc near\npush rbx\nsub rsp, 20h\nloc_140C66756:\nretn\njpt_140C66756 dd offset loc_140C66756");

    return 0;
}
