#!/usr/bin/env python3
# ================================================================================================
# File: ui_resources.py
# Brief: Read checked native resource tables without extracting or trusting resource paths.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

MAX_TABLE_BYTES = 32 * 1024 * 1024
MAX_RESOURCE_BYTES = 64 * 1024 * 1024
MAGIC = 0xD000000D


class ResourceError(ValueError):
    pass


def logical_name(name: str) -> str:
    name = name.replace("\\", "/").lower()
    if (not name or len(name.encode("utf-8")) > 255 or name.startswith("/") or name.endswith("/") or
            any(part in (".", "..") for part in name.split("/")) or
            any(ord(char) < 32 or ord(char) > 126 or char == ":" for char in name)):
        raise ResourceError("resource name must be a bounded relative ASCII path")
    # Retail _sound_pc contains generated//sound entries; filesystems treat the
    # redundant internal separator as the same output path. Never collapse '..'.
    return "/".join(part for part in name.split("/") if part)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def identity(path: Path) -> tuple[int, int, int]:
    info = path.stat()
    return info.st_ino, info.st_size, info.st_mtime_ns


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    length: int
    ordinal: int


class Container:
    def __init__(self, path: Path):
        self.path = path.resolve()
        self.identity = identity(self.path)
        size = self.identity[1]
        with self.path.open("rb") as stream:
            header = stream.read(12)
            if len(header) != 12:
                raise ResourceError("truncated resource header")
            magic, offset, length = struct.unpack(">Iii", header)
            if magic != MAGIC:
                raise ResourceError("wrong resource magic")
            if not (12 <= offset <= size and 4 <= length <= min(size - offset, MAX_TABLE_BYTES)):
                raise ResourceError("resource table outside bounded file")
            stream.seek(offset)
            table = stream.read(length)
        if len(table) != length:
            raise ResourceError("short resource table read")
        count = struct.unpack_from(">i", table)[0]
        if not 0 <= count <= min(1_000_000, (length - 4) // 13):
            raise ResourceError("invalid resource count")
        cursor = 4
        entries = []
        for ordinal in range(count):
            if cursor + 4 > length:
                raise ResourceError("truncated resource name length")
            name_bytes = struct.unpack_from("<i", table, cursor)[0]
            cursor += 4
            if not 1 <= name_bytes <= 255 or name_bytes + 8 > length - cursor:
                raise ResourceError("invalid resource name length")
            try:
                name = logical_name(table[cursor:cursor + name_bytes].decode("ascii"))
            except UnicodeDecodeError as error:
                raise ResourceError("non-ASCII resource name") from error
            cursor += name_bytes
            start, byte_count = struct.unpack_from(">ii", table, cursor)
            cursor += 8
            if start < 12 or byte_count < 0 or start > size or byte_count > size - start:
                raise ResourceError("resource payload outside file")
            if byte_count and start < offset + length and start + byte_count > offset:
                raise ResourceError("resource payload overlaps table")
            entries.append(Entry(name, start, byte_count, ordinal))
        if cursor != length:
            raise ResourceError("resource table has trailing bytes")
        previous = None
        for entry in sorted((item for item in entries if item.length), key=lambda item: (item.offset, item.length)):
            if previous and entry.offset < previous.offset + previous.length and (entry.offset, entry.length) != (previous.offset, previous.length):
                raise ResourceError("resource payloads partially overlap")
            previous = entry
        self.entries = entries
        self.sha256 = file_digest(self.path)
        self.table_offset, self.table_bytes = offset, length
        self._unchanged()

    def _unchanged(self):
        if identity(self.path) != self.identity:
            raise ResourceError("resource container changed during inventory")

    def read(self, entry: Entry, max_bytes: int = MAX_RESOURCE_BYTES) -> bytes:
        if not 0 <= entry.length <= max_bytes:
            raise ResourceError("resource exceeds read budget: " + entry.name)
        self._unchanged()
        with self.path.open("rb") as stream:
            stream.seek(entry.offset)
            data = stream.read(entry.length)
        self._unchanged()
        if len(data) != entry.length:
            raise ResourceError("short resource payload read: " + entry.name)
        return data

    def summary(self) -> dict:
        return {"name": self.path.name, "bytes": self.identity[1], "sha256": self.sha256,
                "entries": len(self.entries), "table_offset": self.table_offset, "table_bytes": self.table_bytes}


class ResourceCatalog:
    """Use last container/record priority over normalized extraction names."""

    def __init__(self, paths: list[Path]):
        if not paths or len(paths) > 255 or len({path.resolve() for path in paths}) != len(paths):
            raise ResourceError("require 1..255 distinct resource containers in load order")
        if len({path.name for path in paths}) != len(paths):
            raise ResourceError("container basenames must be unique for stable manifest identities")
        self.containers = [Container(path) for path in paths]
        self.candidates = {}
        for container in self.containers:
            for entry in container.entries:
                self.candidates.setdefault(entry.name, []).append((container, entry))

    def names(self) -> list[str]:
        return sorted(self.candidates)

    def read(self, name: str, max_bytes: int = MAX_RESOURCE_BYTES) -> bytes:
        name = logical_name(name)
        try:
            container, entry = self.candidates[name][-1]
        except KeyError as error:
            raise ResourceError("resource not found: " + name) from error
        return container.read(entry, max_bytes)

    def describe(self, name: str) -> dict:
        name = logical_name(name)
        if name not in self.candidates:
            raise ResourceError("resource not found: " + name)
        copies = [{"container": container.path.name, "ordinal": entry.ordinal,
                   "offset": entry.offset, "bytes": entry.length, "sha256": digest(container.read(entry))}
                  for container, entry in self.candidates[name]]
        return {"name": name, "selected": copies[-1], "copies": copies,
                "duplicate_content_identical": len({item["sha256"] for item in copies}) == 1}
