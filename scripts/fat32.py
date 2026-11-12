"""FAT32 image builder and reader - no mtools, no sgdisk, pure Python.

Used by scripts/mkimage.py to build the EFI system partition, and by the test
suite to cross-check the kernel's FAT driver against an independent parser.

Supported: 8.3 names, files, nested directories, 512 byte sectors and power of
two cluster sizes.  That is exactly what an ESP needs, and keeping the builder
this small makes it auditable.
"""

import struct

ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20
FAT_EOC = 0x0FFFFFFF


class Fat32Error(Exception):
    pass


def short_name(name):
    if name == ".":
        return b".          "
    if name == "..":
        return b"..         "
    name = name.upper()
    base, _, ext = name.partition(".")
    if len(base) > 8 or len(ext) > 3 or not base:
        raise Fat32Error("name %r does not fit 8.3" % name)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def _fits_83(name):
    base, _, ext = name.upper().partition(".")
    return bool(base) and len(base) <= 8 and len(ext) <= 3


def make_alias(name, used):
    """Build a unique 8.3 alias for a long name (MICROSOFT -> MICROS~1)."""
    base, _, ext = name.upper().partition(".")
    cleaned = "".join(c for c in base if c.isalnum() or c in "_-$")
    stem = cleaned[:6] or "FILE"
    for n in range(1, 100):
        alias = "%s~%d" % (stem, n)
        candidate = alias + (("." + ext[:3]) if ext else "")
        if candidate not in used:
            used.add(candidate)
            return candidate
    raise Fat32Error("too many long names in one directory")


def lfn_checksum(name11):
    total = 0
    for b in name11:
        total = (((total & 1) << 7) + (total >> 1) + b) & 0xFF
    return total


def lfn_entries(name11, longname):
    """Encode a long file name as the LFN entry chain that precedes the 8.3 one."""
    chars = longname.encode("utf-16-le")
    units = [chars[i:i + 2] for i in range(0, len(chars), 2)]
    if len(units) % 13:
        units.append(b"\x00\x00")
        while len(units) % 13:
            units.append(b"\xff\xff")
    blocks = [units[i:i + 13] for i in range(0, len(units), 13)]
    chk = lfn_checksum(name11)
    out = bytearray()
    for i, block in enumerate(reversed(blocks)):
        seq = len(blocks) - i
        if i == 0:
            seq |= 0x40
        e = bytearray(32)
        e[0] = seq
        e[1:11] = b"".join(block[0:5])
        e[11] = 0x0F
        e[12] = 0
        e[13] = chk
        e[14:26] = b"".join(block[5:11])
        e[26:28] = b"\x00\x00"
        e[28:32] = b"".join(block[11:13])
        out += bytes(e)
    return bytes(out)


