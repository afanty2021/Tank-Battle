# -*- coding: utf-8 -*-
"""
从 Scratch 3 项目文件 (.sb3) 中提取《坦克大战》所需的全部素材。

用法:  python extract_assets.py <path-to-.sb3> <output-dir>

输出:
  assets/images/*.png|jpg   已按 2x 舞台分辨率导出的图片
  assets/sounds/*.ogg       音效 (SFML 不支持 mp3, 统一转 ogg)
  assets/manifest.json      逻辑尺寸 / 旋转中心 / 碰撞包围盒 等元数据
"""
import base64
import io
import json
import math
import os
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image

EXPORT_SCALE = 2   # 舞台 480x360 逻辑单位 -> 导出像素 的倍率
SUPER = 4          # SVG 合成的超采样倍率(相对 EXPORT_SCALE), 用于抗锯齿

# ---------------------------------------------------------------- SVG 变换 --

def parse_transform(text):
    """把 SVG transform 字符串解析成 3x3 矩阵 (以 [a b c d e f] 仿射形式)."""
    m = [1, 0, 0, 1, 0, 0]  # 单位阵
    if not text:
        return m
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', text):
        vals = [float(v) for v in re.split(r'[\s,]+', args.strip()) if v]
        if name == 'translate':
            t = [1, 0, 0, 1, vals[0], vals[1] if len(vals) > 1 else 0]
        elif name == 'scale':
            sx = vals[0]
            sy = vals[1] if len(vals) > 1 else sx
            t = [sx, 0, 0, sy, 0, 0]
        elif name == 'rotate':
            a = math.radians(vals[0])
            r = [math.cos(a), math.sin(a), -math.sin(a), math.cos(a), 0, 0]
            if len(vals) == 3:  # rotate(a, cx, cy) = 平移->旋转->平移回
                cx, cy = vals[1], vals[2]
                r = mat_mul(mat_mul([1, 0, 0, 1, cx, cy], r), [1, 0, 0, 1, -cx, -cy])
            t = r
        elif name == 'skewX':
            t = [1, 0, math.tan(math.radians(vals[0])), 1, 0, 0]
        elif name == 'skewY':
            t = [1, math.tan(math.radians(vals[0])), 0, 1, 0, 0]
        elif name == 'matrix':
            t = vals
        else:
            t = [1, 0, 0, 1, 0, 0]
        m = mat_mul(m, t)
    return m


def mat_mul(m1, m2):
    """m1 * m2 (先应用 m2 再应用 m1), 仿射 [a b c d e f]."""
    a1, b1, c1, d1, e1, f1 = m1
    a2, b2, c2, d2, e2, f2 = m2
    return [a1 * a2 + c1 * b2,
            b1 * a2 + d1 * b2,
            a1 * c2 + c1 * d2,
            b1 * c2 + d1 * d2,
            a1 * e2 + c1 * f2 + e1,
            b1 * e2 + d1 * f2 + f1]


def apply(m, x, y):
    a, b, c, d, e, f = m
    return a * x + c * y + e, b * x + d * y + f


def invert(m):
    a, b, c, d, e, f = m
    det = a * d - b * c
    return [d / det, -b / det, -c / det, a / det,
            (c * f - d * e) / det, (b * e - a * f) / det]

# ---------------------------------------------------------------- 图片导出 --

def export_svg(z, md5, out_png):
    """把 Scratch 矢量造型(内嵌一张位图)合成为 PNG, 返回 (逻辑宽, 逻辑高)."""
    root = ET.fromstring(z.read(md5))
    ns = '{http://www.w3.org/2000/svg}'
    xlink = '{http://www.w3.org/1999/xlink}'

    W = float(root.get('width'))
    H = float(root.get('height'))

    # 找到第一个 <image>, 同时收集祖先链上的 transform
    img_el, chain = None, []
    def walk(el, m):
        nonlocal img_el
        mm = mat_mul(m, parse_transform(el.get('transform')))
        if el.tag == ns + 'image' and img_el is None:
            img_el, chain[:] = el, [mm]
            return
        for ch in el:
            walk(ch, mm)
    walk(root, [1, 0, 0, 1, 0, 0])

    href = img_el.get(xlink + 'href') or img_el.get('href')
    b64 = href.split(',', 1)[1]
    bmp = Image.open(io.BytesIO(base64.b64decode(b64))).convert('RGBA')

    x, y = float(img_el.get('x', 0)), float(img_el.get('y', 0))
    w, h = float(img_el.get('width')), float(img_el.get('height'))
    # 位图像素 -> 造型局部坐标: 先缩放到 (w,h) 矩形, 平移 (x,y), 再套祖先+自身变换
    m = mat_mul(chain[0],
                mat_mul([1, 0, 0, 1, x, y], [w / bmp.width, 0, 0, h / bmp.height, 0, 0]))

    S = EXPORT_SCALE * SUPER
    canvas = Image.new('RGBA', (int(round(W * S)), int(round(H * S))), (0, 0, 0, 0))
    # PIL 需要输出->输入的映射
    inv = invert(mat_mul([S, 0, 0, S, 0, 0], m))
    rendered = bmp.transform(canvas.size, Image.AFFINE,
                             (inv[0], inv[2], inv[4], inv[1], inv[3], inv[5]),
                             resample=Image.BICUBIC)
    canvas.alpha_composite(rendered)
    canvas = canvas.resize((int(round(W * EXPORT_SCALE)), int(round(H * EXPORT_SCALE))),
                           Image.LANCZOS)
    canvas.save(out_png)
    return W, H


