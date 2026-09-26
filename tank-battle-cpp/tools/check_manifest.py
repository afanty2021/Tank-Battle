# -*- coding: utf-8 -*-
"""校验 src/Assets.cpp 中手工维护的造型常量与 assets/manifest.json 一致。

用法: python tools/check_manifest.py [assets目录] [Assets.cpp路径]
防止重新导出素材后忘记同步 C++ 侧元数据(尺寸/旋转中心)。
"""
import json, os, re, sys
sys.stdout.reconfigure(encoding='utf-8')

assets_dir = sys.argv[1] if len(sys.argv) > 1 else 'assets'
cpp_path = sys.argv[2] if len(sys.argv) > 2 else 'src/Assets.cpp'

with open(os.path.join(assets_dir, 'manifest.json'), encoding='utf-8') as f:
    manifest = json.load(f)

src = open(cpp_path, encoding='utf-8').read()
errors = []

# 1) 单行 loadCostume 表: loadCostume(x, img + "file.png", {W, H}, {rx, ry})
row = re.compile(
    r'loadCostume\(\s*\w+,\s*img\s*\+\s*"([\w.]+)",\s*\{\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*\},'
    r'\s*\{\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*\}\)')
for m in row.finditer(src):
    fname, w, h, rx, ry = m.group(1), *map(float, m.groups()[1:])
    key = fname.rsplit('.', 1)[0]
    if key not in manifest['images']:
        errors.append(f'{fname}: 不在 manifest 中')
        continue
    mi = manifest['images'][key]
    for label, got, want in (('宽', w, mi['stage_size'][0]),
                             ('高', h, mi['stage_size'][1]),
                             ('旋转中心x', rx, round(mi['rotation_center'][0], 3)),
                             ('旋转中心y', ry, round(mi['rotation_center'][1], 3))):
        if abs(got - want) > 0.01:
            errors.append(f'{fname} {label}: C++={got} manifest={want}')

# 2) 爆炸帧表(两个数组, 每行 {{w,h},{rx,ry}})
for arr_name in ('playerExpl', 'enemyExpl'):
    arr = re.search(arr_name + r'\[6\]\[2\]\s*=\s*\{(.*?)\};', src, re.S)
    if not arr:
        errors.append(f'找不到数组 {arr_name}')
        continue
    entries = re.findall(r'\{\{([\d.]+)f?,\s*([\d.]+)f?\},\s*\{([\d.]+)f?,\s*([\d.]+)f?\}\}',
                         arr.group(1))
    if len(entries) != 6:
        errors.append(f'{arr_name}: 解析到 {len(entries)} 项(应为 6)')
        continue
    prefix = 'explosion_player_' if arr_name == 'playerExpl' else 'explosion_enemy_'
    for i, (w, h, rx, ry) in enumerate(entries):
        mi = manifest['images'][f'{prefix}{i + 1}']
        for label, got, want in (('宽', float(w), mi['stage_size'][0]),
                                 ('高', float(h), mi['stage_size'][1]),
                                 ('旋转中心x', float(rx), round(mi['rotation_center'][0], 3)),
                                 ('旋转中心y', float(ry), round(mi['rotation_center'][1], 3))):
            if abs(got - want) > 0.01:
                errors.append(f'{prefix}{i+1} {label}: C++={got} manifest={want}')

# 3) 素材文件齐全
for key, info in manifest['images'].items():
    p = os.path.join(assets_dir, 'images', info['file'])
    if not os.path.exists(p):
        errors.append(f'缺图片: {p}')
for fname in manifest['sounds']:
    if not os.path.exists(os.path.join(assets_dir, 'sounds', fname)):
        errors.append(f'缺音效: {fname}')

if errors:
    print('check_manifest: FAILED')
    for e in errors:
        print('  ', e)
    sys.exit(1)
print('check_manifest: OK (造型常量与 manifest 一致, 素材齐全)')
