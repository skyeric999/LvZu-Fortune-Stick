#!/usr/bin/env python3
"""生成应用专用的 CJK 位图字体子集（LVGL 9 fmt_txt 格式）。

为什么不用 lv_font_conv：见 docs/reference/shinku-chen/lvgl-cjk-font-subsets.md——
其在新版 Node.js 下会写出损坏位图。本工具用 Pillow/FreeType 光栅化并直接产出
LVGL C 源，且在写盘前把每个字形按 LVGL 读取方式逐像素解码回验。

字符清单来源：
  1. main/*.c/.h 字符串字面量中的全部非 ASCII 字符（含签文数据）；
  2. 可打印 ASCII（0x20..0x7E）与 U+3000 全角空格。
  app_font_26 仅取 ui_shengbei.c（只有该文件用大字号），控制 Flash 占用。

清单持久化在 tools/font_inventory_*.txt：既是覆盖门禁基线，也保证内容裁剪后
重新生成不会丢字。--check 模式扫描源码并与清单比对，缺字时以非零码退出。

位图编码（对照 managed_components/lvgl__lvgl/src/font/fmt_txt/lv_font_fmt_txt.c
的 bpp==4 PLAIN 解码路径）：连续半字节流、高半字节在前、stride=0（行间不补位）、
每个字形起始于字节边界（bitmap_index 即字节偏移）。

用法：
  python3 tools/gen_font_subset.py --font <ttf/otf>            # 生成两套字体
  python3 tools/gen_font_subset.py --check                     # 仅做覆盖门禁
生成：
  main/assets/fonts/app_font_16.c
  main/assets/fonts/app_font_26.c
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
FONTS_DIR = MAIN / "assets" / "fonts"
INVENTORY_DIR = ROOT / "tools"

STRING_LIT_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
ASCII_START, ASCII_END = 0x20, 0x7E
EXTRA_CPS = [0x3000]  # 全角空格需要带全宽步进的空白字形

# 每套字体都用全部源码字符：26px 会渲染模型层动态文本（首页状态大字/结果页
# 杯名来自 shengbei_model.c），只扫 ui_shengbei.c 会缺字成方框。
# 布局参数：格子高 = px+2（含两行降部），基线在格子第 px 行，ofs_y = 0
# （ofs_y 从行顶起算 → 位图顶即行顶），line_height = px+leading，base_line = leading。
FONTS = [
    {"name": "app_font_16", "px": 16, "leading": 3, "scan": "all"},
    {"name": "app_font_26", "px": 26, "leading": 4, "scan": "all"},
]


def scan_strings(paths: list[Path]) -> set[int]:
    """收集给定源文件字符串字面量中的全部非 ASCII 码点。"""
    cps: set[int] = set()
    for path in paths:
        text = path.read_text(encoding="utf-8")
        for lit in STRING_LIT_RE.findall(text):
            lit = lit.replace("\\n", "\n")
            for ch in lit:
                cp = ord(ch)
                if cp > ASCII_END:
                    cps.add(cp)
    return cps


def scan_files() -> dict[str, set[int]]:
    srcs_all = sorted(list(MAIN.glob("*.c")) + list(MAIN.glob("*.h")))
    srcs_ui = [MAIN / "ui_shengbei.c"]
    for p in srcs_ui:
        if not p.is_file():
            sys.exit(f"ERROR: missing {p}")
    return {"all": scan_strings(srcs_all), "ui_only": scan_strings(srcs_ui)}


def build_inventory() -> dict[str, list[int]]:
    found = scan_files()
    inv: dict[str, list[int]] = {}
    for spec in FONTS:
        cps = set(range(ASCII_START, ASCII_END + 1)) | set(EXTRA_CPS)
        if spec["scan"] == "all":
            cps |= found["all"]
        else:
            cps |= found["ui_only"]
        inv[spec["name"]] = sorted(cps)
    return inv


def load_inventory() -> dict[str, list[int]]:
    result = {}
    for spec in FONTS:
        f = INVENTORY_DIR / f"font_inventory_{spec['name']}.txt"
        if not f.is_file():
            sys.exit(f"ERROR: inventory missing: {f}（先运行生成模式）")
        result[spec["name"]] = [int(x) for x in f.read_text().split()]
    return result


def check_coverage() -> int:
    inv = load_inventory()
    found = scan_files()
    bad = 0
    all_avail = set(inv["app_font_16"])
    for cp in sorted(found["all"]):
        if cp not in all_avail:
            print(f"ERROR: U+{cp:04X} {chr(cp)!r} 在 main/ 源码字符串中出现，但不在字体清单里")
            bad += 1
    return bad


# ---------------------------------------------------------------------------
# 光栅化与位图
# ---------------------------------------------------------------------------

def rasterize(font_path: Path, px: int, cps: list[int]) -> dict[int, dict]:
    """把每个码点光栅化到 (px+2) 高、advance 宽的格子，基线在第 px 行。"""
    from PIL import Image, ImageDraw, ImageFont

    # 源字体 cmap 覆盖检查：缺映射的码点会被 FreeType 渲染成 .notdef 方框
    # （如 U+25B8 ▸），逐像素回验无法发现。这里直接硬失败，逼出替换方案。
    from fontTools.ttLib import TTFont
    tt = TTFont(str(font_path))
    have = set()
    for table in tt["cmap"].tables:
        if table.isUnicode():
            have |= set(table.cmap.keys())
    missing = [cp for cp in cps if cp not in have]
    if missing:
        detail = ", ".join(f"U+{cp:04X} {chr(cp)!r}" for cp in missing)
        sys.exit(f"ERROR: 源字体 {font_path.name} 缺字形映射，会渲染成 .notdef 方框: {detail}")

    cell_h = px + 2
    baseline = px  # 格子内基线行
    font = ImageFont.truetype(str(font_path), px)
    glyphs: dict[int, dict] = {}
    for cp in cps:
        ch = chr(cp)
        adv = int(round(font.getlength(ch)))
        adv = max(1, min(adv, px + 8))
        img = Image.new("L", (adv, cell_h), 0)
        draw = ImageDraw.Draw(img)
        try:
            draw.text((0, baseline), ch, font=font, fill=255, anchor="ls")
        except (ValueError, OSError):
            # 个别字符（如 U+3000）无轮廓也必须保留步进
            pass
        glyphs[cp] = {"adv": adv, "w": adv, "h": cell_h, "pixels": list(img.getdata())}
    return glyphs


def pack_4bpp(pixels: list[int], w: int, h: int) -> bytes:
    """连续半字节流：像素 i → 字节 i>>1，偶数位放高半字节（同 LVGL 解码）。"""
    n = w * h
    out = bytearray((n + 1) // 2)
    for i in range(n):
        v = (pixels[i] + 8) // 17  # 0..255 → 0..15
        if i % 2 == 0:
            out[i // 2] |= (v & 0xF) << 4
        else:
            out[i // 2] |= v & 0xF
    return bytes(out)


def verify_glyphs(glyphs: dict[int, dict], packed: dict[int, bytes]) -> int:
    """按 LVGL bpp==4 PLAIN / stride==0 的读取方式解码，逐像素回验。"""
    bad = 0
    for cp, g in glyphs.items():
        blob = packed[cp]
        w, h = g["w"], g["h"]
        for i in range(w * h):
            b = blob[i // 2]
            v = (b >> 4) if i % 2 == 0 else (b & 0xF)
            expect = (g["pixels"][i] + 8) // 17
            if v != expect:
                print(f"ERROR: U+{cp:04X} 像素#{i} 编码 {v} != 光栅 {expect}")
                bad += 1
    return bad


# ---------------------------------------------------------------------------
# C 生成
# ---------------------------------------------------------------------------

def emit_c(spec: dict, cps: list[int], glyphs: dict[int, dict],
           packed: dict[int, bytes], font_path: Path) -> str:
    name = spec["name"]
    px, leading = spec["px"], spec["leading"]
    line_height = px + leading
    base_line = leading
    cell_h = px + 2
    ofs_y = 0  # 位图顶 = 行顶（ofs_y 从行顶起算）

    ascii_cps = [c for c in cps if ASCII_START <= c <= ASCII_END]
    other_cps = [c for c in cps if not (ASCII_START <= c <= ASCII_END)]
    ordered = ascii_cps + other_cps  # glyph id：先 ASCII 稠密段，后稀疏段

    blob = bytearray()
    dsc_lines = []
    # glyph id 0 保留：LVGL 视 gid==0 为"未找到"（fmt_txt.c: if(!gid) return false）。
    # 因此 glyph_dsc[0] 放 dummy，实际字形从 1 起，cmap 的 glyph_id_start 相应 +1。
    # 不加 dummy 的症状：半角空格（rcp=0 → gid=0）全部渲染为方框。
    dsc_lines.append(
        "    { .bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0 },"
        "  /* dummy: glyph id 0 = not found */"
    )
    for cp in ordered:
        g = glyphs[cp]
        dsc_lines.append(
            f"    {{ .bitmap_index = {len(blob)}, .adv_w = {g['adv'] * 16}, "
            f".box_w = {g['w']}, .box_h = {g['h']}, .ofs_x = 0, .ofs_y = {ofs_y} }},"
            f"  /* U+{cp:04X} {chr(cp)} */"
        )
        blob += packed[cp]

    sparse_start = other_cps[0] if other_cps else 0
    unicode_list = ", ".join(f"0x{c - sparse_start:04x}" for c in other_cps)
    sparse_g0 = len(ascii_cps) + 1  # +1 for dummy entry

    lines = []
    lines.append(f"// {FONTS_DIR.relative_to(ROOT) / name}.c —— 由 tools/gen_font_subset.py 生成，勿手改。")
    lines.append(f"// 源字体: {font_path.name}")
    lines.append("// 字体族: Noto Sans SC（SIL Open Font License 1.1, https://fonts.google.com/noto）。")
    lines.append(f"// 子集: {len(cps)} 码点 @ {px}px，PLAIN A4（stride=0），")
    lines.append("//       cmap = FORMAT0_TINY(ASCII 0x20-0x7E) + SPARSE_TINY(其余)。")
    lines.append(f"// 再生成: python3 tools/gen_font_subset.py --font <字体文件>")
    lines.append(f"//         （清单基线 tools/font_inventory_{name}.txt；门禁 --check）")
    lines.append('#include "lvgl.h"')
    lines.append("")
    lines.append(f"static const uint8_t {name}_bitmap[] = {{")
    for i in range(0, len(blob), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    if other_cps:
        lines.append(f"static const uint16_t {name}_unicode_list[] = {{ {unicode_list} }};")
        lines.append("")
    lines.append(f"static const lv_font_fmt_txt_glyph_dsc_t {name}_glyph_dsc[] = {{")
    lines += dsc_lines
    lines.append("};")
    lines.append("")
    lines.append(f"static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{")
    lines.append(
        f"    {{ .range_start = {ASCII_START:#x}, .range_length = {ASCII_END - ASCII_START + 1}, "
        f".glyph_id_start = 1, .unicode_list = NULL, .glyph_id_ofs_list = NULL, "
        f".list_length = {len(ascii_cps)}, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY }},"
    )
    if other_cps:
        # range_length 是覆盖区间长度（末码点-首码点+1），LVGL 查找先做区间边界检查；
        # 写 1 会导致除 range_start 外所有码点直接 miss（真机症状：中文全方框）。
        sparse_range_len = other_cps[-1] - sparse_start + 1
        lines.append(
            f"    {{ .range_start = {sparse_start:#x}, .range_length = {sparse_range_len}, "
            f".glyph_id_start = {sparse_g0}, .unicode_list = {name}_unicode_list, "
            f".glyph_id_ofs_list = NULL, .list_length = {len(other_cps)}, "
            f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY }},"
        )
    lines.append("};")
    lines.append("")
    lines.append(f"static const lv_font_fmt_txt_dsc_t {name}_dsc = {{")
    lines.append(f"    .glyph_bitmap = {name}_bitmap,")
    lines.append(f"    .glyph_dsc = {name}_glyph_dsc,")
    lines.append(f"    .cmaps = {name}_cmaps,")
    lines.append("    .kern_dsc = NULL,")
    lines.append("    .kern_scale = 0,")
    lines.append(f"    .cmap_num = {2 if other_cps else 1},")
    lines.append("    .bpp = 4,")
    lines.append("    .kern_classes = 0,")
    lines.append("    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,")
    lines.append("    .stride = 0,")
    lines.append("};")
    lines.append("")
    lines.append(f"const lv_font_t {name} = {{")
    lines.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    lines.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    lines.append(f"    .line_height = {line_height},")
    lines.append(f"    .base_line = {base_line},")
    lines.append("    .subpx = LV_FONT_SUBPX_NONE,")
    lines.append("    .underline_position = -1,")
    lines.append("    .underline_thickness = 1,")
    lines.append(f"    .dsc = &{name}_dsc,")
    lines.append("    .fallback = NULL,")
    lines.append("    .user_data = NULL,")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", help="TTF/OTF 字体文件路径（生成模式必填）")
    ap.add_argument("--check", action="store_true", help="仅做覆盖门禁")
    args = ap.parse_args()

    if args.check:
        bad = check_coverage()
        if bad:
            print(f"Font coverage: FAIL ({bad} missing)")
            return 1
        inv = load_inventory()
        print(f"Font coverage: PASS (16px {len(inv['app_font_16'])} cps, "
              f"26px {len(inv['app_font_26'])} cps)")
        return 0

    if not args.font:
        ap.error("生成模式需要 --font <字体文件>")

    font_path = Path(args.font)
    if not font_path.is_file():
        sys.exit(f"ERROR: font not found: {font_path}")

    inv = build_inventory()
    for spec in FONTS:
        (INVENTORY_DIR / f"font_inventory_{spec['name']}.txt").write_text(
            " ".join(str(c) for c in inv[spec["name"]]) + "\n")

    total = 0
    for spec in FONTS:
        cps = inv[spec["name"]]
        glyphs = rasterize(font_path, spec["px"], cps)
        packed = {cp: pack_4bpp(g["pixels"], g["w"], g["h"]) for cp, g in glyphs.items()}
        bad = verify_glyphs(glyphs, packed)
        if bad:
            print(f"ERROR: {spec['name']} 位图回验失败 {bad} 像素，拒绝写盘")
            return 1
        c_text = emit_c(spec, cps, glyphs, packed, font_path)
        FONTS_DIR.mkdir(parents=True, exist_ok=True)
        out = FONTS_DIR / f"{spec['name']}.c"
        out.write_text(c_text, encoding="utf-8", newline="\n")
        size = sum(len(p) for p in packed.values())
        total += size
        print(f"{spec['name']}: {len(cps)} 码点, 位图 {size} bytes -> {out}")
    print(f"Font generation: PASS (位图合计 {total} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
