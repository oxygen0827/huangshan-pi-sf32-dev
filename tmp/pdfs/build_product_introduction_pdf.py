from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Flowable,
    HRFlowable,
    Image,
    KeepTogether,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "output/pdf/VibeBoard-Codex-Pet-产品介绍.pdf"

IMAGE_1 = Path(
    "/Users/hushaohong/Library/Containers/com.tencent.xinWeChat/Data/Documents/"
    "xwechat_files/wxid_o5w1stoyd63y22_c354/temp/RWTemp/2026-08/"
    "1fec60b98908d4a55466d1f6d84dbf46/0a4305a4b4c6ad94fc70c54be0a30d73.jpg"
)
IMAGE_2 = Path(
    "/var/folders/9g/k0tt7z_d47v7m7nslwjj36180000gn/T/TemporaryItems/"
    "NSIRD_screencaptureui_mhJ0bL/截屏2026-08-02 21.19.10.png"
)
IMAGE_3 = Path(
    "/var/folders/9g/k0tt7z_d47v7m7nslwjj36180000gn/T/TemporaryItems/"
    "NSIRD_screencaptureui_SZlDNy/截屏2026-08-02 21.19.23.png"
)

NAVY = colors.HexColor("#152238")
INK = colors.HexColor("#253247")
MUTED = colors.HexColor("#66758B")
TEAL = colors.HexColor("#198B93")
CORAL = colors.HexColor("#D96C58")
PALE_TEAL = colors.HexColor("#E9F5F3")
PALE_BLUE = colors.HexColor("#F1F5FA")
LINE = colors.HexColor("#D9E1EA")
FONT = "ArialUnicode"
FONT_PATH = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"


class ColorBand(Flowable):
    def __init__(self, height=0.32 * cm, color=NAVY):
        super().__init__()
        self.height = height
        self.color = color

    def wrap(self, avail_width, avail_height):
        self.width = avail_width
        return self.width, self.height

    def draw(self):
        self.canv.setFillColor(self.color)
        self.canv.rect(0, 0, self.width, self.height, fill=1, stroke=0)


def scaled_image(path, max_width, max_height):
    image = Image(str(path))
    aspect = image.imageWidth / image.imageHeight
    width = max_width
    height = width / aspect
    if height > max_height:
        height = max_height
        width = height * aspect
    image.drawWidth = width
    image.drawHeight = height
    image.hAlign = "CENTER"
    return image


def footer(canvas, doc):
    canvas.saveState()
    if doc.page > 1:
        canvas.setStrokeColor(LINE)
        canvas.setLineWidth(0.35)
        canvas.line(doc.leftMargin, 1.35 * cm, A4[0] - doc.rightMargin, 1.35 * cm)
        canvas.setFillColor(MUTED)
        canvas.setFont(FONT, 7.5)
        canvas.drawString(doc.leftMargin, 0.86 * cm, "VibeBoard / Codex Pet | 产品介绍")
        canvas.drawRightString(A4[0] - doc.rightMargin, 0.86 * cm, f"{doc.page}")
    canvas.restoreState()


def cover_footer(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(NAVY)
    canvas.rect(0, 0, A4[0], 0.34 * cm, fill=1, stroke=0)
    canvas.restoreState()


def bullet(text, styles):
    return Paragraph(f'<font color="#198B93">-</font> {text}', styles["bullet"])


def section_title(text, styles):
    return [
        Spacer(1, 0.18 * cm),
        Paragraph(text, styles["h1"]),
        HRFlowable(width="100%", thickness=0.7, color=LINE, spaceBefore=0.08 * cm, spaceAfter=0.22 * cm),
    ]


def photo_card(image_path, caption, styles, width, max_height):
    image = scaled_image(image_path, width, max_height)
    table = Table(
        [[image], [Paragraph(caption, styles["caption"])]],
        colWidths=[width],
        hAlign="CENTER",
    )
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), colors.white),
                ("BOX", (0, 0), (-1, -1), 0.5, LINE),
                ("LEFTPADDING", (0, 0), (-1, -1), 0.16 * cm),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0.16 * cm),
                ("TOPPADDING", (0, 0), (-1, 0), 0.16 * cm),
                ("BOTTOMPADDING", (0, 0), (-1, 0), 0.14 * cm),
                ("TOPPADDING", (0, 1), (-1, 1), 0.1 * cm),
                ("BOTTOMPADDING", (0, 1), (-1, 1), 0.18 * cm),
            ]
        )
    )
    return table


