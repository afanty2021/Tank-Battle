# -*- coding: utf-8 -*-
# 生成 assets/fonts/NotoSansSC-Game.otf —— 游戏自带的中文字体子集(OFL 授权)。
# 用途: 分数/操作提示文字在 Windows/Linux/macOS 三平台渲染一致。
#
# 用法(在 tank-battle-cpp 目录下):
#   1. 下载源字体(约 8MB, 已被 .gitignore 忽略):
#      curl -L -o NotoSansSC-Regular.otf \
#        https://raw.githubusercontent.com/googlefonts/noto-cjk/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
#   2. python tools/subset_font.py
#
# 注意: 若修改了游戏内显示的文字(Game.cpp 里的 utf8("...")), 需把新字符
# 加进下面 TEXT 并重跑本脚本, 否则新字符不会渲染。
import os
from fontTools import subset
from fontTools.ttLib import TTFont

SRC = "NotoSansSC-Regular.otf"
OUT = "assets/fonts/NotoSansSC-Game.otf"
# 教学版课例里 draw_text 会渲染的中文也要加进来(lessons/L06~L08)
TEXT = ("上下左右为键鼠标左键发射炮弹！，。：、分数"
       "按空格再来一局开始生命时间胜利用坦克大战教学版")

# sanity: source font must open and contain our chars
src = TTFont(SRC)
src_cmap = src.getBestCmap()
missing_src = [c for c in TEXT if ord(c) not in src_cmap]
if missing_src:
    raise SystemExit("source font missing chars: %r" % missing_src)
src.close()

opts = subset.Options()
opts.layout_features = ["*"]
opts.name_IDs = ["*"]
opts.drop_tables += ["DSIG"]
font = subset.load_font(SRC, opts)
sub = subset.Subsetter(options=opts)
sub.populate(text=TEXT, unicodes=list(range(0x20, 0x7F)))
sub.subset(font)
subset.save_font(font, OUT, opts)

out = TTFont(OUT)
cmap = out.getBestCmap()
need = TEXT + "WSADwsad0123456789: !"
missing = [c for c in need if ord(c) not in cmap]
print("subset size: %.1f KB, glyphs: %d" % (os.path.getsize(OUT) / 1024, len(cmap)))
print("missing:", missing if missing else "NONE")
print("family:", out["name"].getDebugName(1))
