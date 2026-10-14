# -*- coding: utf-8 -*-
"""
生成《消灭海盗游戏软件 V1.0》计算机软件著作权登记材料：
  1. 源程序鉴别材料 docx（全部源程序的前30页+后30页，每页50行，页眉含名称/版本/页码）
  2. 操作说明书 docx（页眉同上；截图处留有占位，提交前需替换为真实游戏截图）
  3. 03_申请表填报信息参考.md（含自动统计的代码行数）

用法：py gen_docs.py
"""
import math
import os

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS_DIR = os.path.normpath(os.path.join(HERE, "..", "Data", "Scripts"))

SOFT = "消灭海盗游戏软件"
VER = "V1.0"
HEADER_TEXT = SOFT + " " + VER
LINES_PER_PAGE = 50

# 源程序文件顺序（入口在前，其后按依赖关系排列）
SOURCE_FILES = [
    "main.lua",
    "core/Joystick.lua",
    "game/Config.lua",
    "game/Coast.lua",
    "game/Player.lua",
    "game/Shooting.lua",
    "game/Enemy.lua",
    "game/Waves.lua",
    "game/WaveGen.lua",
    "game/Grow.lua",
    "game/Juice.lua",
    "game/Fx.lua",
    "game/Audio.lua",
    "game/Hud.lua",
    "game/Dog.lua",
    "game/DogLaunch.lua",
    "game/Materials.lua",
    "game/Voice.lua",
]

SRC_DOCX = os.path.join(HERE, "消灭海盗游戏软件V1.0_源程序.docx")
MANUAL_DOCX = os.path.join(HERE, "消灭海盗游戏软件V1.0_操作说明书.docx")
FORM_MD = os.path.join(HERE, "03_申请表填报信息参考.md")


# ---------------------------------------------------------------------
# 通用排版助手
# ---------------------------------------------------------------------
def set_run_font(run, ascii_font, ea_font, size, bold=False, color=None):
    run.font.name = ascii_font
    run.font.size = Pt(size)
    run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), ascii_font)
    rfonts.set(qn("w:hAnsi"), ascii_font)
    rfonts.set(qn("w:eastAsia"), ea_font)


def set_style_font(style, ascii_font, ea_font, size, bold=False, color=None):
    style.font.name = ascii_font
    style.font.size = Pt(size)
    style.font.bold = bold
    if color is not None:
        style.font.color.rgb = color
    rpr = style.element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:eastAsia"), ea_font)


def add_field_run(paragraph, instr, ascii_font="Times New Roman", ea_font="宋体", size=9):
    """插入域（如 PAGE / NUMPAGES），用于页眉自动页码。"""
    run = paragraph.add_run()
    f1 = OxmlElement("w:fldChar")
    f1.set(qn("w:fldCharType"), "begin")
    it = OxmlElement("w:instrText")
    it.set(qn("xml:space"), "preserve")
    it.text = instr
    f2 = OxmlElement("w:fldChar")
    f2.set(qn("w:fldCharType"), "end")
    run._r.append(f1)
    run._r.append(it)
    run._r.append(f2)
    set_run_font(run, ascii_font, ea_font, size)
    return run


def setup_a4(section, top=2.0, bottom=1.8, left=1.8, right=1.8):
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(top)
    section.bottom_margin = Cm(bottom)
    section.left_margin = Cm(left)
    section.right_margin = Cm(right)
    section.header_distance = Cm(1.2)


def make_header(section):
    header = section.header
    header.is_linked_to_previous = False
    p = header.paragraphs[0]
    for r in list(p.runs):
        r.text = ""
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run(HEADER_TEXT + "    第 "), "Times New Roman", "宋体", 9)
    add_field_run(p, "PAGE")
    set_run_font(p.add_run(" 页  共 "), "Times New Roman", "宋体", 9)
    add_field_run(p, "NUMPAGES")
    set_run_font(p.add_run(" 页"), "Times New Roman", "宋体", 9)