def export_bitmap(z, md5, out_png):
    """位图造型原样导出, 返回逻辑宽高 (px / bitmapResolution 由调用方处理)."""
    data = z.read(md5)
    with open(out_png, 'wb') as f:
        f.write(data)
    img = Image.open(io.BytesIO(data))
    return img.width, img.height

# -------------------------------------------------------------------- 主流程 --

# 造型 -> (导出文件名, 所在角色)
COSTUMES = [
    ('Stage',              '地图2',              'background.jpg',      'jpg'),
    ('玩家坦克车身', '玩家坦克车身-造型1', 'player_body.png',      'svg'),
    ('玩家坦克车身', 'b1', 'explosion_player_1.png', 'png'),
    ('玩家坦克车身', 'b2', 'explosion_player_2.png', 'png'),
    ('玩家坦克车身', 'b3', 'explosion_player_3.png', 'png'),
    ('玩家坦克车身', 'b4', 'explosion_player_4.png', 'png'),
    ('玩家坦克车身', 'b5', 'explosion_player_5.png', 'png'),
    ('玩家坦克车身', 'b6', 'explosion_player_6.png', 'png'),
    ('玩家坦克炮塔', '玩家坦克炮塔-造型1', 'player_turret.png',   'svg'),
    ('敌方坦克1',  '敌方坦克1-造型1',   'enemy_tank_1.png',     'svg'),
    ('敌方坦克1',  '敌方坦克2',         'enemy_tank_2.png',     'svg'),
    ('敌方坦克1',  'b1', 'explosion_enemy_1.png', 'svg'),
    ('敌方坦克1',  'b2', 'explosion_enemy_2.png', 'svg'),
    ('敌方坦克1',  'b3', 'explosion_enemy_3.png', 'svg'),
    ('敌方坦克1',  'b4', 'explosion_enemy_4.png', 'svg'),
    ('敌方坦克1',  'b5', 'explosion_enemy_5.png', 'svg'),
    ('敌方坦克1',  'b6', 'explosion_enemy_6.png', 'svg'),
    ('导弹',       '导弹-造型1',        'missile.png',          'svg'),
    ('子弹',       '子弹-造型1',        'bullet.png',           'svg'),
    ('开始游戏',   '开始游戏-造型1',    'screen_start.jpg',     'jpg'),
    ('游戏结束',   '游戏结束-造型1',    'screen_gameover.jpg',  'jpg'),
]

# 音效 -> 导出名
SOUNDS = [
    ('玩家坦克车身', '爆炸',     'explosion.ogg'),
    ('导弹',        '坦克发射',  'fire_player.ogg'),
    ('子弹',        '敌人发射',  'fire_enemy.ogg'),
    ('开始游戏',    '开始游戏',  'music_start.ogg'),
    ('游戏结束',    '游戏结束',  'music_gameover.ogg'),
]


def main():
    sb3_path, out_dir = sys.argv[1], sys.argv[2]
    img_dir = os.path.join(out_dir, 'images')
    snd_dir = os.path.join(out_dir, 'sounds')
    tmp_dir = os.path.join(out_dir, 'sounds_tmp')
    os.makedirs(img_dir, exist_ok=True)
    os.makedirs(snd_dir, exist_ok=True)
    os.makedirs(tmp_dir, exist_ok=True)

    z = zipfile.ZipFile(sb3_path)
    project = json.loads(z.read('project.json').decode('utf-8'))
    targets = {t['name']: t for t in project['targets']}

    manifest = {'images': {}, 'sounds': {}}

    for sprite, costume, fname, kind in COSTUMES:
        c = next(c for c in targets[sprite]['costumes'] if c['name'] == costume)
        out_path = os.path.join(img_dir, fname)
        if kind == 'svg':
            lw, lh = export_svg(z, c['md5ext'], out_path)
            res = 1
        else:
            lw, lh = export_bitmap(z, c['md5ext'], out_path)
            res = c.get('bitmapResolution', 2)
        # 舞台逻辑尺寸
        stage_w, stage_h = lw / res, lh / res
        # 透明像素紧包围盒(像素, 已是 EXPORT_SCALE 倍)
        bbox = None
        if not fname.endswith('.jpg'):
            with Image.open(out_path) as im:
                alpha = im.getchannel('A')
                bbox = alpha.getbbox()
        manifest['images'][fname.rsplit('.', 1)[0]] = {
            'file': fname,
            'stage_size': [round(stage_w, 3), round(stage_h, 3)],
            'rotation_center': [c['rotationCenterX'] / res,
                                c['rotationCenterY'] / res],  # 舞台单位
            'tight_bbox_px': bbox,
        }
        print(f'[img] {fname:<26} stage {stage_w:7.2f} x {stage_h:7.2f}  rot({c["rotationCenterX"]/res:.1f},{c["rotationCenterY"]/res:.1f})')

    # 位图造型的旋转中心: Scratch 里以图像中心为锚时 rotationCenter = 像素尺寸/2*res…
    # 上面除以 res 后即舞台单位, 与 SVG 一致, C++ 端统一乘 EXPORT_SCALE 用作 sf::Sprite origin。

    import subprocess
    for sprite, sound, fname in SOUNDS:
        s = next(s for s in targets[sprite]['sounds'] if s['name'] == sound)
        src = os.path.join(tmp_dir, s['md5ext'])
        with open(src, 'wb') as f:
            f.write(z.read(s['md5ext']))
        out_path = os.path.join(snd_dir, fname)
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', src,
                        '-ar', '44100', '-ac', '2', out_path], check=True)
        os.remove(src)
        manifest['sounds'][fname] = s['md5ext']
        print(f'[snd] {fname}')
    os.rmdir(tmp_dir)

    with open(os.path.join(out_dir, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    print('done ->', out_dir)


if __name__ == '__main__':
    main()
