# -*- coding: utf-8 -*-
"""用 objdump 计算 exe 的 DLL 依赖闭包, 从 ucrt64/bin 部署到 exe 目录。

用法: python tools/deploy_dlls.py [ucrt64_bin] [项目目录]
默认: D:\\Scoop\\apps\\msys2\\current\\ucrt64\\bin 和脚本上级目录。
已存在的 DLL 也会重新覆盖, 保证与工具链版本一致。
"""
import os, re, shutil, subprocess, sys
sys.stdout.reconfigure(encoding='utf-8')

UCRT = sys.argv[1] if len(sys.argv) > 1 else r'D:\Scoop\apps\msys2\current\ucrt64\bin'
PROJ = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(PROJ, 'tank-battle.exe')
if not os.path.exists(EXE):
    print('未找到 tank-battle.exe, 请先编译')
    sys.exit(1)

env = dict(os.environ)
env['PATH'] = UCRT + ';C:\\Windows\\System32;C:\\Windows'

SYSTEM_PREFIXES = ('api-ms-', 'kernel32', 'ws2_32', 'advapi32', 'gdi32', 'user32',
                   'opengl32', 'winmm', 'dwrite', 'rpcrt4', 'usp10', 'ole32', 'shell32',
                   'msvcrt', 'ucrtbase')

def imports_of(path):
    r = subprocess.run([os.path.join(UCRT, 'objdump.exe'), '-p', path],
                       capture_output=True, env=env, text=True, errors='replace')
    if r.returncode != 0:
        print('objdump 失败:', path, r.returncode)
        return []
    return re.findall(r'DLL Name: (\S+)', r.stdout)

seen, queue, copied = set(), [EXE], 0
while queue:
    p = queue.pop()
    for dll in imports_of(p):
        if dll.lower() in seen or dll.lower().startswith(SYSTEM_PREFIXES):
            continue
        seen.add(dll.lower())
        src = os.path.join(UCRT, dll)
        if not os.path.exists(src):
            continue  # 系统提供的 DLL
        shutil.copy2(src, os.path.join(PROJ, dll))  # 总是覆盖, 保持与工具链同步
        copied += 1
        queue.append(src)

print(f'DLL 部署完成: 遍历闭包 {len(seen)} 个, 覆盖拷贝 {copied} 个 -> {PROJ}')
