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
# 常用字层(约 550 字): 课例文案 + 学生自己写句子大概率要用的字。
# 改这里后要重跑本脚本重新生成字体。
TEXT = ("上下左右为键鼠标左键发射炮弹！，。：、分数"
       "按空格再来一局开始生命时间胜利用坦克大战教学版"
       "的一是了我不人在他有这上们来到时大地为子中你说生国年着就那和要她出也得里后自以会家可下而过天去能对小多然于心学么之都好看起发当没成只如事把还用第样道想作种开美总从无情己面最女但现前些所同日手又行意动方期它头经长儿回位分爱老因很给名法间斯知世什两次使身者被高已亲其进此话常与活正感见明问力理尔点文几定本公特做外孩相西果走将月十实向声车全信重三机工物气每并别真打太新比才便夫再书部水像眼等体却加电主界门利海受听表德少克代员许先口由死安写性马光白或住难望教命花结乐色更拉东神记处让母父应直字场平报友关放至张认接告入笑内英军候民岁往何度山觉路带万男边风解叫任金快原吃妈变通师立象数四失满战远格士音轻目条呢病始达深完今提求清王化空业思切怎非找片罗钱语元喜曾离飞科言干流欢约各即指合反题必该论交终林请医晚制球决传画保读运及则房早院量苦火布品近坐产答星精视五连司巴奇管类未朋且婚台夜青北队久乎越观落尽形影红爸百令周吧识步希亚术留市半热送兴造谈容极随演收首根讲整式取照办强石古华铁跑迷赢输关卡难度简单挑战继续努力加油棒欢迎基地保卫生消灭敌人导弹发射爆炸命值分数秒钟暂停开始束新纪录最高勇敢战斗胜利失败重来次数子弹护盾无敌血追速度左右上下移动键盘空格鼠标点击选择退出帮助规则玩法提示句号感叹问好的呢吧啊呀哦哈嘿嗯嘿嘿太真超级厉害好玩有趣神奇漂亮可爱聪明英雄坦克大战准吗困备戏注游谁帅碰换级枪炮装甲修复升级奖励金币宝箱任务目标")

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
