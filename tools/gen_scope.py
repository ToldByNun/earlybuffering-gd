ZnJvbSBwYXRobGliIGltcG9ydCBQYXRoCgpkZWYgdyhwYXRoLCBsaW5lcyk6CiAgICBQYXRoKHBhdGgpLndyaXRlX3RleHQoIlxuIi5qb2luKGxpbmVzKSArICJcbiIsIGVuY29kaW5nPSJ1dGYtOCIpCiAgICBwcmludCgid3JvdGUiLCBwYXRoKQoKcSA9IGNocigzNCkKdygic3JjL2NvcmUvQXNzaXN0U2NvcGVHYXRlLmhwcCIsIFsKICAgICIjaWZuZGVmIEZSSVJFQ1JBQ0tFUl9DT1JFX0FTU0lTVFNDT1BFR0FURV9IUFAiLAogICAgIiNkZWZpbmUgRlJJUkVDUkFDS0VSX0NPUkVfQVNWSVNUU0NPUEVHQVRFX0hQUCIsCiAgICAiIiwKICAgICJjbGFzcyBQbGF5ZXJPYmplY3Q7IiwKICAgICJjbGFzcyBQbGF5TGF5ZXI7IiwKICAgICIiLAogICAgImNsYXNzIEFzc2lzdFNjb3BlR2F0ZSB7IiwKICAgICJwdWJsaWM6IiwKICAgICIgICAgc3RhdGljIGJvb2wgaXNQbGF0Zm9ybWVyKFBsYXllck9iamVjdCogcGxheWVyKTsiLAogICAgIiAgICBzdGF0aWMgYm9vbCBhbGxvd0Fzc2lzdChQbGF5TGF5ZXIqIGxheWVyLCBQbGF5ZXJPYmplY3QqIHBsYXllcik7IiwKICAgICIgICAgc3RhdGljIGNoYXIgY29uc3QqIGJsb2NrUmVhc29uKFBsYXlMYXllciogbGF5ZXIsIFBsYXllck9iamVjdCogcGxheWVyKTsiLAogICAgIn07IiwKICAgICIiLAogICAgIiNlbmRpZiAvLyBGUklSRUNSQUNLRVJfQ09SRV9BU1NJU1RTQ09QRUdBVEVfSFBQIiwKXSkKCndzID0gWwogICAgZiIjaW5jbHVkZSB7cX1Bc3Npc3RTY29wZUdhdGUuaHBwe3F9IiwKICAgIGYjI2luY2x1ZGUge3F9Li4vY29uZmlnL1N0YWdlQ29uZmlnLmhwcHtxfSIsCiAgICBmIiNpbmNsdWRlIHtxfTxHZW9kZS9HZW9kZS5ocHAze3F9IiwKICAgICJ1c2luZyBuYW1lc3BhY2UgZ2VvZGU6OnByZWx1ZGU7IiwKICAgICIiLAogICAgImJvb2wgQXNzaXN0U2NvcGVHYXRlOjppc1BsYXRmb3JtZXIoUGxheWVyT2JqZWN0KiBwbGF5ZXIpIHsiLAogICAgIiAgICByZXR1cm4gcGxheWVyICYmIHBsYXllci0+bV9pc1BsYXRmb3JtZXI7IiwKICAgICJ9IiwKICAgICIiLAogICAgImJvb2wgQXNzaXN0U2NvcGVHYXRlOjphbGxvd0Fzc2lzdChQbGF5TGF5ZXIqIGxheWVyLCBQbGF5ZXJPYmplY3QqIHBsYXllcikgeyIsCiAgICAiICAgIHJldHVybiBibG9ja1JlYXNvbihsYXllciwgcGxheWVyKSA9PSBudWxscHRyOyIsCiAgICAifSIsCl0KIyBGaXggdGhlIGFib3ZlIC0gdXNlIHNpbXBsZXIgYXBwcm9hY2gKcHJpbnQoInNraXAiKQo='@
# simpler: write generator without complex b64
python -c "open(r'tools/gen_scope.py','wb').write(bytes([
]))"
# Use here-string saved to file with Set-Content -Encoding utf8NoBOM
@'
from pathlib import Path
q = chr(34)
def w(path, lines):
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("wrote", path)

w("src/core/AssistScopeGate.hpp", [
    "#ifndef EARLYBUFFER_CORE_ASSISTSCOPEGATE_HPP",
    "#define EARLYBUFFER_CORE_ASSISTSCOPEGATE_HPP",
    "",
    "class PlayerObject;",
    "class PlayLayer;",
    "",
    "class AssistScopeGate {",
    "public:",
    "    static bool isPlatformer(PlayerObject* player);",
    "    static bool allowAssist(PlayLayer* layer, PlayerObject* player);",
    "    static char const* blockReason(PlayLayer* layer, PlayerObject* player);",
    "};",
    "",
    "#endif // EARLYBUFFER_CORE_ASSISTSCOPEGATE_HPP",
])

inc1 = "#include " + q + "AssistScopeGate.hpp" + q
inc2 = "#include " + q + "../config/StageConfig.hpp" + q
inc3 = "#include " + q + "<Geode/Geode.hpp>" + q
# fix geode include
inc3 = "#include <Geode/Geode.hpp>"
w("src/core/AssistScopeGate.cpp", [
    inc1,
    inc2,
    inc3,
    "using namespace geode::prelude;",
    "",
    "bool AssistScopeGate::isPlatformer(PlayerObject* player) {",
    "    return player && player->m_isPlatformer;",
    "}",
    "",
    "bool AssistScopeGate::allowAssist(PlayLayer* layer, PlayerObject* player) {",
    "    return blockReason(layer, player) == nullptr;",
    "}",
    "",
    "char const* AssistScopeGate::blockReason(PlayLayer* layer, PlayerObject* player) {",
    "    if (!player) return " + q + "no-player" + q + ";",
    "    if (isPlatformer(player)) return " + q + "platformer" + q + ";",
    "    if (StageConfig::assistOnlyPractice()) {",
    "        if (!layer || !layer->m_isPracticeMode) return " + q + "practice-only" + q + ";",
    "    }",
    "    int const lo = StageConfig::assistPercentMin();",
    "    int const hi = StageConfig::assistPercentMax();",
    "    if ((lo > 0 || hi < 100) && layer) {",
    "        float pct = layer->getCurrentPercent();",
    "        if (pct + 0.01f < (float)lo || pct - 0.01f > (float)hi) return " + q + "percent-gate" + q + ";",
    "    }",
    "    if (StageConfig::assistStartposOnly() && layer && !layer->m_isTestMode) {",
    "        return " + q + "startpos-only" + q + ";",
    "    }",
    "    return nullptr;",
    "}",
])