# ---------------------------------------------------------------------
# 1. 源程序鉴别材料
# ---------------------------------------------------------------------
def read_all_lines():
    lines = []
    for rel in SOURCE_FILES:
        path = os.path.join(SCRIPTS_DIR, rel.replace("/", os.sep))
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        file_lines = [l.rstrip("\r") for l in content.split("\n")]
        if file_lines and file_lines[-1] == "":
            file_lines.pop()
        lines.extend(file_lines)
    return lines


def estimate_wraps(page_lines, chars_per_line=96.0):
    """估算因超长行折行产生的额外行数（中文按全宽计）。"""
    extra = 0
    for l in page_lines:
        w = sum(1.0 if ord(c) < 128 else 1.85 for c in l)
        if w > chars_per_line:
            extra += int(math.ceil(w / chars_per_line)) - 1
    return extra


def build_source_doc():
    lines = read_all_lines()
    total = len(lines)
    pages = [lines[i:i + LINES_PER_PAGE] for i in range(0, total, LINES_PER_PAGE)]
    n_pages = len(pages)
    if n_pages <= 60:
        selected = list(range(n_pages))
    else:
        selected = list(range(30)) + list(range(n_pages - 30, n_pages))

    worst_extra, worst_page = 0, -1
    for pi in selected:
        extra = estimate_wraps(pages[pi])
        if extra > worst_extra:
            worst_extra, worst_page = extra, pi

    doc = Document()
    sec = doc.sections[0]
    setup_a4(sec)
    make_header(sec)
    set_style_font(doc.styles["Normal"], "Consolas", "宋体", 9)

    for i, pi in enumerate(selected):
        last_para = None
        for l in pages[pi]:
            p = doc.add_paragraph()
            pf = p.paragraph_format
            pf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
            pf.line_spacing = Pt(11)
            pf.space_before = Pt(0)
            pf.space_after = Pt(0)
            set_run_font(p.add_run(l), "Consolas", "宋体", 9)
            last_para = p
        if i < len(selected) - 1:
            last_para.add_run().add_break(WD_BREAK.PAGE)

    doc.core_properties.title = HEADER_TEXT + " 源程序"
    doc.save(SRC_DOCX)
    return {
        "total_lines": total,
        "total_pages": n_pages,
        "selected_pages": len(selected),
        "worst_extra": worst_extra,
        "worst_page": worst_page + 1,
    }


