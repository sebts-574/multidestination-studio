"""Multidestination Studio 1.1.0 - User Guide (PDF).

Screenshots: PNG files in the folder given as argv[2], named as in the shot() calls;
missing ones are drawn as labelled placeholders (with their file name).
"""
import os
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, Image, KeepTogether, NextPageTemplate, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.flowables import Flowable
from reportlab.platypus.tableofcontents import TableOfContents

OUT = sys.argv[1] if len(sys.argv) > 1 else "Multidestination-Studio-1.0-User-Guide.pdf"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "shots"

FONT_DIR = "/usr/share/fonts/truetype/dejavu"
pdfmetrics.registerFont(TTFont("Sans", f"{FONT_DIR}/DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Bold", f"{FONT_DIR}/DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Oblique", f"{FONT_DIR}/DejaVuSans-Oblique.ttf"))
pdfmetrics.registerFont(TTFont("Sans-BoldOblique", f"{FONT_DIR}/DejaVuSans-BoldOblique.ttf"))
pdfmetrics.registerFont(TTFont("Mono", f"{FONT_DIR}/DejaVuSansMono.ttf"))
from reportlab.pdfbase.pdfmetrics import registerFontFamily  # noqa: E402

registerFontFamily("Sans", normal="Sans", bold="Sans-Bold", italic="Sans-Oblique", boldItalic="Sans-BoldOblique")

INK = colors.HexColor("#1D2230")
MUTED = colors.HexColor("#5B6170")
ACCENT = colors.HexColor("#2B3FD6")
LINE = colors.HexColor("#D9DCE3")
SOFT = colors.HexColor("#F3F4F8")
RED = colors.HexColor("#B83232")

body = ParagraphStyle("body", fontName="Sans", fontSize=10, leading=15, textColor=INK, spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=8.5, leading=12, textColor=MUTED)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=3, spaceAfter=3)
h1 = ParagraphStyle("h1", fontName="Sans-Bold", fontSize=20, leading=25, textColor=INK, spaceBefore=4,
                    spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Sans-Bold", fontSize=13, leading=17, textColor=ACCENT, spaceBefore=12,
                    spaceAfter=5, keepWithNext=1)
h1.keepWithNext = 1
toc_title = ParagraphStyle("toc_title", parent=h1)
caption = ParagraphStyle("caption", parent=small, alignment=TA_CENTER, spaceBefore=4, spaceAfter=12)
cell = ParagraphStyle("cell", parent=body, fontSize=9, leading=12.5, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Sans-Bold")
note = ParagraphStyle("note", parent=body, fontSize=9.5, leading=14, textColor=INK, spaceAfter=0)

shot_counter = [0]


class Doc(BaseDocTemplate):
    def afterFlowable(self, f):
        if isinstance(f, Paragraph) and f.style.name in ("h1", "h2"):
            level = 0 if f.style.name == "h1" else 1
            text = f.getPlainText()
            key = f"h{id(f)}"
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text, key, level=level, closed=level > 0)
            if level == 0:
                self.notify("TOCEntry", (level, text, self.page, key))


def P(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph(i, bullet, bulletText="•") for i in items]


def steps(items):
    return [Paragraph(i, bullet, bulletText=f"{n}.") for n, i in enumerate(items, 1)]


def callout(text, title="Tip"):
    t = Table([[Paragraph(f"<b>{title}.</b> {text}", note)]], colWidths=[165 * mm])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), SOFT),
        ("LINEBEFORE", (0, 0), (0, -1), 3, ACCENT),
        ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 10),
        ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
    ]))
    return [t, Spacer(1, 8)]


def table(rows, widths, header=True):
    data = [[Paragraph(c, cellb if (header and r == 0) else cell) for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if header else 0)
    style = [
        ("GRID", (0, 0), (-1, -1), 0.5, LINE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0), SOFT))
    t.setStyle(TableStyle(style))
    return [t, Spacer(1, 10)]


class Placeholder(Flowable):
    def __init__(self, num, text, w, h):
        super().__init__()
        self.num, self.text, self.w, self.h = num, text, w, h

    def wrap(self, aw, ah):
        return self.w, self.h

    def draw(self):
        c = self.canv
        c.setFillColor(SOFT)
        c.setStrokeColor(LINE)
        c.setDash(4, 3)
        c.roundRect(0, 0, self.w, self.h, 6, fill=1, stroke=1)
        c.setDash()
        c.setFillColor(MUTED)
        c.setFont("Sans-Bold", 11)
        c.drawCentredString(self.w / 2, self.h / 2 + 6, f"Screenshot {self.num}")
        c.setFont("Sans", 9)
        c.drawCentredString(self.w / 2, self.h / 2 - 10, self.text)


def shot(text, height=80, file=None):
    """Screenshot with caption; placeholder (with the expected file name) if the PNG is missing."""
    shot_counter[0] += 1
    n = shot_counter[0]
    name = file or f"shot{n:02d}.png"
    path = os.path.join(SHOTS, name)
    w = 165 * mm
    if os.path.exists(path):
        img = Image(path)
        ratio = img.imageHeight / img.imageWidth
        # Natural size at about 96 dpi, never wider than the text column.
        iw = min(w, img.imageWidth * 0.26 * mm)
        h = min(iw * ratio, 120 * mm)
        img = Image(path, width=h / ratio, height=h)
        img.hAlign = "CENTER"
        flow = img
    else:
        flow = Placeholder(n, f"To be provided: {name}", w, height * mm)
    return [KeepTogether([flow, Paragraph(f"<b>Figure {n}.</b> {text}", caption)])]


