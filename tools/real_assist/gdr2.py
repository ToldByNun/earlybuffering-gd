"""Minimal GDR2 reader/writer matching libGDR / EbReplayImport (classic Jump packing)."""

from __future__ import annotations

import struct
from dataclasses import dataclass, field, replace
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
    extension: bytes = field(default_factory=bytes)


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


class _Writer:
    def __init__(self) -> None:
        self._buf = bytearray()

    def raw(self, data: bytes) -> None:
        self._buf.extend(data)

    def varint(self, value: int) -> None:
        if value < 0:
            raise ValueError("negative varint")
        if value == 0:
            self._buf.append(0)
            return
        while value > 0:
            byte = value & 0x7F
            value >>= 7
            if value > 0:
                byte |= 0x80
            self._buf.append(byte)

    def string(self, text: str) -> None:
        data = text.encode("utf-8")
        if len(data) > 0xFFFF:
            raise ValueError("string too long")
        self.varint(len(data))
        self.raw(data)

    def f32(self, value: float) -> None:
        self.raw(struct.pack(">f", float(value)))

    def f64(self, value: float) -> None:
        self.raw(struct.pack(">d", float(value)))

    def boolean(self, value: bool) -> None:
        self.varint(1 if value else 0)

    def finish(self) -> bytes:
        return bytes(self._buf)


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
    extension = reader.take(extension_size)

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
            ext_size = reader.varint()
            reader.take(ext_size)
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

    inputs.sort(key=lambda item: (item.frame, item.player2, item.button, 0 if item.down else 1))
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
        extension=extension,
    )


def export_gdr2(replay: Gdr2Replay, path: Path) -> None:
    writer = _Writer()
    writer.raw(b"GDR")
    writer.varint(replay.version)
    writer.string(replay.input_tag)
    writer.string(replay.author)
    writer.string(replay.description)
    writer.f32(replay.duration)
    writer.varint(replay.game_version)
    writer.f64(replay.framerate if replay.framerate > 0 else 240.0)
    writer.varint(replay.seed)
    writer.varint(replay.coins)
    writer.boolean(replay.ldm)
    writer.boolean(replay.platformer)
    writer.string(replay.bot_name)
    writer.varint(replay.bot_version)
    writer.varint(replay.level_id)
    writer.string(replay.level_name)

    extension = replay.extension or b""
    writer.varint(len(extension))
    writer.raw(extension)

    writer.varint(len(replay.deaths))
    previous = 0
    for death in replay.deaths:
        writer.varint(death - previous)
        previous = death

    inputs = sorted(
        replay.inputs, key=lambda item: (item.frame, item.player2, item.button, 0 if item.down else 1)
    )
    p1 = [item for item in inputs if not item.player2]
    p2 = [item for item in inputs if item.player2]
    writer.varint(len(inputs))
    writer.varint(len(p1))

    def write_stream(stream: list[Gdr2Input]) -> None:
        previous_frame = 0
        for item in stream:
            delta = item.frame - previous_frame
            if delta < 0:
                raise ValueError("input stream not sorted")
            if replay.platformer:
                packed = (delta << 3) | ((item.button & 3) << 1) | (1 if item.down else 0)
            else:
                packed = (delta << 1) | (1 if item.down else 0)
            writer.varint(packed)
            if replay.input_tag:
                writer.varint(0)
            previous_frame = item.frame

    write_stream(p1)
    write_stream(p2)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(writer.finish())


def clone_replay(replay: Gdr2Replay, inputs: list[Gdr2Input] | None = None) -> Gdr2Replay:
    return replace(
        replay,
        inputs=list(inputs if inputs is not None else replay.inputs),
        deaths=list(replay.deaths),
        extension=bytes(replay.extension),
    )
