"""Generate a functional wiring diagram; positions are NOT connector orientation."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Ai_Read" / "robot_wiring.png"
im = Image.new("RGB", (1800, 1320), "#f3f6fa")
d = ImageDraw.Draw(im)
INK, BLUE, ORANGE, RED, GND = "#152438", "#136dc0", "#c25c10", "#bb2442", "#384456"

def font(size=25, bold=False):
    return ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc", size)

def text(x, y, value, size=25, color=INK, bold=False):
    d.text((x, y), value, font=font(size, bold), fill=color)

def box(x, y, w, h, title, subtitle):
    d.rounded_rectangle((x, y, x+w, y+h), radius=20, fill="white", outline="#c7d2e0", width=2)
    text(x+24, y+20, title, 30, bold=True)
    text(x+24, y+67, subtitle, 23, "#526175")

def wire(points, color, arrow=False):
    d.line(points, fill=color, width=5, joint="curve")
    if arrow:
        x,y = points[-1]; prevx,prevy = points[-2]
        sign = 1 if x > prevx else -1
        d.polygon([(x,y),(x-sign*14,y-8),(x-sign*14,y+8)], fill=color)

def dot(x,y,color=GND):
    d.ellipse((x-6,y-6,x+6,y+6),fill=color)

text(60, 30, "Edgi-Talk × Arduino UNO · 小智 AI 舵机接线图", 42, bold=True)
text(60, 95, "按已提供接线：CN5 物理脚号 / uart5 / 38400 8N1；图中位置不代表排针朝向", 26)
box(60, 165, 480, 490, "Edgi-Talk / M55", "板载电平转换后的 3.3 V 排针")
box(710, 165, 380, 490, "HW-221 / TXS0108E", "A 侧 3.3 V    ·    B 侧 5 V")
box(1260, 165, 480, 490, "Arduino UNO", "D2 / D3 软件串口；USB 可上传")

for y, left, right, color, reverse in [
    (320,"物理 8：TX / P17.1","D2：RX",BLUE,False),
    (410,"物理 10：RX / P17.0","D3：TX",ORANGE,True),
]:
    text(85,y-38,left,27,color)
    text(1310,y-38,right,27,color)
    text(730,y-38,"A1" if y==320 else "A2",26,color)
    text(1015,y-38,"B1" if y==320 else "B2",26,color)
    wire([(540,y),(710,y)],color,not reverse)
    wire([(760,y),(1040,y)],color,not reverse)
    wire([(1090,y),(1260,y)],color,not reverse)
    if reverse:
        for a,b in [(540,710),(760,1040),(1090,1260)]: wire([(b,y),(a,y)],color,True)

text(85,472,"物理 1 或 17：3.3 V",26,RED)
text(730,472,"VCCA",26,RED)
text(982,472,"VCCB",26,RED)
text(1310,472,"5 V 逻辑电源",26,RED)
wire([(540,510),(710,510)],RED)
wire([(1090,510),(1260,510)],RED)
wire([(630,510),(630,590),(710,590)],RED)
dot(630,510,RED)
text(730,556,"OE = 3.3 V＊",26,RED)
text(85,571,"物理 6（或其他 GND）",25)
text(1310,571,"GND",26)
text(960,601,"GND",25)
wire([(300,620),(300,710),(1510,710),(1510,620)],GND)
wire([(1000,640),(1000,710)],GND); dot(1000,710)
text(590,724,"公共地：Edgi + HW-221 + UNO + 舵机电源负极",25,bold=True)

box(60,800,480,360,"UNO 扩展板信号引出","仅使用 D9 / D10 对应的 S（信号）")
box(710,800,420,360,"Pan / Tilt 两个舵机","电源电压按具体舵机铭牌确认")
box(1300,800,440,360,"独立舵机电源","电流容量满足两个舵机同时启动")
text(85,925,"D9  → Pan 信号",27,BLUE)
text(85,1005,"D10 → Tilt 信号",27,ORANGE)
text(735,925,"Pan · 水平舵机 S",26,BLUE)
text(735,1005,"Tilt · 俯仰舵机 S",26,ORANGE)
wire([(540,965),(710,965)],BLUE,True)
wire([(540,1045),(710,1045)],ORANGE,True)
text(735,1070,"两舵机 V+（正极）",25,RED)
text(735,1115,"两舵机 GND（负极）",23)
text(85,1080,"GND → 公共地",25)
text(1325,950,"正极 → 两舵机 V+",26,RED)
wire([(1300,1000),(1170,1000),(1170,1100),(1130,1100)],RED,True)
text(1325,1080,"负极 → 公共地",26)
wire([(1510,1130),(1510,1200),(300,1200),(300,1130)],GND)
wire([(920,1140),(920,1200)],GND); dot(920,1200)
text(570,1210,"此地线接上方公共地；独立电源正极不接 UNO / Edgi 的 5 V",25,bold=True)
text(60,1260,"＊OE 按原图常接 3.3 V；严谨的上掉电设计应在两侧电源稳定后使能。扩展板 V 电源跳线须先确认隔离。",23)
im.save(OUT)
print(OUT)
