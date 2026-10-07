#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Выгрузка иконок товаров из атласов в отдельные папки + ключи icon в common/goods.txt
(для V2DLL >= 5.21, опция ENABLE_GOODS_ICONS).

Что делает:
  * читает gfx/interface/resources.dds, resources_big.dds, resources_small.dds (число кадров -
    noOfFrames из interface/core.gfx; кадр товара = его номер в goods.txt + 1);
  * для каждого товара кладёт gfx/goods/<товар>/normal|big|small.dds (или .png);
  * в блок каждого товара в common/goods.txt добавляет строку  icon = "gfx\\goods\\<товар>".

Безопасно запускать повторно: готовые файлы иконок НЕ перезаписываются (ваши правки целы, пока нет
--force), ключ icon второй раз не добавляется. Пустые кадры (например dummy_good) пропускаются.
goods.txt правится только добавлением строк (кодировка и переводы строк сохраняются);
резервной копии не делается - откат через git.

Примеры:
  python export_goods_icons.py --dry-run      # только показать, что будет сделано
  python export_goods_icons.py                # выгрузить в DDS и прописать ключи
  python export_goods_icons.py --format png   # выгрузить в PNG (удобнее править в редакторах)
  python export_goods_icons.py --no-keys      # только выгрузить файлы, goods.txt не трогать
