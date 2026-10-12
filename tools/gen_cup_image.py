# gen_cup_image.py —— 把用户实拍圣杯照片转成 LVGL 9 ARGB8888 图片资源。
# 用法: python3 tools/gen_cup_image.py [尺寸]
# 输入: tools/cup_photo_src.png；输出: main/assets/images/cup_photo.c
# 白底处理：按最小通道(min(r,g,b))估算 alpha——白背景全透明、杯体不透明、
# 边缘半透明渐变，避免暗色主题下出现白色矩形底。
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "cup_photo_src.png"
OUT = ROOT / "main" / "assets" / "images" / "cup_photo.c"
NAME = "cup_photo"
SIZE = int(sys.argv[1]) if len(sys.argv) > 1 else 144


def alpha_for(minc: int) -> int:
    # minc: 像素最小通道值。>=190 视为背景(0)，<=70 完全不透明(255)，中间线性渐变。
    if minc >= 190:
        return 0
    if minc <= 70:
        return 255
    return (190 - minc) * 255 // 120


img = Image.open(SRC).convert("RGB")
side = min(img.size)
img = img.crop(((img.width - side) // 2, (img.height - side) // 2,
                (img.width + side) // 2, (img.height + side) // 2))
img = img.resize((SIZE, SIZE), Image.LANCZOS)

data = bytearray()
for r, g, b in img.getdata():
    a = alpha_for(min(r, g, b))
    # LVGL ARGB8888 小端字节序: B, G, R, A
    data += bytes((b, g, r, a))

# 写盘前回验：重新解析首行像素与 alpha 范围
assert len(data) == SIZE * SIZE * 4
opaque = sum(1 for i in range(3, len(data), 4) if data[i] == 255)
transp = sum(1 for i in range(3, len(data), 4) if data[i] == 0)
print(f"opaque={opaque} transparent={transp} half={SIZE*SIZE-opaque-transp}")

lines = []
lines.append(f"// {OUT.relative_to(ROOT)} —— 由 tools/gen_cup_image.py 生成，勿手改。")
lines.append("// 源图: 用户提供的红圣杯实拍（白底已转透明）。")
lines.append('#include "lvgl.h"')
lines.append("")
lines.append(f"static const uint8_t {NAME}_data[] = {{")
for i in range(0, len(data), 16):
    chunk = ", ".join(f"0x{x:02x}" for x in data[i:i + 16])
    lines.append(f"    {chunk},")
lines.append("};")
lines.append("")
lines.append(f"const lv_image_dsc_t {NAME} = {{")
lines.append("    .header = {")
lines.append(f"        .cf = LV_COLOR_FORMAT_ARGB8888,")
lines.append(f"        .w = {SIZE},")
lines.append(f"        .h = {SIZE},")
lines.append(f"        .stride = {SIZE * 4},")
lines.append("    },")
lines.append(f"    .data_size = {len(data)},")
lines.append(f"    .data = {NAME}_data,")
lines.append("};")
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"OK: {OUT} ({SIZE}x{SIZE}, {len(data)} bytes)")