# ---------------------------------------------------------------------
# 2. 操作说明书
# ---------------------------------------------------------------------
def h1(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(14)
    p.paragraph_format.space_after = Pt(8)
    set_run_font(p.add_run(text), "Times New Roman", "黑体", 16, bold=True,
                 color=RGBColor(0, 0, 0))


def h2(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(6)
    set_run_font(p.add_run(text), "Times New Roman", "黑体", 13, bold=True,
                 color=RGBColor(0, 0, 0))


def body(doc, text, indent=True):
    p = doc.add_paragraph()
    pf = p.paragraph_format
    pf.line_spacing = 1.4
    pf.space_after = Pt(4)
    if indent:
        pf.first_line_indent = Pt(24)
    set_run_font(p.add_run(text), "Times New Roman", "宋体", 12)


def shot(doc, caption):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(10)
    set_run_font(p.add_run("【此处插入截图：" + caption + "】"),
                 "Times New Roman", "黑体", 12, bold=True,
                 color=RGBColor(0xC0, 0x00, 0x00))


def table(doc, rows, widths=(4.2, 12.4)):
    t = doc.add_table(rows=len(rows), cols=2)
    t.style = "Table Grid"
    for ri, (c0, c1) in enumerate(rows):
        for ci, text in enumerate((c0, c1)):
            cell = t.rows[ri].cells[ci]
            cell.width = Cm(widths[ci])
            p = cell.paragraphs[0]
            p.paragraph_format.space_after = Pt(2)
            set_run_font(p.add_run(text), "Times New Roman", "宋体", 10.5,
                         bold=(ri == 0))
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


def build_manual_doc():
    doc = Document()
    sec = doc.sections[0]
    setup_a4(sec, top=2.5, bottom=2.5, left=2.6, right=2.6)
    make_header(sec)
    set_style_font(doc.styles["Normal"], "Times New Roman", "宋体", 12)

    # ---- 封面 ----
    for _ in range(6):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run(SOFT), "Times New Roman", "黑体", 26, bold=True)
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run("操作说明书"), "Times New Roman", "黑体", 22, bold=True)
    doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run("版本号：" + VER), "Times New Roman", "宋体", 14)
    for _ in range(8):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run("著作权人：____________（与实名认证姓名一致）"),
                 "Times New Roman", "宋体", 12)
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run("日期：______年____月____日"), "Times New Roman", "宋体", 12)
    doc.paragraphs[-1].add_run().add_break(WD_BREAK.PAGE)

    # ---- 1 引言 ----
    h1(doc, "1  引言")
    h2(doc, "1.1  编写目的")
    body(doc, "本说明书为《消灭海盗游戏软件》（版本号 V1.0）的用户操作说明书，面向最终玩家"
              "与运营审核人员编写，用于介绍软件的功能特性、运行环境与操作方法，帮助使用者"
              "快速了解并上手本软件。本说明书描述的内容与软件 V1.0 版本的实际功能一致。")
    h2(doc, "1.2  软件概述")
    body(doc, "《消灭海盗游戏软件》是一款海岸题材的第三人称俯视角休闲射击小游戏。玩家扮演"
              "海岸守卫，在沙滩阵地上灵活走位，武器自动锁定并射击乘小船登陆的海盗，阻止敌人"
              "突破防线。软件采用波次挑战制：击杀敌人获得分数与经验，经验驱动武器成长（弹丸"
              "数量与伤害逐级提升）；此外还有宠物狗协同作战、可驾驶小船出海自由探索等玩法。"
              "软件单局节奏紧凑，全程仅需一只虚拟摇杆操作，适合碎片化时间休闲娱乐。")
    h2(doc, "1.3  运行平台")
    body(doc, "本软件以抖音小游戏形式发布，在抖音 App 内搜索“消灭海盗”或扫描小程序码即可"
              "启动，无需下载安装，即点即玩。")

    # ---- 2 运行环境 ----
    h1(doc, "2  运行环境")
    table(doc, [
        ("项目", "说明"),
        ("硬件环境", "智能手机（建议运行内存 4GB 及以上）"),
        ("操作系统", "Android 8.0 及以上 / iOS 12 及以上"),
        ("软件环境", "抖音 App（建议升级到最新版本），抖音小游戏基础库 3.7.0 及以上"),
        ("网络环境", "首次进入需联网下载游戏资源（Wi-Fi 或 4G/5G），资源加载完成后可流畅游玩"),
    ])

    # ---- 3 软件功能说明 ----
    h1(doc, "3  软件功能说明")
    h2(doc, "3.1  总体功能结构")
    body(doc, "软件由战斗、波次、敌人、成长、伙伴、载具、计分、反馈、防线、结算与界面共十一"
              "个功能模块构成，各模块功能如下表所示。", indent=False)
    table(doc, [
        ("功能模块", "功能说明"),
        ("战斗系统", "全自动锁定射击、扇形多弹丸散射、弹匣与自动装弹"),
        ("波次系统", "三波手调开场加无尽波次、四航道轮转、齐射、大船滞留炮击"),
        ("敌人系统", "登陆小艇、武装大船、普通海盗、突袭型海盗"),
        ("成长系统", "击杀经验积累、等级提升、弹丸数与伤害共八档成长"),
        ("伙伴系统", "宠物狗跟随、主动索敌咬击、波间回身边休息"),
        ("载具系统", "码头登船、摇杆驾船、舷侧双炮、随时下船"),
        ("计分系统", "击杀得分、连击加成、连杀触发范围爆炸"),
        ("反馈系统", "击杀定格、屏幕震动、飘分字、命中标记、音效与旁白"),
        ("防线系统", "防线血量、漏防扣血、败局判定"),
        ("结算系统", "败局结算面板、得分展示、一键重新开始"),
        ("界面系统", "竖屏自适应 HUD、浮动原点虚拟摇杆、数据绑定实时刷新"),
    ])
    h2(doc, "3.2  核心玩法循环")
    body(doc, "进入游戏 → 抵御海盗波次 → 击杀获得分数与经验 → 武器升级变强 → 迎接更密集的"
              "波次（无尽模式）→ 防线失守或主角阵亡 → 结算得分 → 点击“再来一局”重新开始。")
    h2(doc, "3.3  战斗系统")
    body(doc, "武器全自动开火：自动搜索主角周围 45 米范围内最近的敌人射击，玩家无需瞄准，"
              "只需专注走位。两发子弹之间最短间隔 0.22 秒；弹匣容量 5 发，打空后自动装弹"
              "1.2 秒，界面右下显示装弹进度。武器升级后，一次击发沿 26 度扇形喷出多发弹丸"
              "（始终为奇数发，正中一发直射锁定目标），弹丸命中飞行路线上的第一个敌人即结算。")
    h2(doc, "3.4  波次系统")
    body(doc, "开局三波为精心编排的手调节奏：第一波纯小艇错开预热，第二波武装大船登场并伴随"
              "四航道齐射，第三波为终局高潮。第四波起进入无尽模式，波次越高，敌船数量越多、"
              "齐射越频繁、出怪节奏越紧凑，没有通关上限，直至防线或主角被打穿。每波清完后有"
              "5 秒休整间歇，界面显示“下一波 X s”倒计时。武装大船卸完海盗后会滞留海面，向"
              "沙滩抛射炮弹：落点提前显示橙色预警圆盘，炮弹飞行缓慢，走位躲开即可；被炸中"
              "损失 30 点生命。")
    h2(doc, "3.5  敌人系统")
    body(doc, "敌人为渡海登陆的海盗，共四类：登陆小艇（3 点耐久，载 7 名海盗，击沉得 100 分）；"
              "武装大船（8 点耐久，载 12 名海盗并带炮台，击沉得 260 分）；普通海盗（1 点生命，"
              "涉水冲滩，抵达防线后挥刀破坏，每次漏防扣防线 10 点）；突袭型海盗（3 点生命，"
              "骷髅或船长外观，主动追击主角并近身挥砍，每刀造成 10 点伤害）。船只被击沉时"
              "播放沉船动画，海盗自浅滩涉水登陆，压迫感随波次递增。")
    h2(doc, "3.6  火药桶与连锁爆炸")
    body(doc, "沙滩、栈桥与敌船甲板上分布着可击爆的火药桶（1 点耐久，25 分）。火药桶爆炸对"
              "半径 4.6 米内的敌人造成 2 点伤害，并可以 0.12 秒的递延连锁引爆附近的其他火药桶，"
              "形成一串爆炸，成片消灭敌人。")
    h2(doc, "3.7  连杀爆炸")
    body(doc, "连击每累计满 3 次击杀，在击杀点触发一次大范围爆炸（半径 6 米），形成“击杀带来"
              "更多击杀”的正反馈。武器弹丸数达到 5 发后，触发间隔自动放宽为 15 次击杀，"
              "防止后期滚雪球式清屏，保持挑战平衡。")
    h2(doc, "3.8  局内成长系统")
    body(doc, "击杀敌人获得经验：普通海盗 1 点、小艇 6 点、武装大船 20 点、火药桶 2 点。累计"
              "经验推动武器共八档成长：扇形弹丸数由 1 发逐级提升至 3、5、7 发，最高档单发伤害"
              "提升至 2 点。每次升级时界面顶部提示“升级！弹丸 xN”，并伴随震动与音效反馈，"
              "力量增长直观可见。")
    h2(doc, "3.9  宠物狗伙伴")
    body(doc, "宠物狗全程跟随主角，平时在主角身边溜达；发现 11 米范围内的海盗会主动冲上去咬击"
              "（普通海盗一口即可消灭），是可靠的清怪助手。每一波敌人清完后，狗会跑回主角身边"
              "趴卧休息、喘气片刻。大船炮弹可以把狗炸飞上天翻滚，但狗不会死亡，落地后翻身"
              "爬起继续战斗。")
    h2(doc, "3.10  载具系统")
    body(doc, "码头旁停泊着玩家专属小船。步行靠近至 5 米以内时，界面右侧出现圆形“登船”按钮，"
              "点击后角色登上小船，此时摇杆改为驾驶船只：船速 8 米每秒，转向带惯性手感，可在"
              "开阔海域自由行驶（不可冲上沙滩）；船头左右双炮会自动向敌人开火。任意时刻点击"
              "“下船”按钮即可回到步行状态。")
    h2(doc, "3.11  计分与连击")
    body(doc, "击杀得分：普通海盗 50 分、登陆小艇 100 分、武装大船 260 分、火药桶 25 分。两次"
              "击杀间隔小于 2 秒连击数加一，每级连击额外奖励 10 分；连击达到 3、5、8 时播放"
              "激励旁白。屏幕右上实时显示总分与连击倍数。")
    h2(doc, "3.12  防线与生命")
    body(doc, "防线初始 300 点耐久，海盗突破至防线每次扣 10 点；主角生命 100 点，受击后拥有"
              "0.6 秒无敌时间，防止被连续命中。防线耐久归零或主角生命归零即判定败局。")
    h2(doc, "3.13  战斗反馈")
    body(doc, "击杀瞬间世界时间短暂放缓（击杀定格），屏幕按事件强度震动，击杀点向上飘出加分"
              "数字；子弹命中时屏幕中央闪现命中标记（普通命中为白色，强力命中为红色）；不同"
              "材质命中播放对应音效（木质、金属、肉体、水花等），每波开始与终结配有回合旁白。")
    h2(doc, "3.14  结算与重开")
    body(doc, "败局时弹出全屏结算面板：标题显示“防线失守”或“主角阵亡”，下方展示本局总分"
              "（SCORE），点击红色“再来一局”按钮立即开始新的一局。")

    # ---- 4 操作说明 ----
    h1(doc, "4  操作说明")
    h2(doc, "4.1  启动游戏")
    body(doc, "打开抖音 App，搜索“消灭海盗”或扫描小程序码进入，等待资源加载完成后自动进入"
              "战斗，无需注册登录。")
    shot(doc, "游戏开局画面（竖屏，主角驻守木台、海盗乘船来犯）")
    h2(doc, "4.2  移动操作")
    body(doc, "在屏幕左下区域按下手指，按下处即出现虚拟摇杆（浮动原点设计），按住并拖动控制"
              "主角向对应方向移动，拖动幅度越大移动越果断，松手即停。移动方向为屏幕绝对方向，"
              "上手零门槛。")
    shot(doc, "虚拟摇杆走位操作画面")
    h2(doc, "4.3  射击操作")
    body(doc, "射击全程自动完成，无需点击：武器自动锁定 45 米内最近的敌人开火。玩家通过走位"
              "拉开与突袭型海盗的距离、躲避炮弹预警落点、并把敌人引入火力覆盖区即可。")
    shot(doc, "战斗画面（自动开火、命中标记、飘分与连击显示）")
    h2(doc, "4.4  成长与升级")
    body(doc, "持续击杀积累经验，左上角 LV 后的经验条充满即升级，弹丸数提升，屏幕顶部出现"
              "“升级！弹丸 xN”提示。")
    shot(doc, "升级提示画面（顶部“升级！弹丸 x3”）")
    h2(doc, "4.5  躲避炮击")
    body(doc, "武装大船滞留时会向沙滩抛射炮弹，落点出现橙色预警圆盘，在炮弹落地前走出圆盘"
              "范围即可躲避。")
    shot(doc, "大船炮击画面（橙色落点预警圆盘）")
    h2(doc, "4.6  登船与下船")
    body(doc, "步行靠近码头小船，待右侧出现“登船”按钮后点击登船；驾船时摇杆控制航向，双炮"
              "自动开火；点击“下船”按钮返回步行。")
    shot(doc, "“登船”按钮显示画面")
    shot(doc, "驾船出海画面（船上双炮开火）")
    h2(doc, "4.7  重新开始")
    body(doc, "败局后在结算面板点击“再来一局”即可立即重开，无需等待。")
    shot(doc, "结算面板（“防线失守”/“主角阵亡” + SCORE + 再来一局按钮）")

    # ---- 5 界面说明 ----
    h1(doc, "5  界面说明")
    h2(doc, "5.1  游戏主界面")
    body(doc, "游戏主界面各区域功能如下：", indent=False)
    table(doc, [
        ("屏幕区域", "界面元素与含义"),
        ("左上", "WAVE 当前波次；HP 主角生命条；防线耐久条；LV 等级与经验条；文字提示区"
                  "（波次提示、下一波倒计时、升级提示）"),
        ("右上", "大号字体显示当前总分；其下金色字体显示连击倍数（xN）"),
        ("中央", "子弹命中时闪现 X 形命中标记：白色为普通命中，红色为强力命中"),
        ("左下", "虚拟摇杆底环，按住拖动控制主角移动"),
        ("右下", "弹药剩余数、装弹进度条、当前武器状态（弹丸 xN）；载具交互按钮"
                  "（登船/下船，按场景自动显示）"),
        ("全屏", "击杀时飘出金色加分数字，击杀瞬间画面短暂定格并伴随震动"),
    ])
    shot(doc, "游戏主界面全貌（标注各 HUD 区域）")
    h2(doc, "5.2  结算界面")
    body(doc, "结算界面为全屏半透明黑色面板：顶部大号标题显示败局原因（“防线失守”或“主角"
              "阵亡”），中部显示本局总分（SCORE 数字），底部为红色“再来一局”按钮。")
    shot(doc, "结算界面")

    # ---- 6 常见问题 ----
    h1(doc, "6  常见问题")
    body(doc, "（1）问：游戏需要注册账号吗？答：不需要，打开抖音 App 即可直接游玩。", indent=False)
    body(doc, "（2）问：游戏收费吗？答：本软件免费游玩，无内购项目。", indent=False)
    body(doc, "（3）问：支持横屏吗？答：软件按竖屏自适应设计，横竖屏均可正常显示与操作。", indent=False)
    body(doc, "（4）问：游戏失败后可以重开吗？答：可以，结算面板点击“再来一局”立即重新开始。", indent=False)
    body(doc, "（5）问：宠物狗会被打死吗？答：不会。狗只会被大船炮弹炸飞，落地后翻身爬起继续战斗。",
         indent=False)

    doc.core_properties.title = HEADER_TEXT + " 操作说明书"
    doc.save(MANUAL_DOCX)