class ConceptDiagram(Flowable):
    """Scenes → destinations → outputs."""

    def wrap(self, aw, ah):
        self.width = 165 * mm
        return self.width, 70 * mm

    def box(self, x, y, w, h, title, sub, fill, stroke):
        c = self.canv
        c.setFillColor(fill)
        c.setStrokeColor(stroke)
        c.setLineWidth(0.8)
        c.roundRect(x, y, w, h, 5, fill=1, stroke=1)
        c.setFillColor(INK)
        c.setFont("Sans-Bold", 9)
        c.drawCentredString(x + w / 2, y + h / 2 + 3, title)
        c.setFont("Sans", 7.5)
        c.setFillColor(MUTED)
        c.drawCentredString(x + w / 2, y + h / 2 - 9, sub)

    def arrow(self, x1, y1, x2, y2):
        c = self.canv
        c.setStrokeColor(MUTED)
        c.setLineWidth(0.8)
        c.line(x1, y1, x2, y2)
        import math
        a = math.atan2(y2 - y1, x2 - x1)
        for d in (2.6, -2.6):
            c.line(x2, y2, x2 - 6 * math.cos(a + d / 6), y2 - 6 * math.sin(a + d / 6))

    def draw(self):
        c = self.canv
        blue_f, blue_s = colors.HexColor("#E6EAFB"), ACCENT
        teal_f, teal_s = colors.HexColor("#E1F5EE"), colors.HexColor("#0F6E56")
        gray_f, gray_s = SOFT, colors.HexColor("#888780")
        W = self.width
        c.setFont("Sans-Bold", 8)
        c.setFillColor(MUTED)
        c.drawString(0, 190, "SHARED SCENES")
        c.drawString(W * 0.36, 190, "DESTINATIONS")
        c.drawString(W * 0.76, 190, "OUTPUTS")
        bw, bh = 120, 34
        scenes = [("Opening", "covers all"), ("Keynote", "covers 2 of 3"), ("Break", "covers 1 of 3")]
        dests = [("Default (OBS)", "1920 × 1080 · stream"), ("LED wall", "3840 × 1080"),
                 ("Projection", "3500 × 1080 · blended")]
        outs = [("Stream / record", "OBS outputs"), ("LED processor", "screen 2"),
                ("Projector L + R", "screens 3 and 4")]
        ys = [140, 85, 30]
        for (t, s), y in zip(scenes, ys):
            self.box(0, y, bw, bh, t, s, blue_f, blue_s)
        for (t, s), y in zip(dests, ys):
            self.box(W * 0.36, y, bw + 20, bh, t, s, teal_f, teal_s)
        for (t, s), y in zip(outs, ys):
            self.box(W * 0.76, y, bw, bh, t, s, gray_f, gray_s)
        for y in ys:
            self.arrow(bw + 2, y + bh / 2, W * 0.36 - 3, y + bh / 2)
            self.arrow(W * 0.36 + bw + 22, y + bh / 2, W * 0.76 - 3, y + bh / 2)
        self.arrow(bw + 2, 140 + bh / 2, W * 0.36 - 3, 85 + bh / 2 + 6)
        self.arrow(bw + 2, 85 + bh / 2, W * 0.36 - 3, 30 + bh / 2 + 6)
        c.setFont("Sans-Oblique", 8)
        c.setFillColor(MUTED)
        c.drawCentredString(W / 2, 6, "Each scene covers some destinations. Each destination keeps its own layout "
                                      "of the scene and feeds its own outputs.")


def on_page(canv, doc):
    canv.saveState()
    canv.setFont("Sans", 8)
    canv.setFillColor(MUTED)
    canv.drawString(20 * mm, 12 * mm, "Multidestination Studio 1.1.0 · User Guide")
    canv.drawRightString(190 * mm, 12 * mm, str(doc.page))
    canv.setStrokeColor(LINE)
    canv.line(20 * mm, 16 * mm, 190 * mm, 16 * mm)
    canv.restoreState()


def on_cover(canv, doc):
    canv.saveState()
    W, H = A4
    canv.setFillColor(colors.HexColor("#14182A"))
    canv.rect(0, 0, W, H, fill=1, stroke=0)
    canv.setFillColor(ACCENT)
    canv.rect(0, H * 0.43, W, 4, fill=1, stroke=0)
    # stylised Preview / Program tiles
    for i, x in enumerate((25 * mm, 112 * mm)):
        for j in range(3):
            y = H - 95 * mm - j * 24 * mm
            canv.setFillColor(colors.HexColor("#22283F"))
            canv.setStrokeColor(colors.HexColor("#3A4366") if i == 0 else colors.HexColor("#8E2A2A"))
            canv.roundRect(x, y, 73 * mm, 20 * mm, 3, fill=1, stroke=1)
    canv.setFillColor(colors.white)
    canv.setFont("Sans-Bold", 11)
    canv.drawCentredString(61.5 * mm, H - 22 * mm - 3 * mm, "Preview")
    canv.drawCentredString(148.5 * mm, H - 22 * mm - 3 * mm, "Program")
    canv.setFont("Sans-Bold", 34)
    canv.drawString(25 * mm, H * 0.33, "Multidestination Studio")
    canv.setFont("Sans", 15)
    canv.setFillColor(colors.HexColor("#B9C0DA"))
    canv.drawString(25 * mm, H * 0.33 - 13 * mm, "User Guide · Version 1.1.0")
    canv.setFont("Sans", 11)
    canv.drawString(25 * mm, H * 0.33 - 24 * mm, "A plugin for OBS Studio · Windows, macOS, Linux")
    canv.setFont("Sans", 9)
    canv.drawString(25 * mm, 20 * mm, "© 2026 Sébastien Tombre · GPL-2.0-or-later")
    canv.restoreState()


