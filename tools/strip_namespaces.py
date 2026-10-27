from pathlib import Path
import re

root = Path(r"C:\Users\mikaj\Desktop\earlybuffer\src")


def guard_name(path: Path) -> str:
    rel = path.relative_to(root).as_posix().upper().replace("/", "_").replace(".", "_").replace("-", "_")
    return f"EARLYBUFFER_{rel}"


def strip_namespace_Eg(text: str) -> str:
    text = re.sub(r"^\s*using\s+namespace\s+Eg\s*;\s*\n", "", text, flags=re.M)
    text = re.sub(r"^namespace\s+Eg\s*\{\s*\n", "", text, flags=re.M)
    lines = text.splitlines(True)
    idx = len(lines) - 1
    while idx >= 0 and lines[idx].strip() == "":
        idx -= 1
    if idx >= 0 and lines[idx].strip() == "}":
        lines.pop(idx)
        text = "".join(lines)
        if not text.endswith("\n"):
            text += "\n"
    return text


def replace_pragma(text: str, path: Path) -> str:
    g = guard_name(path)
    if "#pragma once" in text:
        text = text.replace("#pragma once", f"#ifndef {g}\n#define {g}", 1)
        if path.suffix == ".hpp":
            # remove existing trailing endif if we will add ours? keep content
            if f"#endif // {g}" not in text:
                text = text.rstrip() + f"\n\n#endif // {g}\n"
    return text


def add_static_to_funcs(body_text: str) -> str:
    lines = body_text.splitlines(True)
    res = []
    for ln in lines:
        s = ln.lstrip()
        if re.match(r"^(constexpr|inline|struct|class|enum|using|typedef|template|static)", s):
            res.append(ln)
        elif re.match(
            r"^(bool|int|void|float|double|auto|char|unsigned|size_t|std::|ThreatZone|SimPlan|OrbWindow)",
            s,
        ) and "(" in s:
            indent = ln[: len(ln) - len(s)]
            res.append(indent + "static " + s)
        else:
            res.append(ln)
    return "".join(res)


def anon_to_static_block(text: str) -> str:
    out = []
    i = 0
    pattern = re.compile(r"^namespace\s*\{\s*$", re.M)
    while True:
        m = pattern.search(text, i)
        if not m:
            out.append(text[i:])
            break
        out.append(text[i : m.start()])
        body_start = m.end()
        depth = 1
        j = body_start
        while j < len(text) and depth:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
            j += 1
        body = text[body_start : j - 1]
        out.append(add_static_to_funcs(body))
        i = j
        if i < len(text) and text[i] == "\n":
            i += 1
    return "".join(out)


def main() -> None:
    changed = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".hpp", ".cpp"):
            continue
        raw = path.read_text(encoding="utf-8")
        new = raw
        if path.suffix == ".hpp":
            new = replace_pragma(new, path)
        if "namespace Eg" in new or "using namespace Eg" in new:
            new = strip_namespace_Eg(new)
        if re.search(r"^namespace\s*\{", new, flags=re.M):
            new = anon_to_static_block(new)
        if new != raw:
            path.write_text(new, encoding="utf-8")
            changed.append(str(path.relative_to(root)))
    print("changed", len(changed))
    for c in changed:
        print(" ", c)
    left = []
    anon = []
    for path in root.rglob("*"):
        if path.suffix not in (".hpp", ".cpp"):
            continue
        t = path.read_text(encoding="utf-8", errors="replace")
        if "namespace Eg" in t or "using namespace Eg" in t:
            left.append(path.name)
        if re.search(r"^namespace\s*\{", t, flags=re.M):
            anon.append(path.name)
    print("remaining Eg ns", left)
    print("remaining anon ns", anon)


if __name__ == "__main__":
    main()