# ---------------------------------------------------------------------
# 3. 申请表填报信息参考
# ---------------------------------------------------------------------
FORM_TEMPLATE = """# 《消灭海盗游戏软件 V1.0》软著申请表填报信息参考

> 在中国版权保护中心官网在线填报时，对照本文件逐项复制/填写。
> 标注「____」的项需申请人自行填写。

## 基本信息

| 表单项 | 填报内容 |
| --- | --- |
| 软件全称 | 消灭海盗游戏软件 |
| 软件简称 | 消灭海盗 |
| 版本号 | V1.0 |
| 软件分类 | 应用软件 → 游戏软件 |
| 开发方式 | 独立开发 |
| 权利取得方式 | 原始取得 |
| 权利范围 | 全部权利 |
| 著作权人 | ____（与账号实名认证的身份证姓名完全一致） |
| 开发完成日期 | ____年____月____日（填代码定稿日，须早于申请日） |
| 首次发表日期 | 建议选「未发表」；如已上线则填实际上线日（不得晚于申请日） |

## 技术与环境信息

| 表单项 | 填报内容 |
| --- | --- |
| 编程语言 | Lua、C++ |
| 代码量 | 约 {total_lines} 行（源程序总行数，鉴别材料提交前 30 页 + 后 30 页，共 {total_pages} 页中的 60 页） |
| 开发工具 | Visual Studio Code、rbfx Editor、抖音开发者工具 |
| 硬件环境 | 智能手机（Android 8.0+/iOS 12+，建议运行内存 4GB 以上） |
| 软件环境 | 抖音 App、抖音小游戏基础库 3.7.0 及以上 |

## 软件用途（可直接复制，可按表单字数限制删减）

「消灭海盗」是一款海岸题材的俯视角休闲射击小游戏。玩家操控海岸守卫在沙滩阵地上走位，
武器自动射击乘船登陆的海盗，保卫防线；内置波次挑战、武器成长、宠物伙伴、载具驾驶等玩法，
单局节奏紧凑，适合碎片化时间休闲娱乐。

## 软件功能和技术特点（可直接复制，可按表单字数限制删减）

1. 第三人称俯视角全自动射击，单摇杆走位即可上手，适配手机竖屏操作；
2. 波次导演系统：三波手调开场加无尽模式，四航道轮转、齐射与大船炮击，难度随波次递增；
3. 局内成长：击杀积累经验驱动武器八档成长，扇形弹丸数与伤害逐级提升；
4. 连杀触发范围爆炸、火药桶连锁引爆，形成击杀正反馈；
5. 宠物狗伙伴协同作战，可登船驾驶、海上自由探索；
6. 击杀定格、屏幕震动、飘分字、命中标记等多重打击感反馈；
7. 基于自研引擎 rbfx（Lua 脚本驱动），资源异步分包加载，竖屏自适应 UI。

## 鉴别材料清单

| 材料 | 文件 | 要求 |
| --- | --- | --- |
| 源程序 | 消灭海盗游戏软件V1.0_源程序.docx（另存/导出为 PDF 上传） | 前 30 页 + 后 30 页，每页 50 行，页眉含名称/版本/页码 |
| 文档 | 消灭海盗游戏软件V1.0_操作说明书.docx（替换截图占位后导出 PDF） | 不足 60 页全部提交，页眉同上 |
| 身份证明 | 身份证正反面照片/扫描件 | 实名认证时上传 |

## 一致性自查（提交前逐项核对）

- [ ] 申请表「软件全称」= 源程序页眉名称 = 说明书页眉名称 = 消灭海盗游戏软件
- [ ] 申请表「版本号」= 页眉版本号 = V1.0
- [ ] 著作权人姓名 = 账号实名认证姓名 = 身份证姓名
- [ ] 说明书中的游戏界面截图已替换占位符，且截图内游戏名称与「消灭海盗」一致
- [ ] 开发完成日期 ≤ 首次发表日期（如已发表）≤ 申请日期
- [ ] 计划上架抖音的游戏名称与软著名称保持一致（消灭海盗）
"""


def build_form_md(stats):
    with open(FORM_MD, "w", encoding="utf-8") as f:
        f.write(FORM_TEMPLATE.format(**stats))


def main():
    stats = build_source_doc()
    build_manual_doc()
    build_form_md(stats)
    print("=" * 60)
    print("源程序总行数: {total_lines}".format(**stats))
    print("按每页 {0} 行共 {1} 页，选取 {2} 页（前30+后30）".format(
        LINES_PER_PAGE, stats["total_pages"], stats["selected_pages"]))
    print("折行估算：最坏页（第 {0} 页）额外折行 {1} 行".format(
        stats["worst_page"], stats["worst_extra"]))
    print("已生成：")
    print("  " + SRC_DOCX)
    print("  " + MANUAL_DOCX)
    print("  " + FORM_MD)


if __name__ == "__main__":
    main()