def build_pdf():
    for path in (IMAGE_1, IMAGE_2, IMAGE_3):
        if not path.is_file():
            raise FileNotFoundError(f"Missing product image: {path}")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    pdfmetrics.registerFont(TTFont(FONT, FONT_PATH))

    doc = SimpleDocTemplate(
        str(OUTPUT),
        pagesize=A4,
        leftMargin=1.7 * cm,
        rightMargin=1.7 * cm,
        topMargin=1.45 * cm,
        bottomMargin=1.75 * cm,
        title="VibeBoard / Codex Pet 产品介绍",
        author="VibeBoard",
        subject="AI 桌面伴侣与可编程硬件平台",
    )

    base = getSampleStyleSheet()
    styles = {
        "cover_kicker": ParagraphStyle(
            "cover_kicker", parent=base["Normal"], fontName="Helvetica-Bold", fontSize=9,
            leading=13, textColor=TEAL, alignment=TA_CENTER, spaceAfter=0.28 * cm,
        ),
        "cover_title": ParagraphStyle(
            "cover_title", parent=base["Normal"], fontName="Helvetica-Bold", fontSize=28,
            leading=34, textColor=NAVY, alignment=TA_CENTER, spaceAfter=0.18 * cm,
        ),
        "cover_cn": ParagraphStyle(
            "cover_cn", parent=base["Normal"], fontName=FONT, fontSize=17,
            leading=25, textColor=INK, alignment=TA_CENTER, spaceAfter=0.42 * cm,
        ),
        "cover_subtitle": ParagraphStyle(
            "cover_subtitle", parent=base["Normal"], fontName=FONT, fontSize=10.5,
            leading=17, textColor=MUTED, alignment=TA_CENTER,
        ),
        "h1": ParagraphStyle(
            "h1", parent=base["Normal"], fontName=FONT, fontSize=17,
            leading=23, textColor=NAVY, spaceBefore=0.14 * cm,
        ),
        "h2": ParagraphStyle(
            "h2", parent=base["Normal"], fontName=FONT, fontSize=12.5,
            leading=18, textColor=TEAL, spaceBefore=0.22 * cm, spaceAfter=0.12 * cm,
        ),
        "body": ParagraphStyle(
            "body", parent=base["Normal"], fontName=FONT, fontSize=9.6,
            leading=15.2, textColor=INK, spaceAfter=0.14 * cm,
        ),
        "bullet": ParagraphStyle(
            "bullet", parent=base["Normal"], fontName=FONT, fontSize=9.4,
            leading=14.8, textColor=INK, leftIndent=0.15 * cm, firstLineIndent=-0.15 * cm,
            spaceAfter=0.06 * cm,
        ),
        "small": ParagraphStyle(
            "small", parent=base["Normal"], fontName=FONT, fontSize=8.1,
            leading=12, textColor=MUTED,
        ),
        "caption": ParagraphStyle(
            "caption", parent=base["Normal"], fontName=FONT, fontSize=8.2,
            leading=11.5, textColor=MUTED, alignment=TA_CENTER,
        ),
        "table_header": ParagraphStyle(
            "table_header", parent=base["Normal"], fontName=FONT, fontSize=9,
            leading=13, textColor=colors.white,
        ),
        "table_cell": ParagraphStyle(
            "table_cell", parent=base["Normal"], fontName=FONT, fontSize=8.65,
            leading=12.4, textColor=INK,
        ),
        "callout": ParagraphStyle(
            "callout", parent=base["Normal"], fontName=FONT, fontSize=9.3,
            leading=14.6, textColor=INK,
        ),
    }

    story = []

    # Cover
    story.extend(
        [
            ColorBand(),
            Spacer(1, 0.78 * cm),
            Paragraph("AI DESKTOP COMPANION", styles["cover_kicker"]),
            Paragraph("VibeBoard", styles["cover_title"]),
            Paragraph("Codex Pet 产品介绍", styles["cover_cn"]),
            Paragraph("把 Codex 的工作过程，从电脑窗口带到现实桌面。", styles["cover_subtitle"]),
            Spacer(1, 0.55 * cm),
            scaled_image(IMAGE_1, 11.0 * cm, 12.1 * cm),
            Spacer(1, 0.36 * cm),
            Paragraph("开发者产品 / 原型产品 | 2026-08", styles["cover_subtitle"]),
        ]
    )
    story.append(PageBreak())

    # Product introduction and experience.
    story.extend(section_title("产品介绍", styles))
    story.append(
        Paragraph(
            "VibeBoard 是一套基于立创黄山派的低功耗智能硬件平台。它把带有 AMOLED 触摸屏、"
            "蓝牙、音频和运动传感器的开发板，做成一个可以安装 App、连接电脑和手机、持续扩展的产品底座。",
            styles["body"],
        )
    )
    story.append(
        Paragraph(
            "当前第一款产品形态是 Codex Pet: 桌面 AI 工作状态伴侣。它放在电脑旁边，通过蓝牙接收 "
            "Codex Desktop 的任务状态，让用户不用一直盯着电脑，也能知道 AI 正在工作、已经完成、"
            "遇到错误，或者正在等待审批。",
            styles["body"],
        )
    )
    callout = Table(
        [[Paragraph("<b>当前阶段</b>  开发者产品 / 原型产品。整机外壳、最终尺寸、量产 BOM 和正式零售价尚未定稿。", styles["callout"])]],
        colWidths=[doc.width],
    )
    callout.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), PALE_TEAL),
        ("LINEBEFORE", (0, 0), (0, -1), 3, TEAL),
        ("LEFTPADDING", (0, 0), (-1, -1), 0.35 * cm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0.28 * cm),
        ("TOPPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0.22 * cm),
    ]))
    story.extend([Spacer(1, 0.14 * cm), callout])

    story.append(Paragraph("核心体验", styles["h2"]))
    core_features = [
        "<b>实时状态展示</b>：显示 Codex 任务连接、运行、完成、阻塞、等待输入等状态。",
        "<b>桌面宠物</b>：宠物会根据任务状态播放待机、工作、等待、报错和完成动画。",
        "<b>审批交互</b>：遇到命令执行或文件修改审批时，板端显示 Allow / Deny，可通过物理按键处理。",
        "<b>任务切换</b>：在屏幕中部左右滑动，切换当前监控的 Codex 任务。",
        "<b>轻量互动</b>：点击宠物触发跳跃等动画，RGB 指示灯随系统状态变化。",
        "<b>可扩展 App</b>：固件烧录一次后，可以通过 BLE 从 Mac 或 iPhone 安装新的 Runtime App，不需要每次重新刷固件。",
    ]
    story.extend(bullet(item, styles) for item in core_features)

    story.append(Paragraph("平台能力", styles["h2"]))
    platform_features = [
        "Lua 5.5 Runtime 和受控 LVGL UI 能力。",
        "App 安装、启动、停止、删除和恢复。",
        "时钟、番茄钟、2048、贪吃蛇、打砖块等小游戏。",
        "加速度、陀螺仪、姿态和运动类应用。",
        "电池电压、充电状态、屏幕亮度、触摸和物理按键读取。",
        "RGB 灯控制、信息流展示和提醒。",
        "PCM WAV 音频播放，以及受控的短录音接口。",
        "手机/桌面端注入天气、AI 回复和其他云端信息。",
    ]
    story.extend(bullet(item, styles) for item in platform_features)
    story.append(
        Paragraph(
            "网络和云端能力由电脑或手机 Companion 负责，板子默认不直接提供 Wi-Fi、HTTP 服务或稳定的局域网地址。"
            "这样可以避免在设备上配置热点密码，也让板端 Runtime 保持简单、低功耗和可控。",
            styles["body"],
        )
    )

    # Hardware page.
    story.append(PageBreak())
    story.extend(section_title("硬件规格", styles))
    specs = [
        ("主控芯片", "思澈 SF32LB525UC6"),
        ("主控模组", "SF32LB52-MOD-1-N16R8"),
        ("CPU", "双核 Arm Cortex-M33，约 240 MHz + 24 MHz"),
        ("内存", "约 512 KB 级 SRAM、8 MB PSRAM"),
        ("Flash", "16 MB NOR Flash"),
        ("显示屏", "1.85 英寸 AMOLED，390 x 450，CO5300 驱动"),
        ("触摸", "FT6146 电容触摸"),
        ("无线", "双模 Bluetooth 5.3，当前以 BLE 为主"),
        ("音频", "板载麦克风、音频输入输出、外接扬声器接口"),
        ("电源", "AW32001 充电管理、VBAT 电压检测、锂电池供电"),
        ("扩展", "TF 卡、USB-UART、GPIO、RGB 灯、物理按键"),
    ]
    spec_rows = [[Paragraph("项目", styles["table_header"]), Paragraph("规格", styles["table_header"])]]
    spec_rows.extend(
        [Paragraph(key, styles["table_cell"]), Paragraph(value, styles["table_cell"])]
        for key, value in specs
    )
    table = Table(spec_rows, colWidths=[3.25 * cm, doc.width - 3.25 * cm], repeatRows=1)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("BACKGROUND", (0, 1), (-1, -1), colors.white),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE_BLUE]),
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("TOPPADDING", (0, 0), (-1, -1), 0.15 * cm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0.15 * cm),
    ]))
    story.append(table)
    story.append(Spacer(1, 0.18 * cm))
    story.append(
        Paragraph(
            "官方资料将黄山派定位为低功耗多媒体显示和智能穿戴原型平台，标配 390 x 450 屏、"
            "8 MB PSRAM 和 16 MB NOR Flash。",
            styles["body"],
        )
    )
    sources = [
        "SiFli 黄山派板级资料：https://docs.sifli.com/projects/solution/open-source-community/board_introduction.html",
        "立创黄山派产品资料：https://www.szlcsc.com/selection/AAECAB66A41FA34FF49377D262B8CB61.html",
    ]
    story.extend(bullet(item, styles) for item in sources)

    story.append(Paragraph("传感器规格与实测边界", styles["h2"]))
    story.append(
        Paragraph(
            "官方板卡资料宣传的是“九轴传感器 + 光感”，理论器件组合为 LSM6DSL（三轴加速度计 + 三轴陀螺仪）、"
            "MMC56X3（三轴磁力计，可用于电子罗盘）和 LTR303（环境光传感器）。",
            styles["body"],
        )
    )
    sensor_rows = [
        [Paragraph("器件", styles["table_header"]), Paragraph("I2C 地址", styles["table_header"]), Paragraph("当前实测", styles["table_header"])],
        [Paragraph("LSM6DSL", styles["table_cell"]), Paragraph("0x6a", styles["table_cell"]), Paragraph("已应答，确认在线", styles["table_cell"])],
        [Paragraph("LTR303", styles["table_cell"]), Paragraph("0x29", styles["table_cell"]), Paragraph("未应答", styles["table_cell"])],
        [Paragraph("MMC56X3", styles["table_cell"]), Paragraph("0x30", styles["table_cell"]), Paragraph("未应答", styles["table_cell"])],
    ]
    sensor_table = Table(sensor_rows, colWidths=[3.35 * cm, 3.0 * cm, doc.width - 6.35 * cm], repeatRows=1)
    sensor_table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE_BLUE]),
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("TOPPADDING", (0, 0), (-1, -1), 0.16 * cm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0.16 * cm),
    ]))
    story.append(sensor_table)
    story.append(Spacer(1, 0.14 * cm))
    story.append(
        Paragraph(
            "当前可以稳定承诺的是加速度和陀螺仪数据，以及基于 IMU 的姿态、倾斜、运动和手势类功能。"
            "步数等运动应用接口仍需按最终产品算法做实测。光照和电子罗盘暂时不能作为当前产品的确定功能对外承诺。"
            "Runtime 会先探测硬件，缺失传感器时降级显示，不会因 I2C 未应答而崩溃。",
            styles["body"],
        )
    )

    # Size and pricing page.
    story.append(PageBreak())
    story.extend(section_title("尺寸与价格规划", styles))
    story.append(Paragraph("尺寸规划", styles["h2"]))
    size_features = [
        "屏幕：390 x 450 像素，约 1.85 英寸。",
        "主控模组：约 27.9 x 18.7 mm 级别。",
        "整块开发板和最终外壳：尚未形成正式产品图纸。",
    ]
    story.extend(bullet(item, styles) for item in size_features)
    story.append(
        Paragraph(
            "如果继续做桌面 Codex Pet，建议第一版外壳先按约 68 x 48 mm 的小型设备规划，再根据电池、"
            "扬声器、按键和天线空间调整厚度。这个尺寸只能作为产品定义起点，不能视为最终量产规格。",
            styles["body"],
        )
    )
    story.append(Paragraph("价格规划", styles["h2"]))
    story.append(
        Paragraph(
            "项目目前没有正式售价。黄山派开发板公开资料的常规价格约为 133 元，过去曾有 98 元的限时活动价，"
            "但该活动不应当视为长期价格。完整 Codex Pet 套装还需要增加外壳、电池、扬声器、装配、包装和 Companion 体验。",
            styles["body"],
        )
    )
    price_rows = [
        [Paragraph("版本", styles["table_header"]), Paragraph("建议售价", styles["table_header"])],
        [Paragraph("开发者裸板版", styles["table_cell"]), Paragraph("129 - 149 元", styles["table_cell"])],
        [Paragraph("Codex Pet 基础套装", styles["table_cell"]), Paragraph("199 - 249 元", styles["table_cell"])],
        [Paragraph("完整外壳/电池/扬声器套装", styles["table_cell"]), Paragraph("249 - 299 元", styles["table_cell"])],
    ]
    price_table = Table(price_rows, colWidths=[doc.width * 0.68, doc.width * 0.32], repeatRows=1)
    price_table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE_BLUE]),
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0.22 * cm),
        ("TOPPADDING", (0, 0), (-1, -1), 0.16 * cm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0.16 * cm),
    ]))
    story.append(price_table)
    story.append(Spacer(1, 0.16 * cm))
    story.append(
        Paragraph(
            "当前比较合适的产品锚定价是 249 元左右。这个价格对应的是“AI 桌面伴侣 + 可编程硬件平台”，"
            "而不是具备防水、心率、GPS 和成熟健康功能的消费级智能手表。",
            styles["body"],
        )
    )

    story.append(Paragraph("产品定位", styles["h2"]))
    positioning = [
        "它是一个有真实屏幕和实体交互的 AI 状态终端。",
        "它能把 Codex 的工作过程从电脑窗口带到现实桌面。",
        "它不是一次性 Demo，而是可以持续安装新 App 的硬件平台。",
        "它同时适合开发者工具、桌面宠物、传感器实验、小游戏和智能穿戴原型。",
    ]
    story.extend(bullet(item, styles) for item in positioning)

    story.append(Paragraph("当前项目参考", styles["h2"]))
    references = [
        "项目开发基座 README：README.md",
        "Codex Pet Companion 说明：codex-pet-companion/README.md",
        "Runtime 产品边界：docs/runtime-boundary.md",
        "蓝牙与联网边界：docs/huangshan-networking.md",
    ]
    story.extend(bullet(item, styles) for item in references)

    # Product photo gallery.
    story.append(PageBreak())
    story.extend(section_title("产品实拍", styles))
    story.append(
        Paragraph(
            "以下为当前 Codex Pet 在黄山派设备上的实机运行画面，分别展示任务运行、思考和完成后的不同状态。",
            styles["body"],
        )
    )
    hero_card = photo_card(IMAGE_1, "任务运行中：宠物动画与 Running 状态", styles, 7.2 * cm, 9.1 * cm)
    story.append(Table([[hero_card]], colWidths=[doc.width], hAlign="CENTER", style=[("ALIGN", (0, 0), (-1, -1), "CENTER")]))
    story.append(Spacer(1, 0.26 * cm))
    left_card = photo_card(IMAGE_2, "运行 / 思考状态", styles, 5.1 * cm, 6.7 * cm)
    right_card = photo_card(IMAGE_3, "任务完成状态", styles, 5.1 * cm, 6.7 * cm)
    gallery = Table([[left_card, right_card]], colWidths=[doc.width / 2, doc.width / 2], hAlign="CENTER")
    gallery.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("ALIGN", (0, 0), (-1, -1), "CENTER"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0.12 * cm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0.12 * cm),
        ("TOPPADDING", (0, 0), (-1, -1), 0),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
    ]))
    story.append(gallery)

    doc.build(story, onFirstPage=cover_footer, onLaterPages=footer)
    print(OUTPUT)


if __name__ == "__main__":
    build_pdf()
