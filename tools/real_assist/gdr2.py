"""Minimal GDR2 reader matching libGDR / EbReplayImport (classic Jump-only packing)."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Gdr2Input:
    frame: int
    button: int
    player2: bool
    down: bool


@dataclass
class Gdr2Replay:
    version: int
    input_tag: str
    author: str
    description: str
    duration: float
    game_version: int
    framerate: float
    seed: int
    coins: int
    ldm: bool
    platformer: bool
    bot_name: str
    bot_version: int
    level_id: int
    level_name: str
    deaths: list[int]
    inputs: list[Gdr2Input]


class _Reader:
    def __init__(self, data: bytes) -> None:
        self._data = memoryview(data)
        self._i = 0

    def remaining(self) -> int:
        return len(self._data) - self._i

    def take(self, count: int) -> bytes:
        if self.remaining() < count:
            raise ValueError("truncated GDR2")
        start = self._i
        self._i += count
        return bytes(self._data[start : self._i])

    def varint(self) -> int:
        result = 0
        shift = 0
        for _ in range(10):
            if self.remaining() == 0:
                raise ValueError("truncated GDR2 varint")
            byte = self._data[self._i]
            self._i += 1
            result |= (byte & 0x7F) << shift
            if (byte & 0x80) == 0:
                return result
            shift += 7
        raise ValueError("GDR2 varint too long")

    def string(self) -> str:
        length = self.varint()
        if length > 0xFFFF:
            raise ValueError("GDR2 string too long")
        return self.take(length).decode("utf-8", "replace")

    def f32(self) -> float:
        return struct.unpack(">f", self.take(4))[0]

    def f64(self) -> float:
        return struct.unpack(">d", self.take(8))[0]

    def boolean(self) -> bool:
        return bool(self.varint())


def parse_gdr2(path: Path) -> Gdr2Replay:
    data = path.read_bytes()
    reader = _Reader(data)
    magic = reader.take(3)
    if magic != b"GDR":
        raise ValueError(f"invalid magic {magic!r}")

    version = reader.varint()
    input_tag = reader.string()
    author = reader.string()
    description = reader.string()
    duration = reader.f32()
    game_version = reader.varint()
    framerate = reader.f64()
    seed = reader.varint()
    coins = reader.varint()
    ldm = reader.boolean()
    platformer = reader.boolean()
    bot_name = reader.string()
    bot_version = reader.varint()
    level_id = reader.varint()
    level_name = reader.string()

    extension_size = reader.varint()
    reader.take(extension_size)

    death_count = reader.varint()
    deaths: list[int] = []
    previous = 0
    for _ in range(death_count):
        delta = reader.varint()
        previous += delta
        deaths.append(previous)

    input_count = reader.varint()
    remaining_p1 = reader.varint()
    inputs: list[Gdr2Input] = []
    previous = 0
    while reader.remaining() > 0 and len(inputs) < input_count:
        packed = reader.varint()
        if platformer:
            delta = packed >> 3
            button = (packed >> 1) & 3
            down = bool(packed & 1)
        else:
            delta = packed >> 1
            button = 1
            down = bool(packed & 1)
        frame = delta + previous
        player2 = remaining_p1 == 0
        if input_tag:
            extension = reader.varint()
            reader.take(extension)
        inputs.append(Gdr2Input(frame=frame, button=button, player2=player2, down=down))
        previous = frame
        if remaining_p1 > 0:
            remaining_p1 -= 1
            if remaining_p1 == 0:
                previous = 0

    if len(inputs) != input_count:
        raise ValueError(f"expected {input_count} inputs, got {len(inputs)}")
    if reader.remaining() != 0:
        raise ValueError(f"trailing {reader.remaining()} bytes after inputs")

    inputs.sort(key=lambda item: item.frame)
    return Gdr2Replay(
        version=version,
        input_tag=input_tag,
        author=author,
        description=description,
        duration=duration,
        game_version=game_version,
        framerate=framerate if framerate > 0 else 240.0,
        seed=seed,
        coins=coins,
        ldm=ldm,
        platformer=platformer,
        bot_name=bot_name,
        bot_version=bot_version,
        level_id=level_id,
        level_name=level_name,
        deaths=deaths,
        inputs=inputs,
    )