def build():
    doc = Doc(OUT, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=20 * mm,
              bottomMargin=22 * mm, title="Multidestination Studio 1.1.0 - User Guide", author="Sébastien Tombre",
              subject="User guide for the Multidestination Studio plugin for OBS Studio")
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")
    doc.addPageTemplates([PageTemplate("cover", frames=[frame], onPage=on_cover),
                          PageTemplate("body", frames=[frame], onPage=on_page)])
    s = [NextPageTemplate("body"), PageBreak()]

    toc = TableOfContents()
    toc.levelStyles = [
        ParagraphStyle("t0", fontName="Sans-Bold", fontSize=11, leading=22, textColor=INK),
        ParagraphStyle("t1", fontName="Sans", fontSize=8.5, leading=11, leftIndent=14, textColor=MUTED),
    ]
    s += [P("Contents", toc_title), toc, PageBreak()]

    # 1 -------------------------------------------------------------
    s += [P("1. What is Multidestination Studio?", h1)]
    s += [P("At a conference, a concert or a church service, the video team rarely sends the same picture "
            "everywhere. The LED wall behind the stage, a pair of projectors on the side screens, the "
            "confidence monitor for the speaker and the live stream all need different content, at "
            "different resolutions, switched at the same moment.")]
    s += [P("<b>Multidestination Studio</b> is a free plugin for OBS Studio that does exactly this. It adds "
            "independent <b>destinations</b> to OBS, each with its own canvas, resolution, Preview and "
            "Program, and lets you run all of them from one studio window with a single <b>Transition</b>. "
            "You keep everything you already know from OBS: sources, filters, transitions, audio, streaming "
            "and recording.")]
    s += [P("What it gives you", h2)]
    s += bullets([
        "<b>Several outputs, one operator.</b> Every destination has its own picture, but one scene list and one "
        "Transition drive them all.",
        "<b>A different layout per screen.</b> The same scene can show a full-screen camera on the stream, a wide "
        "graphic on the LED wall and nothing at all on the projectors.",
        "<b>Projector blending built in.</b> Spread one destination across several projectors with soft-edge "
        "blending, gamma, black level and keystone correction.",
        "<b>A safe live output.</b> Changes you prepare in Preview never touch what is on air, unless you decide "
        "to edit the Program directly.",
        "<b>Cues</b> (optional): play several scenes one after the other, with waits and per-scene transitions.",
        "<b>Companion and Stream Deck</b>: a ready-made Companion module with buttons for scenes, transitions, "
        "settings and cues.",
    ])
    s += [P("Requirements", h2)]
    s += table([
        ["Item", "Requirement"],
        ["OBS Studio", "Version 31.1 or later"],
        ["Systems", "Windows 10/11 (64-bit), macOS (Apple Silicon and Intel), Ubuntu 24.04 (x86_64)"],
        ["Hardware", "One graphics card able to render every destination at your OBS frame rate. Each extra "
                     "screen used for output must be connected to the computer."],
        ["Companion (optional)", "A recent Bitfocus Companion, to control the studio from a Stream Deck "
                                 "(chapter 12)."],
        ["Languages", "English, French, Norwegian (Bokmål)"],
    ], [38, 127])

    # 2 -------------------------------------------------------------
    s += [PageBreak(), P("2. Key concepts", h1)]
    s += [ConceptDiagram(), Spacer(1, 6)]
    s += table([
        ["Concept", "Meaning"],
        ["Destination", "An independent video output with its own canvas and resolution: a screen, an LED wall, "
                        "a group of blended projectors. The <b>default destination</b> is the OBS main canvas, "
                        "the one OBS streams and records."],
        ["Scene", "Scenes are shared. A scene lists the destinations it <b>covers</b>; each covered destination "
                  "has its own version of the scene, with its own sources and layout."],
        ["Folder", "Scenes can be arranged in folders. A folder only organises the list: it is never taken "
                   "live."],
        ["Preview / Program", "As in the OBS Studio Mode: Preview shows what is coming next, Program shows what "
                              "is live. Each destination has both."],
        ["Transition", "Sends the Preview scene to Program on every destination it covers, at the same moment, "
                       "with the OBS transition. <b>Quick transitions</b> and the <b>T-bar</b> do the same with "
                       "another transition or by hand."],
        ["Lock", "A locked destination ignores transitions and keeps its current Program."],
        ["Modify PGM", "Program shows a copy of the scene taken live. Modify PGM lets you edit that live "
                       "picture directly; the changes are also kept in the scene."],
        ["Output blending", "A destination split across several outputs (usually projectors) whose overlaps are "
                            "softly blended to look like one seamless image."],
        ["Cue", "A list of scenes taken live one after the other, each with its own waits and transition."],
    ], [35, 130])
    s += callout("A destination that a scene does not cover is not affected when that scene is taken: it simply "
                 "keeps showing its current Program. This lets you change one screen without touching the "
                 "others.", "Good to know")

    # 3 -------------------------------------------------------------
    s += [PageBreak(), P("3. Installation and first launch", h1)]
    s += [P("Close OBS, then install the package for your system, downloaded from the project's GitHub "
            "Releases page.")]
    s += table([
        ["System", "Package", "Notes"],
        ["Windows", "multidestination-studio-1.1.0-windows-x64-installer.exe",
         "The installer is not code-signed: Windows SmartScreen or Defender may ask you to confirm."],
        ["macOS", "Universal .pkg package", "Not signed: open System Settings → Privacy &amp; Security → Open "
                                            "Anyway."],
        ["Ubuntu", "multidestination-studio-1.1.0-x86_64-linux-gnu.deb", "sudo apt install ./ followed by the "
                                                                         "file name."],
        ["Companion", "obs-studio-4.0.0-multidestination.1.tgz", "Optional: the Companion module (chapter 12)."],
    ], [22, 70, 73])
    s += [P("Start OBS. A <b>Multidestination Studio</b> button appears in the OBS <b>Controls</b> dock, next to "
            "<b>Studio Mode</b>. The same command is in the <b>Tools</b> menu, and you can assign a hotkey to it "
            "in OBS Settings → Hotkeys.")]
    s += shot("The Multidestination Studio button in the OBS Controls dock.", 55, "shot01.png")
    s += [P("Multidestination Studio opens full screen on Windows and Linux, and in a large window on macOS. "
            "When it opens, OBS Studio Mode is switched on (and switched off again when you leave, if it was "
            "off), so that the default destination also has a real Preview and Program.")]
    s += [P("The studio is a window of its own, like an OBS projector: it has its own entry in the taskbar "
            "(the Dock and Mission Control on macOS). After minimizing it, click that entry to come back. To "
            "leave the studio, use <b>File → Exit Multidestination Studio</b>, the <b>Quit</b> button in the "
            "top-right corner (full screen), the <b>Exit</b> button in the Controls dock, or <b>Esc</b>.")]
    s += callout("To have the studio open automatically every time OBS starts, turn on <b>Configuration "
                 "Settings → Startup Settings → Launch Multidestination Studio on startup</b>.")

    # 4 -------------------------------------------------------------
    s += [PageBreak(), P("4. A tour of the studio", h1)]
    s += shot("The Multidestination Studio window with three destinations.", 95, "studio-window.png")
    s += [P("Menus", h2)]
    s += table([
        ["Menu", "Contents"],
        ["File", "Exit Multidestination Studio."],
        ["Edit", "Undo, copy and paste, group, remove, transform, order, properties and filters for the selected "
                 "source (chapter 7)."],
        ["Destinations", "One submenu per destination, Manage destinations, Output blending, New destination "
                         "(chapters 5 and 10)."],
        ["Docks", "Show or hide each dock, Reset docks, Fullscreen (F11)."],
        ["Configuration Settings", "After a transition, Destinations shown, Cues, Startup Settings, Language "
                                   "(chapter 13)."],
    ], [42, 123])
    s += [P("Next to the menus, an information hint reminds you that <b>F11</b> switches full screen on or "
            "off. In full screen, moving the mouse to the top-right corner reveals <b>Minimize</b>, <b>Exit "
            "fullscreen</b> and <b>Quit</b>; they disappear when the mouse moves away.")]
    s += shot("Top-right window controls in full screen, and the F11 hint next to the menus.", 40, "shot03.png")
    s += [P("Preview and Program", h2)]
    s += [P("The <b>Preview</b> column (left) and the <b>Program</b> column (right) show one tile per "
            "destination, stacked and scaled to fit the window. Each tile has a header with the destination "
            "name and resolution (in Program, also the scene on air) and two buttons:")]
    s += bullets([
        "<b>Enlarge</b> shows only this destination, much larger, for precise placement. <b>Show all</b> brings "
        "the other destinations back.",
        "<b>Lock</b> (Preview only) keeps this destination out of transitions.",
    ])
    s += [P("When space is short, the buttons shrink to their icons and the name text scales down so it always "
            "remains complete. Between the columns are <b>Modify PGM</b>, then <b>Transition</b> with its "
            "<b>⋮</b> menu, the <b>Quick transitions</b> and the <b>T-bar</b> (chapter 8).")]
    s += shot("A tile header: destination name, Enlarge and Lock (unlocked and locked).", 35, "shot04.png")
    s += [P("Docks", h2)]
    s += [P("By default the docks sit side by side at the bottom of the window: <b>Scenes</b>, <b>Sources</b>, "
            "<b>Cues</b> (when the Cues feature is on), <b>Media controls</b>, <b>Audio mixer</b>, <b>Scene "
            "transitions</b> and <b>Controls</b>. They can be moved, resized, floated or closed like OBS docks; "
            "the arrangement is saved with your scene collection. <b>Docks → Reset docks</b> restores the default "
            "layout. The Scenes, Sources and Cues docks look and behave like those of OBS, with the same toolbar "
            "icons.")]
    s += table([
        ["Dock", "Role"],
        ["Scenes", "The shared scene list, with folders: add, remove, settings, filters, new folder, reorder."],
        ["Sources", "Sources of the active tile (the destination you clicked last), with groups: add, remove, "
                    "properties, new folder, reorder; eye and lock on each source."],
        ["Cues", "Optional: cues and their scenes, with live status (chapter 9)."],
        ["Media controls", "Play / pause, restart and stop the selected media source."],
        ["Audio mixer", "Meters, faders, mute and monitoring for the active audio sources."],
        ["Scene transitions", "Choose the OBS transition, its duration and properties, or remove it."],
        ["Controls", "Start or stop streaming and recording, exit the studio."],
    ], [35, 130])

    # 5 -------------------------------------------------------------
    s += [PageBreak(), P("5. Destinations", h1)]
    s += [P("Creating a destination", h2)]
    s += steps([
        "Choose <b>Destinations → New destination…</b>.",
        "Enter a <b>name</b> and a <b>resolution</b>. Choosing an <b>output screen</b> fills in the resolution "
        "of that screen automatically.",
        "Tick <b>Add to existing scenes</b> if the new destination should appear in every scene you already "
        "have. Leave it unticked to add it to scenes one by one.",
        "Click <b>Create</b>. The destination appears in Preview and Program for every scene that covers it.",
    ])
    s += [P("When <b>All destinations, all the time</b> is on (chapter 13), a note under the checkbox reminds you "
            "that the new destination will be visible under every scene; where it was not added, it shows "
            "<b>Not in this scene</b> and nothing is placed on it.")]
    s += shot("The New destination dialog, with the note shown when All destinations, all the time is on.", 60,
              "shot05.png")
    s += [P("The Destinations menu", h2)]
    s += [P("Each destination has its own submenu:")]
    s += table([
        ["Command", "What it does"],
        ["Settings…", "Rename, change the resolution or the output screen."],
        ["Output blending…", "Split this destination across several projectors (chapter 10)."],
        ["Open fullscreen on …", "Shows the Program of the destination full screen on its output screen. For a "
                                 "blended destination: one full-screen window per projector."],
        ["Open windowed", "Shows the Program in a normal window, for checking or for capture."],
        ["Close projection", "Closes the full-screen or windowed output."],
        ["Delete", "Deletes the destination and its layouts in every scene, after confirmation."],
    ], [42, 123])
    s += [P("A full-screen output stays on top of its screen, whatever you do in OBS or in the studio, and does "
            "not take the focus. If its screen is disconnected, the output hides and comes back by itself when "
            "the screen returns. Click an output to give it the focus: <b>Esc</b> then closes it, and a "
            "double-click switches between full screen and window. The mouse pointer is hidden on output "
            "screens.")]
    s += shot("The Destinations menu with a destination submenu open.", 60, "shot06.png")
    s += [P("Managing all destinations", h2)]
    s += [P("<b>Destinations → Manage destinations…</b> shows every destination in one window. Select a "
            "destination on the left to change its name, resolution, output screen and lock, and to tick or untick "
            "the scenes it appears in; the window shows how many scenes cover it. Click <b>Apply</b> to save the "
            "changes; unticking a scene asks for confirmation, because the layout of that scene on this destination "
            "is deleted. The same window gives direct access to Output blending, fullscreen projection, Delete "
            "(Deactivate or Activate for the default destination) and New destination.")]
    s += shot("The Manage destinations window.", 80, "shot07.png")
    s += [P("The default destination", h2)]
    s += [P("The default destination is the OBS main canvas: its resolution is set in OBS Settings → Video, and "
            "it is what OBS streams and records. If you do not need it, choose <b>Deactivate &amp; hide</b> in its "
            "submenu: it disappears from the studio, its scenes are kept for later, and streaming and recording "
            "are disabled until you reactivate it.")]

    # 6 -------------------------------------------------------------
    s += [PageBreak(), P("6. Scenes", h1)]
    s += [P("Scenes are shared by all destinations. A scene decides <b>which destinations it covers</b>; each "
            "covered destination has its own version of the scene with its own sources.")]
    s += [P("Creating a scene", h2)]
    s += [P("Click <b>+</b> in the Scenes dock. Give the scene a name and tick the destinations under <b>Show "
            "this scene on</b> (all are ticked by default). Scenes created directly in OBS, by a Stream Deck or "
            "through obs-websocket open the same dialog.")]
    s += shot("The scene dialog with its list of destinations.", 60, "shot08.png")
    s += [P("Changing which destinations a scene covers", h2)]
    s += [P("Double-click a scene, or use the gear button, to open the same dialog. Unticking a destination "
            "deletes the layout of that scene on that destination, so you are asked to confirm.")]
    s += [P("Selecting and organising", h2)]
    s += bullets([
        "<b>Select</b>: click a scene. Ctrl + click (Cmd + click on macOS) adds or removes a scene from the "
        "selection; Shift + click selects everything between two scenes; dragging from an empty area of the "
        "list selects several scenes.",
        "<b>Move</b>: press on a scene and drag it, use the ↑ and ↓ buttons, or right-click for Move up, Move "
        "down, Move to top and Move to bottom. The order is shared with the OBS scene list.",
        "<b>Rename</b>: F2 (Enter on macOS), or right-click → Rename.",
        "<b>Filters</b>: the filter button opens the OBS filters of the selected scene.",
        "<b>Remove</b>: press <b>Delete</b> or use the bin button; a confirmation is always shown.",
    ])
    s += [P("Folders", h2)]
    s += [P("Folders keep long scene lists tidy. The folder button (or right-click → <b>New folder</b> / "
            "<b>Group into a folder</b>) creates a folder; the selected scenes, and the scenes of selected "
            "folders, go into it. Drag scenes into and out of folders; the arrow at the start of a folder opens "
            "or closes it. <b>Remove folder</b> keeps its scenes. Folders only organise the list: when a folder "
            "is selected, <b>Transition</b> and the quick transitions are disabled and a short note explains why.")]
    s += shot("The Scenes dock with a folder open.", 55, "scenes-dock.png")
    s += [P("When a scene does not cover a destination", h2)]
    s += [P("By default every destination stays visible. When the selected scene does not cover a destination, "
            "its Preview tile is hatched, framed in red and marked <b>Not in this scene</b>; its Program keeps "
            "showing what is live. You can instead show only the destinations of the selected scene "
            "(chapter 13).")]
    s += shot("A destination that the selected scene does not cover.", 55, "shot09.png")

    # 7 -------------------------------------------------------------
    s += [PageBreak(), P("7. Sources and editing", h1)]
    s += [P("Click a Preview tile to make it the <b>active tile</b>: the Sources dock then lists the sources of "
            "that destination for the selected scene, and a discreet green frame marks the tile. Each source shows "
            "the icon of its type, as in OBS.")]
    s += [P("Adding sources", h2)]
    s += bullets([
        "<b>+</b> in the Sources dock lists every OBS source type, plus existing sources and other scenes.",
        "Drag image or video files from your computer onto a destination tile. They are added at their "
        "original size, centred; a new video shows its first frame without playing.",
    ])
    s += [P("Visibility, lock and folders", h2)]
    s += bullets([
        "The <b>eye</b> shows or hides a source; the <b>lock</b> stops it from being moved or resized with the "
        "mouse.",
        "The <b>folder</b> button creates a folder (an OBS group) with the selected sources in it, ready to be "
        "renamed. <b>Edit → Group selected sources</b> (Ctrl+G) and <b>Ungroup</b> (Ctrl+Shift+G) do the same "
        "from the menu.",
        "Drag sources into and out of groups. Selecting a source inside a group edits that source.",
    ])
    s += shot("The Sources dock with a group, the eye and the lock.", 50, "sources-dock.png")
    s += [P("Editing with the mouse", h2)]
    s += table([
        ["Action", "How"],
        ["Move", "Drag the source. It snaps to the centre of the canvas; hold Ctrl to disable snapping."],
        ["Resize", "Drag a corner or edge handle; the aspect ratio is kept. Hold Shift to resize freely."],
        ["Crop", "Hold Alt and drag a handle."],
        ["Nudge", "Arrow keys move the selected source by one pixel."],
        ["Select", "Click a source in the tile or in the Sources dock. In the dock, Ctrl + click and Shift + "
                   "click select several sources."],
        ["Reorder", "Drag sources in the Sources dock, or use ↑ / ↓ and the Edit → Order commands."],
        ["Rename", "F2 (Enter on macOS), or right-click → Rename."],
    ], [30, 135])
    s += [P("Selected sources have a green outline with handles; a source that goes beyond the canvas is "
            "outlined in orange. Clicking elsewhere in the window clears the outlines. A video that is not "
            "playing keeps a frame at its real size and can be edited like any other source.")]
    s += shot("Editing a source in Preview: selection handles and centre guides.", 75, "shot10.png")
    s += [P("Edit menu and shortcuts", h2)]
    s += table([
        ["Command", "Shortcut"],
        ["Undo", "Ctrl+Z"],
        ["Copy / Paste (reference)", "Ctrl+C / Ctrl+V"],
        ["Paste (duplicate): an independent copy, with its filters", "Ctrl+Alt+V"],
        ["Group selected sources / Ungroup", "Ctrl+G / Ctrl+Shift+G"],
        ["Remove (with confirmation)", "Delete or Backspace"],
        ["Copy / Paste transform", "Ctrl+Shift+C / Ctrl+Shift+V"],
        ["Reset transform", "Ctrl+R"],
        ["Fit to screen / Stretch to screen / Center to screen", "Ctrl+F / Ctrl+S / Ctrl+D"],
        ["Move up / down / to top / to bottom", "Ctrl+↑ / Ctrl+↓ / Ctrl+Home / Ctrl+End"],
        ["Properties, Filters", "Edit menu, double-click or right-click"],
    ], [95, 70])
    s += [P("On macOS, use Cmd instead of Ctrl. Editing shortcuts work when the Preview or the Sources dock has "
            "the focus, so they never interfere with typing in other fields.", small)]

    # 8 -------------------------------------------------------------
    s += [PageBreak(), P("8. Going live", h1)]
    s += [P("Transition", h2)]
    s += [P("Select a scene: it appears in Preview on every destination it covers. Click <b>Transition</b> to "
            "send it to Program with the transition and duration chosen in the Scene transitions dock. All "
            "destinations switch at the same moment.")]
    s += bullets([
        "A <b>locked</b> destination keeps its Program.",
        "A destination the scene does not cover keeps its Program.",
        "After the transition, the previous Program returns to Preview, as in OBS Studio Mode. The <b>⋮</b> "
        "menu next to Transition switches this on or off, and switches between all destinations and only "
        "those of the selected scene (the same settings as chapter 13).",
        "Hotkeys for <b>Transition</b>, <b>Cut</b> and opening the studio can be set in OBS Settings → Hotkeys "
        "(they start with \"Multidestination Studio\").",
    ])
    s += [P("Switching scenes in the normal OBS window only changes the default destination; the other "
            "destinations are driven from Multidestination Studio (or from Companion, chapter 12).")]
    s += [P("Quick transitions", h2)]
    s += [P("Quick transitions take the Preview to Program with a given transition and duration, without "
            "changing the transition of the Scene transitions dock. The list starts empty.")]
    s += bullets([
        "<b>Add</b>: click <b>+</b> next to <b>Quick transitions</b>, set the duration at the bottom of the "
        "menu, then choose a transition type (Cut, Fade, Swipe, Slide, Stinger, Fade to Color, Luma Wipe, "
        "Move). Cut and Fade use the OBS built-in transitions; the other types open the OBS name and "
        "properties windows. The new quick transition is added under the others.",
        "<b>Use</b>: click the quick transition.",
        "<b>Edit or remove</b>: click its arrow for <b>Properties…</b>, the duration, or <b>Remove</b>.",
    ])
    s += shot("Transition and its ⋮ menu, quick transitions with the + menu open, and the T-bar.", 60,
              "quick-transitions.png")
    s += [P("T-bar", h2)]
    s += [P("The T-bar under the quick transitions makes a manual transition on every destination. Drag its "
            "handle to the end to finish the transition, or back to the start to cancel it; released in "
            "between, the transition waits there. As in OBS, Cut and Stinger are replaced by a fade for the "
            "T-bar.")]
    s += [P("Editing what is live: Modify PGM", h2)]
    s += [P("What is on air is a copy of the scene, so changes you make in Preview never reach the live output "
            "by accident. When you do need to fix something live, click <b>Modify PGM</b>: the button turns red "
            "and the Program tiles become editable with the same tools as Preview. Your changes are applied live "
            "and also saved into the scene. Click the button again to protect the Program.")]
    s += shot("Modify PGM switched on: the button is red and the Program can be edited.", 70, "modify-pgm.png")

    # 9 -------------------------------------------------------------
    s += [PageBreak(), P("9. Cues", h1)]
    s += [P("A cue plays several scenes one after the other, like a cue list in show control software: for "
            "example a countdown video, then the opening graphic, then the camera. The feature is optional and "
            "off by default: turn it on with <b>Configuration Settings → Cues… → Activate Cues</b>. The Cues "
            "dock then appears between Sources and Media controls.")]
    s += shot("The Cues settings dialog.", 35, "cues-setting.png")
    s += [P("Creating a cue", h2)]
    s += steps([
        "Optionally select, in the Scenes dock, the scenes the cue should start with.",
        "Click <b>+</b> in the Cues dock. Give the cue a name.",
        "For each scene, from top to bottom, set the columns described below. <b>Add scene</b>, <b>Remove "
        "scene</b>, <b>Move up</b> and <b>Move down</b> edit the list.",
        "Click <b>Save</b>.",
    ])
    s += table([
        ["Column", "Meaning"],
        ["Scene", "The scene taken live at this step."],
        ["Pre-wait", "Delay before the scene goes live."],
        ["Transition", "<b>Default</b> (the transition of the Scene transitions dock) or any OBS transition."],
        ["Duration", "Follows the transition, as in the OBS dock: with Default, the default duration (not "
                     "editable); with Cut, greyed; with other transitions, editable (300 ms proposed)."],
        ["Next at media end", "The next scene starts when the video or audio of this scene ends, instead of after "
                              "the post-wait."],
        ["Post-wait", "Delay after the scene starts, before the next scene. It always lasts at least as long as "
                      "the transition."],
    ], [38, 127])
    s += shot("The cue editor.", 70, "cue-editor.png")
    s += [P("Running a cue", h2)]
    s += bullets([
        "Click a cue: its first scene appears in Preview. Press <b>Transition</b> (or Cut) to run it; the scenes "
        "then go live one after the other.",
        "The <b>Run</b> button runs the selected cue directly; <b>Stop</b> stops the running cue.",
        "The running cue opens and its current scene is marked ▶, with its status: the pre-wait or post-wait "
        "countdown, or the media time (0:12 / 0:45).",
        "Choosing another scene in the Scenes dock leaves the cue. Pressing Transition while a cue runs stops it "
        "and takes the Preview as usual.",
    ])
    s += shot("The Cues dock with a cue running.", 50, "cues-dock.png")
    s += [P("Cues are saved with the scene collection. Double-click a cue to edit it; drag cues to reorder "
            "them.")]

    # 10 ------------------------------------------------------------
    s += [PageBreak(), P("10. Output blending", h1)]
    s += [P("Output blending spreads one destination across several outputs, typically two or more projectors "
            "whose images overlap on a wide screen. In the overlap, each projector fades out softly so the "
            "audience sees one seamless picture.")]
    s += [P("Creating a blended destination", h2)]
    s += steps([
        "Choose <b>Destinations → Output blending…</b>.",
        "Click <b>New output</b> for each projector and choose its <b>screen</b>.",
        "Drag each output from the list onto the <b>layout</b> (or double-click it). An output keeps the shape "
        "of its screen and cannot leave the layout.",
        "Set the <b>layout size</b>, for example 3500 × 1080 for two overlapping full-HD projectors. The outputs "
        "follow automatically. The layout size becomes the resolution of the destination.",
        "Place the outputs so that they overlap; overlaps are hatched and blended automatically.",
        "Give the destination a name (\"Destination X\" if left empty) and click <b>Save &amp; Create "
        "destination</b>.",
    ])
    s += shot("The Output blending window with two overlapping outputs.", 85, "shot12.png")
    s += [P("Placing outputs", h2)]
    s += [P("Outputs are moved and resized with the mouse like sources: corners resize while keeping the shape "
            "(Shift resizes freely), the centre snaps (Ctrl disables it), arrow keys nudge, Delete removes an "
            "output from the layout and Ctrl+Z undoes. Position and size can also be typed in; Enter confirms a "
            "value without closing the window.")]
    s += [P("Blend settings", h2)]
    s += table([
        ["Setting", "Use"],
        ["Automatic blend widths", "On: the blend zones follow the overlaps. Off: set Blend left, right, top "
                                   "and bottom by hand, in layout pixels."],
        ["Gamma", "Matches the response of the projector so the blend is even (usually 1.8 to 2.4)."],
        ["Curve", "Shape of the fade: 1 is linear, higher values are softer in the middle."],
        ["Black level", "Raises the black outside the overlaps, so the overlap does not look brighter on dark "
                        "images."],
        ["Corner pin", "Moves each corner of the output, in output pixels, to correct keystone. Reset corners "
                       "restores them."],
        ["Test pattern", "Replaces the picture with a grid and centre cross to align the projectors."],
    ], [42, 123])
    s += [P("Adjusting live and saving", h2)]
    s += [P("For an existing blended destination, open <b>Output blending…</b> from its submenu or from its "
            "Settings. <b>Open outputs</b> shows every output full screen on its projector, and every change "
            "appears there immediately. <b>Save changes</b> stores the settings (the button greys out once "
            "saved). If you <b>Close</b> with unsaved changes, you are asked whether to <b>Save changes</b> or "
            "<b>Discard changes</b>; discarding returns the projectors to the saved settings.")]
    s += shot("Two projectors showing the test pattern, aligned and blended.", 70, "shot13.png")
    s += callout("Start with the test pattern: align the projectors with the grid and corner pin, adjust the "
                 "overlap, then the gamma, and finally the black level on a dark image.")

    # 11 ------------------------------------------------------------
    s += [PageBreak(), P("11. Audio and media", h1)]
    s += [P("The <b>Audio mixer</b> dock shows a strip per audio source with a level meter, a fader, a mute "
            "button and a monitoring menu (Monitor off, Monitor only, Monitor and output). It lists the active "
            "audio sources and those of the selected scene; <b>View all</b> shows every audio source. Audio is "
            "the OBS audio: it is mixed and sent once, with the default destination.")]
    s += [P("The <b>Media controls</b> dock controls the selected media source: play / pause, restart and stop, "
            "with its time.")]
    s += shot("The Media controls and Audio mixer docks.", 50, "shot14.png")

    # 12 ------------------------------------------------------------
    s += [PageBreak(), P("12. Companion and Stream Deck", h1)]
    s += [P("Multidestination Studio comes with a version of Bitfocus Companion's <b>OBS Studio</b> module, "
            "<b>obs-studio-4.0.0-multidestination.1.tgz</b>, attached to every release. It is the full OBS "
            "Studio module plus a <b>Multidestination Studio</b> preset section, so a Stream Deck can drive the "
            "studio.")]
    s += [P("Installing the module", h2)]
    s += steps([
        "In OBS: <b>Tools → WebSocket Server Settings</b>, tick <b>Enable WebSocket server</b>, and note the "
        "port, the IP address and the password (<b>Show Connect Info</b>).",
        "Download <b>obs-studio-4.0.0-multidestination.1.tgz</b> from the release. Do not unzip or rename it.",
        "In Companion: <b>Modules → Import module package</b>, and choose the file. It is added as one more "
        "version of the OBS Studio module, named <b>4.0.0-multidestination.1</b>.",
        "In <b>Connections</b>, add an OBS Studio connection with this version, or, if you already have one, "
        "<b>change its module version</b> from the official version to <b>4.0.0-multidestination.1</b>. "
        "Importing the module does not do this for you. Enter the address, port and password. The connection "
        "turns green.",
        "In <b>Buttons → Presets → OBS</b>, open <b>Multidestination Studio</b> and drag the buttons onto your "
        "pages.",
    ])
    s += shot("Companion: the OBS Studio module with version 4.0.0-multidestination.1.", 55,
              "companion-module.png")
    s += [P("The buttons", h2)]
    s += table([
        ["Preset", "What it does", "Colour"],
        ["Scene to Preview", "Shows the scene in the studio Preview (one button per scene)",
         "Green in Preview, red in Program"],
        ["Scene to Program", "Puts the scene on air on every unlocked destination it covers",
         "Green in Preview, red in Program"],
        ["Transition", "Takes the Preview to Program with the current transition", "—"],
        ["Transition With", "Takes the Preview with that transition (one button per OBS transition)", "—"],
        ["After a Transition", "Toggles “the previous Program returns to Preview”", "Lit when on"],
        ["Destinations Shown", "Toggles all destinations / only those of the selected scene", "Lit when on"],
        ["Cue to Preview", "Shows the cue in Preview; Transition then runs it (when Cues are on)",
         "Green in Preview, red while running"],
        ["Stop Cue", "Stops the running cue", "—"],
    ], [35, 85, 45])
    s += shot("Companion: the Multidestination Studio presets.", 60, "companion-presets.png")
    s += [P("The buttons follow the studio live, and new scenes, transitions and cues appear in the presets by "
            "themselves. The same features exist as actions and feedbacks named <b>Multidestination Studio - "
            "…</b>. The plugin also answers obs-websocket vendor requests (vendor <b>multidestination-studio</b>) "
            "for other control software. The full guide is in the project repository "
            "(docs/companion-module.md).")]
    s += callout("An existing OBS connection keeps the official version after the import: you must switch it "
                 "to 4.0.0-multidestination.1 yourself, on every computer running Companion. In <b>Modules → OBS "
                 "Studio</b>, the plug icon shows the version in use. Keep the connection on this version: an "
                 "official version removes the Multidestination Studio buttons.", "Important")

    # 13 ------------------------------------------------------------
    s += [PageBreak(), P("13. Configuration Settings", h1)]
    s += [P("These settings apply to all your scene collections.")]
    s += table([
        ["Setting", "Choices"],
        ["After a transition…", "<b>The previous Program returns to Preview</b> (recommended, default), or "
                                "<b>The scene sent live stays in Preview</b>, useful when your scenes do not "
                                "cover the same destinations."],
        ["Destinations shown…", "<b>All destinations, all the time</b> (recommended, default), or <b>Only the "
                                "destinations of the selected scene</b>."],
        ["Cues…", "<b>Activate Cues</b>: on or off (off by default). Shows the Cues dock (chapter 9)."],
        ["Startup Settings…", "<b>Launch Multidestination Studio on startup</b> and <b>Open every destination on "
                              "its output screen at startup</b>: on or off (both off by default)."],
        ["Language…", "Same as OBS, English, Français or Norsk (Bokmål). Takes effect when OBS restarts."],
    ], [42, 123])
    s += shot("The Configuration Settings menu and the After a transition dialog.", 60, "settings-menu.png")
    s += shot("The Destinations shown and Language dialogs.", 60, "shot16.png")
    s += shot("The Startup Settings dialog.", 35, "startup-settings.png")

    # 14 ------------------------------------------------------------
    s += [PageBreak(), P("14. Saving, shortcuts and troubleshooting", h1)]
    s += [P("What is saved, and where", h2)]
    s += bullets([
        "Destinations, their scenes and layouts, scene folders, cues, quick transitions, output blending and the "
        "dock arrangement are saved in the OBS <b>scene collection</b>. Switching collection switches all of "
        "them.",
        "Configuration Settings are saved in your OBS user settings and apply to every collection.",
    ])
    s += [P("Shortcuts at a glance", h2)]
    s += table([
        ["Key", "Action"],
        ["F11", "Full screen on / off"],
        ["Esc", "Leave the studio; in a projection window (after clicking it), close the projection"],
        ["Delete", "Remove the selected scenes or sources (with confirmation)"],
        ["F2 (Enter on macOS)", "Rename the selected scene, folder, source or group"],
        ["Ctrl+Z", "Undo"],
        ["Ctrl+G / Ctrl+Shift+G", "Group selected sources / Ungroup"],
        ["Arrow keys", "Nudge the selected source or output"],
        ["Shift (while resizing)", "Free aspect ratio"],
        ["Ctrl (while moving)", "Disable centre snapping"],
        ["Alt (on a handle)", "Crop"],
    ], [45, 120])
    s += [P("Troubleshooting", h2)]
    s += table([
        ["Problem", "What to check"],
        ["The button does not appear in OBS", "Check that OBS is 31.1 or later and reinstall with OBS closed. In "
                                              "OBS, Help → Log Files → View Current Log should contain lines "
                                              "starting with [multidestination-studio]."],
        ["Windows blocks the installer", "The installer is not code-signed. In SmartScreen choose More info → "
                                         "Run anyway; in Defender, allow the file from the protection history."],
        ["The studio was minimized", "Click its entry in the taskbar (the Dock on macOS)."],
        ["A destination stays empty", "Check that the selected scene covers it and that it is not locked."],
        ["Transition is greyed out", "A scene folder is selected: select a scene."],
        ["Open fullscreen is greyed out", "Choose an output screen in the destination Settings, and check that "
                                          "the screen is connected."],
        ["No Multidestination Studio section in Companion", "Most often the OBS connection still uses the "
                                                            "official version: set its module version to "
                                                            "4.0.0-multidestination.1. Then check that it is green "
                                                            "and that OBS runs with the plugin."],
        ["Companion refuses the module", "Import the .tgz file itself (not a .zip); update Companion."],
        ["The interface is in the wrong language", "Configuration Settings → Language, then restart OBS."],
    ], [52, 113])

    doc.multiBuild(s)


if __name__ == "__main__":
    build()
    print("written", OUT, "screenshots:", shot_counter[0])
