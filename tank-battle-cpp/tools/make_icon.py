# -*- coding: utf-8 -*-
# 生成 res/tank.ico(exe 资源图标) 与 res/tank_icon.png(运行时窗口图标)。
# 图标 = 玩家坦克(车身+炮塔按游戏内锚点对齐、朝上) —— 与 Assets.cpp 的
# 旋转中心元数据联动: 2x 贴图里 rc_px = rc_stage * 2。
#
# 用法(在 tank-battle-cpp 目录下): python tools/make_icon.py
from PIL import Image
import os

BODY = ("assets/images/player_body.png", (112.15, 54.04))    # (文件, 旋转中心-舞台单位)
TURRET = ("assets/images/player_turret.png", (63.54, 45.08))
OUT_DIR = "res"


def rot_ccw90_with_rc(path, rc_stage):
    """图片逆时针转 90°(朝右 -> 朝上, 同游戏 dir=0 姿态), 返回(新图, 新旋转中心)"""
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    rc = (rc_stage[0] * 2.0, rc_stage[1] * 2.0)  # 舞台单位 -> 2x 像素
    c = (w / 2.0, h / 2.0)
    d = (rc[0] - c[0], rc[1] - c[1])
    rot = im.rotate(90, expand=True)             # PIL rotate 为逆时针
    nc = (h / 2.0, w / 2.0)                      # 新尺寸 (h, w) 的新中心
    nd = (d[1], -d[0])                           # y-down 屏幕系逆时针 90°
    return rot, (nc[0] + nd[0], nc[1] + nd[1])


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    body, brc = rot_ccw90_with_rc(*BODY)
    turret, trc = rot_ccw90_with_rc(*TURRET)
    # 游戏里两个精灵的旋转中心都锚定在 player.pos: 合成时对齐两个 rc
    off = (round(brc[0] - trc[0]), round(brc[1] - trc[1]))
    combined = body.copy()
    combined.alpha_composite(turret, off)
    # 裁到内容紧包围盒, 等比缩放到 256 见方画布居中
    bbox = combined.getbbox()
    combined = combined.crop(bbox)
    size = max(combined.size)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(combined, ((size - combined.width) // 2,
                                      (size - combined.height) // 2))
    master = canvas.resize((256, 256), Image.LANCZOS)
    master.save(os.path.join(OUT_DIR, "tank_icon.png"))
    master.save(os.path.join(OUT_DIR, "tank.ico"),
                sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                       (64, 64), (128, 128), (256, 256)])
    print("res/tank_icon.png + res/tank.ico written, master", master.size)


if __name__ == "__main__":
    main()