class Fat32Builder:
    def __init__(self, total_bytes, cluster_bytes=512, reserved_sectors=32,
                 num_fats=2, label="SRTOS"):
        if cluster_bytes % 512 or (cluster_bytes // 512) & (cluster_bytes // 512 - 1):
            raise Fat32Error("cluster size must be 512 * power of two")
        self.sector = 512
        self.total_bytes = total_bytes - (total_bytes % self.sector)
        self.spc = cluster_bytes // self.sector
        self.cluster_bytes = cluster_bytes
        self.reserved = reserved_sectors
        self.num_fats = num_fats
        self.label = label[:11].ljust(11)

        total_sectors = self.total_bytes // self.sector
        fat_sectors = 1
        while True:
            data_sectors = total_sectors - self.reserved - self.num_fats * fat_sectors
            clusters = data_sectors // self.spc
            need = ((clusters + 2) * 4 + self.sector - 1) // self.sector
            if need <= fat_sectors:
                self.fat_sectors = fat_sectors
                self.clusters = clusters
                break
            fat_sectors = need
            if fat_sectors > 8192:
                raise Fat32Error("image too large for this cluster size")
        if self.clusters < 65525:
            raise Fat32Error(
                "FAT32 needs >= 65525 clusters, got %d - use smaller clusters "
                "or a bigger partition" % self.clusters)

        self.data_start = self.reserved + self.num_fats * self.fat_sectors
        self.image = bytearray(self.total_bytes)
        self.fat = [0] * (self.clusters + 2)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = 0x0FFFFFFF
        self.next_cluster = 2
        self.root = {"name": "", "dir": True, "children": {}, "cluster": 2}
        self._alloc(1)  # cluster 2 holds the root directory
        self.file_count = 0
        self.dir_count = 1

    # ------------------------------------------------------------ clusters --
    def _alloc(self, count):
        start = self.next_cluster
        if start + count > self.clusters + 1:
            raise Fat32Error("out of clusters")
        for i in range(count):
            self.fat[start + i] = FAT_EOC if i == count - 1 else start + i + 1
        self.next_cluster += count
        return start

    def _cluster_off(self, cluster):
        return (self.data_start + (cluster - 2) * self.spc) * self.sector

    def _write_clusters(self, first, data):
        cluster = first
        pos = 0
        total = max(len(data), 1)
        while pos < total:
            off = self._cluster_off(cluster)
            chunk = data[pos:pos + self.cluster_bytes]
            self.image[off:off + len(chunk)] = chunk
            pos += self.cluster_bytes
            if pos < total:
                cluster = self.fat[cluster]
                if cluster >= FAT_EOC:
                    break

    # ---------------------------------------------------------------- tree --
    def _node(self, path, create=False):
        node = self.root
        for part in [p for p in path.strip("/").split("/") if p]:
            if part not in node["children"]:
                if not create:
                    raise Fat32Error("no such path %r" % path)
                node["children"][part] = {
                    "name": part, "dir": True, "children": {}, "cluster": None}
                self.dir_count += 1
            node = node["children"][part]
            if not node["dir"]:
                raise Fat32Error("%r is a file, not a directory" % path)
        return node

    def mkdir(self, path):
        self._node(path, create=True)

    def add_file(self, path, data):
        path = path.strip("/")
        parent, _, name = path.rpartition("/")
        node = self._node(parent, create=True)
        if name in node["children"]:
            raise Fat32Error("%r already exists" % path)
        node["children"][name] = {
            "name": name, "dir": False, "data": bytes(data), "cluster": None}
        self.file_count += 1

    def _entry(self, name11, attr, cluster, size):
        e = bytearray(32)
        e[0:11] = name11
        e[11] = attr
        struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", e, 26, cluster & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    def _write_dir(self, node, parent_cluster):
        entries = bytearray()
        if node["cluster"] is None:
            node["cluster"] = self._alloc(1)
        if node["name"]:
            entries += self._entry(short_name("."), ATTR_DIR, node["cluster"], 0)
            entries += self._entry(short_name(".."), ATTR_DIR, parent_cluster, 0)
        used = set()
        for child in sorted(node["children"].values(),
                            key=lambda c: (not c["dir"], c["name"])):
            if _fits_83(child["name"]):
                name11 = short_name(child["name"])
                used.add(child["name"].upper())
            else:
                alias = make_alias(child["name"], used)
                name11 = short_name(alias)
                entries += lfn_entries(name11, child["name"])
            entries += self._entry(name11,
                                   ATTR_DIR if child["dir"] else ATTR_ARCHIVE,
                                   child["cluster"] or 0,
                                   0 if child["dir"] else len(child["data"]))
        need = max(1, (len(entries) + self.cluster_bytes - 1) // self.cluster_bytes)
        cluster = node["cluster"]
        have = 1
        while have < need:
            nxt = self._alloc(1)
            self.fat[cluster] = nxt
            cluster = nxt
            have += 1
        self._write_clusters(node["cluster"], bytes(entries))

    # --------------------------------------------------------------- build --
    def build(self):
        def alloc_files(node):
            for child in node["children"].values():
                if child["dir"]:
                    alloc_files(child)
                else:
                    count = max(1, (len(child["data"]) + self.cluster_bytes - 1)
                                // self.cluster_bytes)
                    child["cluster"] = self._alloc(count)

        def write_files(node):
            for child in node["children"].values():
                if child["dir"]:
                    write_files(child)
                else:
                    self._write_clusters(child["cluster"], child["data"])

        def write_dirs(node, parent):
            for child in node["children"].values():
                if child["dir"]:
                    write_dirs(child, node["cluster"] or 0)
            self._write_dir(node, parent)

        alloc_files(self.root)
        write_files(self.root)
        write_dirs(self.root, 0)
        self._write_fat()
        self._write_boot_sector()
        return bytes(self.image)

    def _write_boot_sector(self):
        bs = bytearray(512)
        bs[0:3] = b"\xEB\x58\x90"
        bs[3:11] = b"SRTOS   "
        struct.pack_into("<H", bs, 11, self.sector)
        bs[13] = self.spc
        struct.pack_into("<H", bs, 14, self.reserved)
        bs[16] = self.num_fats
        struct.pack_into("<H", bs, 17, 0)
        struct.pack_into("<H", bs, 19, 0)
        bs[21] = 0xF8
        struct.pack_into("<H", bs, 22, 0)
        struct.pack_into("<H", bs, 24, 32)     # sectors per track
        struct.pack_into("<H", bs, 26, 64)     # heads
        struct.pack_into("<I", bs, 28, 0)      # hidden sectors
        struct.pack_into("<I", bs, 32, self.total_bytes // self.sector)
        struct.pack_into("<I", bs, 36, self.fat_sectors)
        struct.pack_into("<H", bs, 40, 0)      # ext flags
        struct.pack_into("<H", bs, 42, 0)      # fs version
        struct.pack_into("<I", bs, 44, 2)      # root cluster
        struct.pack_into("<H", bs, 48, 1)      # FSInfo sector
        struct.pack_into("<H", bs, 50, 6)      # backup boot sector
        bs[64] = 0x80
        bs[66] = 0x29
        struct.pack_into("<I", bs, 67, 0x5352544F)
        bs[71:82] = self.label.encode("ascii")
        bs[82:90] = b"FAT32   "
        bs[510] = 0x55
        bs[511] = 0xAA
        self.image[0:512] = bs
        self.image[6 * 512:7 * 512] = bs

        fsinfo = bytearray(512)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488,
                         max(0, self.clusters - self.next_cluster + 2))
        struct.pack_into("<I", fsinfo, 492, self.next_cluster)
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)
        self.image[1 * 512:2 * 512] = fsinfo
        self.image[7 * 512:8 * 512] = fsinfo

        return bytes(self.image)

    def _write_fat(self):
        blob = b"".join(struct.pack("<I", v) for v in self.fat)
        for i in range(self.num_fats):
            off = (self.reserved + i * self.fat_sectors) * self.sector
            self.image[off:off + len(blob)] = blob



class Fat32Reader:
    """Independent FAT32 parser: used by the test suite and by the simulator."""

    def __init__(self, data):
        self.data = data
        bs = data[0:512]
        if bs[510] != 0x55 or bs[511] != 0xAA:
            raise Fat32Error("no FAT boot sector")
        self.sector = struct.unpack_from("<H", bs, 11)[0]
        self.spc = bs[13]
        self.reserved = struct.unpack_from("<H", bs, 14)[0]
        self.num_fats = bs[16]
        self.fat_sectors = struct.unpack_from("<I", bs, 36)[0]
        self.root_cluster = struct.unpack_from("<I", bs, 44)[0]
        self.cluster_bytes = self.sector * self.spc
        self.fat_off = self.reserved * self.sector
        self.data_off = (self.reserved + self.num_fats * self.fat_sectors) * self.sector

    def _next(self, cluster):
        return struct.unpack_from("<I", self.data,
                                  self.fat_off + cluster * 4)[0] & 0x0FFFFFFF

    def _chain(self, first, size=None):
        out = bytearray()
        cluster = first
        guard = 0
        while 2 <= cluster < FAT_EOC and guard < (1 << 20):
            off = self.data_off + (cluster - 2) * self.cluster_bytes
            out += self.data[off:off + self.cluster_bytes]
            cluster = self._next(cluster)
            guard += 1
        return bytes(out[:size]) if size is not None else bytes(out)

    def _entries(self, cluster):
        raw = self._chain(cluster)
        out = []
        long_parts = []
        for off in range(0, len(raw) - 31, 32):
            e = raw[off:off + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                long_parts = []
                continue
            if e[11] == 0x0F:
                chunk = e[1:11] + e[14:26] + e[28:32]
                long_parts.insert(0, chunk)
                continue
            if e[11] == 0x08:
                continue
            base = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            if long_parts:
                name = b"".join(long_parts).decode("utf-16-le")
                name = name.split("\x00")[0].replace("￿", "")
                long_parts = []
            else:
                name = base + ("." + ext if ext else "")
            if name in (".", ".."):
                continue
            hi = struct.unpack_from("<H", e, 20)[0]
            lo = struct.unpack_from("<H", e, 26)[0]
            out.append((name, (hi << 16) | lo, bool(e[11] & ATTR_DIR),
                        struct.unpack_from("<I", e, 28)[0]))
        return out

    def listdir(self, path="/"):
        node = self.root_cluster
        for part in [p for p in path.strip("/").split("/") if p]:
            hit = [c for n, c, d, s in self._entries(node)
                   if n.lower() == part.lower() and d]
            if not hit:
                raise Fat32Error("no such directory %r" % path)
            node = hit[0]
        return [(n, d, s) for n, c, d, s in self._entries(node)]

    def read(self, path):
        parts = [p for p in path.strip("/").split("/") if p]
        node = self.root_cluster
        for i, part in enumerate(parts):
            last = i == len(parts) - 1
            hit = None
            for name, cluster, is_dir, size in self._entries(node):
                if name.lower() != part.lower():
                    continue
                if is_dir and not last:
                    hit = ("dir", cluster, 0)
                elif not is_dir and last:
                    hit = ("file", cluster, size)
            if hit is None:
                raise Fat32Error("no such path %r" % path)
            if hit[0] == "dir":
                node = hit[1]
            else:
                return self._chain(hit[1], hit[2])
        raise Fat32Error("%r is a directory" % path)
