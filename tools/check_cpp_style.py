"""Check the structural C++ conventions for authored source and test files."""

from pathlib import Path
import re


class CppStyleChecker:
    @staticmethod
    def mask_literals(source):
        source = re.sub(r'R"([^ ()\\\t]{0,16})\(.*?\)\1"', '""', source, flags=re.S)
        return re.sub(
            r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
            lambda match: "\n" * match[0].count("\n"),
            source,
            flags=re.S,
        )

    @staticmethod
    def run():
        failures = []
        files = sorted(
            path
            for directory in (Path("src"), Path("tools"))
            for path in directory.rglob("*")
            if path.suffix in (".cpp", ".hpp")
            and not any(part in ("node_modules", ".vercel") for part in path.parts)
        )
        for path in files:
            source = path.read_text(encoding="utf-8")
            code = CppStyleChecker.mask_literals(source)
            if not re.fullmatch(r"[a-z][a-z0-9_]*", path.stem):
                failures.append(f"{path}: filename must use snake_case")
            counterpart = path.with_suffix(".hpp" if path.suffix == ".cpp" else ".cpp")
            if not counterpart.is_file():
                failures.append(f"{path}: missing {counterpart.name}")
            if re.search(r"\bnamespace\b", code):
                failures.append(f"{path}: namespace declaration, alias or import")
            if re.search(r"\bstruct\b", code):
                failures.append(f"{path}: use a class with explicit access sections")
            if path.suffix == ".hpp":
                guard = path.stem.upper() + "_HPP"
                if not re.search(r"#ifndef\s+" + guard + r"\s*\n#define\s+" + guard + r"\b", code):
                    failures.append(f"{path}: missing {guard} include guard")
                if not re.search(r"#endif\s*$", code):
                    failures.append(f"{path}: missing closing include guard")
                for match in re.finditer(
                    r"\bclass\s+(?:\w+(?:::\w+)*|\$modify\([^)]*\))[^;{]*\{", code
                ):
                    if re.match(r"class\s+\w+\s*[>,]", match[0]):
                        continue
                    if re.search(r"enum\s*$", code[max(0, match.start() - 6):match.start()]):
                        continue
                    position = match.end()
                    depth = 1
                    public_section = False
                    private_section = False
                    while position < len(code) and depth:
                        if depth == 1:
                            public_section |= bool(re.match(r"public\s*:", code[position:]))
                            private_section |= bool(re.match(r"private\s*:", code[position:]))
                        depth += (code[position] == "{") - (code[position] == "}")
                        position += 1
                    if not public_section or not private_section:
                        failures.append(f"{path}: class needs public and private sections")
        for failure in failures:
            print(failure)
        print(f"C++ style audit: {len(files)} files, {len(failures)} failures")
        return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(CppStyleChecker.run())
