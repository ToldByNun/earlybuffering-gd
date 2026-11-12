# Project coding standards

- Work on `earlybuffer-1.6.0` / ship line for GD 2.2081.
- Use early returns for invalid inputs and completed cases. Keep checks before the work they guard and preserve required initialization and cleanup.
- Put C++ class declarations in `snake_case.hpp` and implementations in the matching `snake_case.cpp`.
- Use `#ifndef FILE_NAME_HPP`, `#define FILE_NAME_HPP` and a closing `#endif` in every project header.
- Declare the public interface first and implementation state under `private`. Use `protected` where a framework override requires it.
- Use classes for behavior and state. Keep Geode entry macros and the test executable's `main` as small delegating entry points.
- Do not declare named or anonymous namespaces, namespace aliases, or `using namespace` directives in project code. Qualify dependency types and functions explicitly.
- Use meaningful parameter and variable names. Preserve serialized setting keys, JSON fields, IDs, and externally defined API names when renaming C++ identifiers.
- Format C++ with `.clang-format` and run `python tools/check_cpp_style.py`.
- Build with `geode build` and install the generated package. Run the recorded Wave regression when changing or refactoring input planning.
- Keep generated output and third-party dependency sources outside project refactors.
