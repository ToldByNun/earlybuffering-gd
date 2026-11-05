# IDA Dump Parser

Kleines, portables C++20-Programm zum Auftrennen eines IDA-Textdumps in zusammenhängende Hex-Bytes und Pseudo-Assembler.

Kommentare ab `;` werden entfernt. Labels, Direktiven und Instruktionen bleiben im Pseudo-Assembler, damit der Kontext erhalten bleibt.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Nutzung

```sh
./build/ida_dump_parser path/to/ida-dump.txt
```

Unter Windows ist die ausführbare Datei `build/Debug/ida_dump_parser.exe` (bei Visual-Studio-Generatoren). Unter Linux und macOS ist es `build/ida_dump_parser`.
