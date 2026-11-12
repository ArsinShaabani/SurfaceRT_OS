"""Minimal GPT writer (protective MBR + primary and backup headers).

Produces a layout that both the Surface RT firmware and Linux' tools accept:
LBA 0 protective MBR, LBA 1 GPT header, LBA 2..33 partition entries, then a
mirrored header/entry set at the end of the disk.
"""

import struct
import zlib

SECTOR = 512
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRY_SECTORS = (ENTRY_COUNT * ENTRY_SIZE + SECTOR - 1) // SECTOR

TYPE_ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
TYPE_LINUX_DATA = "0FC63DAF-8483-4772-8E79-3D69D8477DE4"
TYPE_MS_BASIC = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"


def guid_bytes(text):
    """Pack a GUID string into the 16 byte mixed endian GPT form."""
    parts = text.replace("{", "").replace("}", "").split("-")
    if len(parts) != 5:
        raise ValueError("invalid GUID %r" % text)
    d1 = int(parts[0], 16)
    d2 = int(parts[1], 16)
    d3 = int(parts[2], 16)
    d4 = bytes.fromhex(parts[3])
    d5 = bytes.fromhex(parts[4])
    return struct.pack("<IHH", d1, d2, d3) + d4 + d5


def _header(current, backup, first_usable, last_usable, disk_guid, entries_lba,
            entries_crc):
    hdr = bytearray(92)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<I", hdr, 8, 0x00010000)      # revision 1.0
    struct.pack_into("<I", hdr, 12, 92)             # header size
    struct.pack_into("<Q", hdr, 24, current)        # my LBA
    struct.pack_into("<Q", hdr, 32, backup)         # alternate LBA
    struct.pack_into("<Q", hdr, 40, first_usable)
    struct.pack_into("<Q", hdr, 48, last_usable)
    hdr[56:72] = disk_guid
    struct.pack_into("<Q", hdr, 72, entries_lba)
    struct.pack_into("<I", hdr, 80, ENTRY_COUNT)
    struct.pack_into("<I", hdr, 84, ENTRY_SIZE)
    struct.pack_into("<I", hdr, 88, entries_crc)
    struct.pack_into("<I", hdr, 16, zlib.crc32(bytes(hdr)) & 0xFFFFFFFF)
    return bytes(hdr)


def build(image, partitions):
    """Write a GPT into `image` (a bytearray) describing `partitions`.

    partitions: list of dicts with start (LBA), sectors, type (GUID), name.
    """
    total_sectors = len(image) // SECTOR
    first_usable = 2 + ENTRY_SECTORS
    last_usable = total_sectors - 2 - ENTRY_SECTORS

    entries = bytearray(ENTRY_COUNT * ENTRY_SIZE)
    for i, part in enumerate(partitions):
        off = i * ENTRY_SIZE
        if part["start"] < first_usable or part["start"] + part["sectors"] - 1 > last_usable:
            raise ValueError("partition %d does not fit the usable area" % i)
        entries[off:off + 16] = guid_bytes(part.get("type", TYPE_LINUX_DATA))
        entries[off + 16:off + 32] = guid_bytes(part["guid"]) if part.get("guid") \
            else bytes(range(16))
        struct.pack_into("<Q", entries, off + 32, part["start"])
        struct.pack_into("<Q", entries, off + 40, part["start"] + part["sectors"] - 1)
        struct.pack_into("<Q", entries, off + 48, 0)
        name = part.get("name", "").encode("utf-16-le")[:70]
        entries[off + 56:off + 56 + len(name)] = name

    entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
    disk_guid = guid_bytes("5352544F-0001-4000-8000-5352544F534F")

    # protective MBR
    mbr = bytearray(SECTOR)
    mbr[446] = 0x00
    mbr[450] = 0xEE
    struct.pack_into("<I", mbr, 454, 1)
    struct.pack_into("<I", mbr, 458, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[510] = 0x55
    mbr[511] = 0xAA
    image[0:SECTOR] = mbr

    primary = _header(1, total_sectors - 1, first_usable, last_usable,
                      disk_guid, 2, entries_crc)
    image[SECTOR:2 * SECTOR] = primary + bytes(SECTOR - len(primary))
    image[2 * SECTOR:(2 + ENTRY_SECTORS) * SECTOR] = entries

    backup_entries_lba = total_sectors - 1 - ENTRY_SECTORS
    image[backup_entries_lba * SECTOR:(backup_entries_lba + ENTRY_SECTORS) * SECTOR] = entries
    backup = _header(total_sectors - 1, 1, first_usable, last_usable, disk_guid,
                     backup_entries_lba, entries_crc)
    image[(total_sectors - 1) * SECTOR:total_sectors * SECTOR] =         backup + bytes(SECTOR - len(backup))
    return image


def read(image):
    """Parse a GPT image, returning a list of partition dicts."""
    if image[512:520] != b"EFI PART":
        raise ValueError("no GPT header at LBA 1")
    entries_lba = struct.unpack_from("<Q", image, 512 + 72)[0]
    count = struct.unpack_from("<I", image, 512 + 80)[0]
    size = struct.unpack_from("<I", image, 512 + 84)[0]
    out = []
    for i in range(count):
        off = entries_lba * SECTOR + i * size
        e = image[off:off + size]
        if e[:16] == bytes(16):
            continue
        out.append({
            "index": i + 1,
            "type": e[:16].hex(),
            "start": struct.unpack_from("<Q", e, 32)[0],
            "sectors": struct.unpack_from("<Q", e, 40)[0] - struct.unpack_from("<Q", e, 32)[0] + 1,
            "name": e[56:128].decode("utf-16-le", "ignore").rstrip("\x00"),
        })
    return out