"""
import argparse
import os
import re
import struct
import sys
import zlib

SIZES = (  # (имя размера, файл атласа в gfx/interface)
    ("normal", "resources.dds"),
    ("big", "resources_big.dds"),
    ("small", "resources_small.dds"),
)
DEFAULT_ICON_DIR = "gfx/goods"


# ------------------------------------------------------------------ DDS / PNG

def read_dds(path):
    d = open(path, "rb").read()
    if len(d) < 128 or d[:4] != b"DDS ":
        raise ValueError("%s: не DDS" % path)
    hsize, = struct.unpack_from("<I", d, 4)
    h, w = struct.unpack_from("<II", d, 12)
    mips, = struct.unpack_from("<I", d, 28)
    pf_flags, = struct.unpack_from("<I", d, 80)
    bpp, = struct.unpack_from("<I", d, 88)
    masks = struct.unpack_from("<IIII", d, 92)
    if hsize != 124 or (pf_flags & 0x4) or not (pf_flags & 0x40) or bpp != 32 or mips > 1:
        raise ValueError("%s: нужен несжатый 32-битный DDS без мип-уровней" % path)
    idx = {0x000000FF: 0, 0x0000FF00: 1, 0x00FF0000: 2, 0xFF000000: 3}
    if any(m not in idx for m in masks) or len({idx[m] for m in masks}) != 4:
        raise ValueError("%s: неподдерживаемые маски каналов" % path)
    r, g, b, a = (idx[m] for m in masks)
    raw = d[128:128 + w * h * 4]
    if len(raw) != w * h * 4:
        raise ValueError("%s: файл обрезан" % path)
    if (r, g, b, a) == (0, 1, 2, 3):
        rgba = raw
    else:
        out = bytearray(len(raw))
        for i in range(0, len(raw), 4):
            out[i], out[i + 1], out[i + 2], out[i + 3] = raw[i + r], raw[i + g], raw[i + b], raw[i + a]
        rgba = bytes(out)
    return w, h, rgba


def write_dds(path, w, h, rgba):
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<I", hdr, 4, 124)
    struct.pack_into("<I", hdr, 8, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000)    # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    struct.pack_into("<II", hdr, 12, h, w)
    struct.pack_into("<I", hdr, 20, w * 4)
    struct.pack_into("<I", hdr, 28, 1)
    struct.pack_into("<I", hdr, 76, 32)
    struct.pack_into("<I", hdr, 80, 0x41)                              # RGB | ALPHAPIXELS
    struct.pack_into("<I", hdr, 88, 32)
    struct.pack_into("<IIII", hdr, 92, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    struct.pack_into("<I", hdr, 108, 0x1000)                           # TEXTURE
    with open(path, "wb") as f:
        f.write(bytes(hdr) + rgba)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, dat):
        c = struct.pack(">I", len(dat)) + t + dat
        return c + struct.pack(">I", zlib.crc32(t + dat) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def cell_of(rgba, w, h, frames, index):
    cw = w // frames
    return cw, h, b"".join(rgba[(y * w + index * cw) * 4:(y * w + (index + 1) * cw) * 4] for y in range(h))


def is_empty(cell_rgba):
    return not any(cell_rgba[3::4])


# ------------------------------------------------------------------ core.gfx / goods.txt

def frames_from_core_gfx(mod, atlas_file):
    """noOfFrames блока, у которого texturefile заканчивается на interface/<atlas_file>."""
    for gfx in ("interface/core.gfx", "interface/mapitems.gfx"):
        p = os.path.join(mod, gfx)
        if not os.path.exists(p):
            continue
        t = open(p, encoding="cp1251", errors="replace").read()
        for m in re.finditer(r'texturefile\s*=\s*"([^"]*)"', t, re.I):
            path = re.sub(r"[\\/]+", "/", m.group(1)).lower()
            if not path.endswith("interface/" + atlas_file.lower()):
                continue
            start = t.rfind("{", 0, m.start())
            end = t.find("}", m.end())
            block = t[start:end if end != -1 else len(t)]
            n = re.search(r"noofframes\s*=\s*(\d+)", block, re.I)
            if n and int(n.group(1)) > 0:
                return int(n.group(1))
    return 0


def is_delim(c):
    return c in b" \t\r\n{}=#\""


def parse_goods(data):
    """Товары в порядке файла (как их нумерует движок).
    -> список dict(name, open_end, line_end, inline, icon): open_end - позиция сразу после '{' товара,
       icon - значение ключа icon или None."""
    n = len(data)
    goods = []
    depth = 0
    i = 0

    def skip_ws(j):
        while j < n:
            c = data[j]
            if c == 0x23:                                  # '#'
                while j < n and data[j] != 0x0A:
                    j += 1
            elif c in b" \t\r\n":
                j += 1
            else:
                break
        return j

    while True:
        i = skip_ws(i)
        if i >= n:
            break
        c = data[i]
        if c == 0x7B:
            depth += 1
            i += 1
            continue
        if c == 0x7D:
            depth = max(0, depth - 1)
            i += 1
            continue
        if c == 0x3D:
            i += 1
            continue
        if c == 0x22:
            i += 1
            while i < n and data[i] != 0x22:
                i += 1
            i += 1
            continue
        s = i
        while i < n and not is_delim(data[i]):
            i += 1
        e = i
        j = skip_ws(e)
        if j >= n or data[j] != 0x3D:
            continue
        j = skip_ws(j + 1)
        if j < n and data[j] == 0x7B:
            if depth == 1:
                eol = data.find(b"\n", j)
                eol = n if eol == -1 else eol
                rest = data[j + 1:eol]
                rest = re.sub(rb"#.*", b"", rest).strip()
                goods.append(dict(name=data[s:e].decode("ascii", "replace"), open_end=j + 1,
                                  line_end=eol, inline=bool(rest), icon=None))
            i = j
            continue
        if j < n and data[j] == 0x22:
            vs = j + 1
            ve = vs
            while ve < n and data[ve] != 0x22:
                ve += 1
            end = ve + 1
        else:
            vs = ve = j
            while ve < n and not is_delim(data[ve]):
                ve += 1
            end = ve
        if depth == 2 and goods and data[s:e].lower() == b"icon" and goods[-1]["icon"] is None:
            goods[-1]["icon"] = data[vs:ve].decode("cp1251", "replace")
        i = end
    return goods


def norm_path(p):
    p = re.sub(r"[\\/]+", "/", p.strip())
    while p.startswith("./"):
        p = p[2:]
    return p.strip("/")


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description="Выгрузка иконок товаров в папки и ключи icon в goods.txt")
    ap.add_argument("--mod", default=os.path.dirname(os.path.abspath(__file__)), help="папка мода (по умолчанию - рядом со скриптом)")
    ap.add_argument("--format", choices=("dds", "png"), default="dds", help="формат файлов иконок (по умолчанию dds)")
    ap.add_argument("--dry-run", action="store_true", help="ничего не писать, только показать")
    ap.add_argument("--force", action="store_true", help="перезаписывать уже существующие файлы иконок")
    ap.add_argument("--no-keys", action="store_true", help="не трогать common/goods.txt")
    args = ap.parse_args()

    mod = args.mod
    goods_path = os.path.join(mod, "common", "goods.txt")
    data = open(goods_path, "rb").read()
    goods = parse_goods(data)
    if not goods:
        sys.exit("в common/goods.txt не найдено ни одного товара")
    nl = b"\r\n" if b"\r\n" in data else b"\n"

    atlases = {}
    frames = 0
    for size, fname in SIZES:
        path = os.path.join(mod, "gfx", "interface", fname)
        w, h, rgba = read_dds(path)
        n = frames_from_core_gfx(mod, fname)
        if n <= 1 or w % n:
            sys.exit("%s: не удалось определить число кадров (noOfFrames=%d, ширина %d)" % (fname, n, w))
        if frames and n != frames:
            print("внимание: у атласов разное число кадров (%d и %d)" % (frames, n))
        frames = max(frames, n)
        atlases[size] = (w, h, rgba, n)
    print("атласы прочитаны; товаров в goods.txt: %d, кадров в атласах: %d" % (len(goods), frames))

    ext = args.format
    exported = skipped_exist = skipped_empty = 0
    new_keys = []                      # (позиция в файле, текст вставки)
    for idx, g in enumerate(goods):
        name = g["name"]
        frame = idx + 1
        if frame >= frames:
            print("  %-22s кадр %d за пределами атласа - пропущен" % (name, frame))
            continue
        cells = {}
        for size, _ in SIZES:
            w, h, rgba, n = atlases[size]
            cells[size] = cell_of(rgba, w, h, n, frame)
        if all(is_empty(c[2]) for c in cells.values()):
            skipped_empty += 1
            print("  %-22s кадр %d пустой - пропущен" % (name, frame))
            continue

        folder = norm_path(g["icon"]) if g["icon"] else "%s/%s" % (DEFAULT_ICON_DIR, name)
        out_dir = os.path.join(mod, *folder.split("/"))
        for size, _ in SIZES:
            cw, ch, px = cells[size]
            target = os.path.join(out_dir, "%s.%s" % (size, ext))
            if os.path.exists(target) and not args.force:
                skipped_exist += 1
                continue
            exported += 1
            if args.dry_run:
                continue
            os.makedirs(out_dir, exist_ok=True)
            (write_dds if ext == "dds" else write_png)(target, cw, ch, px)

        if g["icon"] is None and not args.no_keys:
            value = ("gfx\\\\goods\\\\%s" % name).encode("ascii")
            if g["inline"]:
                new_keys.append((g["open_end"], b' icon = "' + value + b'"'))
            else:
                after = data[g["line_end"] + 1:g["line_end"] + 200]
                m = re.match(rb"[ \t]*", after)
                indent = m.group(0) if m and after[m.end():m.end() + 1] not in (b"", b"\r", b"\n", b"}") else b"\t\t"
                new_keys.append((g["line_end"] if data[g["line_end"] - 1:g["line_end"]] != b"\r" else g["line_end"] - 1,
                                 nl + indent + b'icon = "' + value + b'"'))

    if new_keys and not args.dry_run:
        out = bytearray(data)
        for pos, text in sorted(new_keys, reverse=True):
            out[pos:pos] = text
        with open(goods_path, "wb") as f:
            f.write(bytes(out))

    print()
    print("%sфайлов иконок записано: %d, уже существовало (не тронуто): %d, пустых кадров пропущено: %d, ключей icon добавлено: %d"
          % ("[DRY-RUN, ничего не записано] " if args.dry_run else "", exported, skipped_exist, skipped_empty, len(new_keys)))


if __name__ == "__main__":
    main()
