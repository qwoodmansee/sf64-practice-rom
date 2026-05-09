#!/usr/bin/env python3
"""
SF64 Practice ROM User Guide - PDF generator.
Produces sf64-practice-guide.pdf at the repo root.
Run: python3 tools/gen_practice_guide.py
"""

from fpdf import FPDF
from fpdf.enums import XPos, YPos, RenderStyle, Corner
import os

OUTPUT = "/Users/qwoodmansee/code/sf64-practice-rom/sf64-practice-guide.pdf"

# ── Colour palette  (SF64 box art inspired) ───────────────────────────────────
BG          = (3,    4,   10)    # space black
PAGE_BG     = (4,    5,   14)    # page background
PANEL_DARK  = (31,  37,   78)    # #1F254E - dark blue trim
PANEL_MID   = (44,  50,  105)    # between dark and light trim
BORDER      = (61,  66,  160)    # #3D42A0 - lighter blue trim
GREEN_TOP   = (15,  94,   31)    # #0F5E1F - heading gradient top
GREEN_BOT   = (127, 194,  36)    # #7FC224 - heading gradient bottom
ACCENT_CYAN = (127, 194,  36)    # keep alias → heading-green (bottom shade)
ACCENT_GOLD = (255, 200,  40)    # warm gold / key badges
ACCENT_RED  = (251,  26,  50)    # #FB1A32 - warnings
ACCENT_GRN  = (60,  200,  60)    # tip green
GREY        = (181, 184, 156)    # #B5B89C - warm olive-grey
TEXT_MAIN   = (210, 228, 255)    # slightly blue-white (space light)
TEXT_DIM    = (181, 184, 156)    # #B5B89C warm grey for secondary text
WHITE       = (255, 255, 255)
STAR_WHITE  = (200, 215, 255)    # star colour (slight blue tint)

# ── Geometry ──────────────────────────────────────────────────────────────────
PW, PH      = 215.9, 279.4   # letter (mm)
M_L, M_R    = 14, 14          # left/right margins
M_T, M_B    = 16, 14          # top/bottom margins
INNER_W     = PW - M_L - M_R  # 187.9 mm
COL2        = INNER_W / 2 + 2  # two-column split (right col x offset from M_L)

# ── Font sizes ────────────────────────────────────────────────────────────────
FS_TITLE    = 22
FS_H1       = 15
FS_H2       = 10
FS_BODY     = 8
FS_SMALL    = 7
FS_BADGE    = 7
FS_KEY      = 7.5


class GuidePDF(FPDF):
    """Custom PDF with persistent dark background and shared helpers."""

    def __init__(self):
        super().__init__(unit="mm", format="letter")
        self.set_margins(M_L, M_T, M_R)
        self.set_auto_page_break(auto=True, margin=M_B)
        self._page_num = 0
        import random
        self._rng = random.Random(42)

    def _draw_stars(self, count=120, min_y=0, max_y=None):
        """Scatter tiny stars across the page background."""
        if max_y is None:
            max_y = PH
        self.set_fill_color(*STAR_WHITE)
        self.set_draw_color(*STAR_WHITE)
        rng = self._rng
        for _ in range(count):
            x = rng.uniform(0, PW)
            y = rng.uniform(min_y, max_y)
            r = rng.choice([0.25, 0.3, 0.35, 0.4, 0.5])
            self.ellipse(x, y, r, r, style="F")

    def _draw_arwing_corner_panels(self):
        """Blue geometric Arwing-panel accents in page corners."""
        self.set_fill_color(*PANEL_DARK)
        self.set_draw_color(*GREY)
        self.set_line_width(0.3)
        # Top-left corner wing panel
        self.polygon([(0, 0), (28, 0), (18, 8), (0, 12)], style="FD")
        # Top-right corner wing panel
        self.polygon([(PW, 0), (PW - 28, 0), (PW - 18, 8), (PW, 12)], style="FD")
        # Bottom-left corner
        self.polygon([(0, PH), (22, PH), (14, PH - 6), (0, PH - 9)], style="FD")
        # Bottom-right corner
        self.polygon([(PW, PH), (PW - 22, PH), (PW - 14, PH - 6), (PW, PH - 9)], style="FD")

    # ── Core overrides ────────────────────────────────────────────────────────

    def header(self):
        # Space-black page fill
        self.set_fill_color(*PAGE_BG)
        self.rect(0, 0, PW, PH, style="F")

        # Star field on every page
        self._draw_stars(count=100)

        if self.page_no() == 1:
            return  # cover draws its own header region

        # Arwing corner panels on inner pages
        self._draw_arwing_corner_panels()

        # Top bar — dark trim with grey rule below
        self.set_fill_color(*PANEL_DARK)
        self.rect(0, 0, PW, 1.4, style="F")
        self.set_fill_color(*GREY)
        self.rect(0, 1.4, PW, 0.35, style="F")

        self.set_y(3)
        self.set_font("Courier", "B", 7)
        self.set_text_color(*GREY)
        self.cell(0, 5, "SF64 PRACTICE ROM  -  PLAYER GUIDE  v0.5", align="C")
        self.set_y(M_T)

    def footer(self):
        if self.page_no() == 1:
            return
        self.set_y(-10)
        self.set_fill_color(*PANEL_DARK)
        self.rect(0, PH - 10, PW, 10, style="F")
        self.set_fill_color(*GREY)
        self.rect(0, PH - 10, PW, 0.35, style="F")
        self.set_font("Courier", "", 6.5)
        self.set_text_color(*GREY)
        self.cell(0, 6, f"Page {self.page_no()}", align="C")

    # ── Layout helpers ────────────────────────────────────────────────────────

    def _rr(self, x, y, w, h, r=1.5, style="FD"):
        """Draw a rounded rectangle using fpdf2's internal method."""
        rs = RenderStyle.DF if style == "FD" else RenderStyle.F if style == "F" else RenderStyle.D
        corners = (Corner.TOP_LEFT, Corner.TOP_RIGHT, Corner.BOTTOM_LEFT, Corner.BOTTOM_RIGHT)
        self._draw_rounded_rect(x, y, w, h, rs, round_corners=corners, r=r)

    def panel(self, x, y, w, h, fill=PANEL_DARK, border=BORDER, radius=1.5):
        """Rounded filled panel with border."""
        self.set_fill_color(*fill)
        self.set_draw_color(*border)
        self.set_line_width(0.25)
        self._rr(x, y, w, h, r=radius, style="FD")

    def h_rule(self, y=None, color=BORDER, width=0.3):
        if y is None:
            y = self.get_y()
        self.set_draw_color(*color)
        self.set_line_width(width)
        self.line(M_L, y, PW - M_R, y)
        self.set_y(y + 1.5)

    # ── Gradient text helpers ─────────────────────────────────────────────────

    def _push_clip_rect(self, x, y, w, h):
        """Save graphics state and clip output to a rectangle (PDF operators)."""
        k = self.k
        ph = self.h        # page height in mm (fpdf2 internal unit == mm here)
        xp = x * k
        yp = (ph - y - h) * k
        wp = w * k
        hp = h * k
        self._out("q")
        self._out(f"{xp:.3f} {yp:.3f} {wp:.3f} {hp:.3f} re W n")

    def _pop_clip(self):
        self._out("Q")

    def section_header(self, title, color=None, underline=True):
        """Section heading with top→bottom green gradient text."""
        fs   = FS_H1          # 15 pt
        th   = 8.5            # cell height in mm
        y0   = self.get_y()
        cw   = PW - M_L - M_R
        n    = 16             # gradient bands
        bh   = th / n

        r0, g0, b0 = GREEN_TOP
        r1, g1, b1 = GREEN_BOT

        for i in range(n):
            t  = i / (n - 1)
            rc = int(r0 + (r1 - r0) * t)
            gc = int(g0 + (g1 - g0) * t)
            bc = int(b0 + (b1 - b0) * t)
            cy = y0 + i * bh

            self._push_clip_rect(M_L, cy, cw, bh + 0.3)
            self.set_xy(M_L, y0)
            self.set_font("Courier", "B", fs)
            self.set_text_color(rc, gc, bc)
            self.cell(cw, th, title, align="L")
            self._pop_clip()

        self.set_y(y0 + th + 0.5)
        if underline:
            self.h_rule(y0 + th + 0.5, color=GREEN_BOT, width=0.4)
        self.set_text_color(*TEXT_MAIN)

    def sub_header(self, title, color=ACCENT_GOLD):
        self.set_font("Courier", "B", FS_H2)
        self.set_text_color(*color)
        self.cell(0, 5.5, title, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
        self.set_text_color(*TEXT_MAIN)

    def body(self, text, size=FS_BODY, color=TEXT_MAIN, indent=0):
        self.set_font("Helvetica", "", size)
        self.set_text_color(*color)
        if indent:
            self.set_x(M_L + indent)
        self.multi_cell(INNER_W - indent, 4.5, text, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
        self.set_text_color(*TEXT_MAIN)

    def bullet(self, key, desc, key_color=ACCENT_GOLD, key_w=42, indent=3):
        """One row: coloured key label + description text."""
        x0 = M_L + indent
        y0 = self.get_y()
        # Key pill background (Arwing dark-cobalt panel)
        self.set_fill_color(*PANEL_DARK)
        self.set_draw_color(*BORDER)
        self.set_line_width(0.15)
        self._rr(x0, y0, key_w, 5.0, r=1.0, style="FD")
        # Key text
        self.set_xy(x0 + 1, y0 + 0.5)
        self.set_font("Courier", "B", FS_KEY)
        self.set_text_color(*key_color)
        self.cell(key_w - 2, 4.2, key, align="L")
        # Description
        self.set_xy(x0 + key_w + 2, y0 + 0.4)
        self.set_font("Helvetica", "", FS_BODY)
        self.set_text_color(*TEXT_MAIN)
        desc_w = INNER_W - key_w - indent - 4
        self.multi_cell(desc_w, 4.5, desc, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
        bottom = self.get_y()
        self.set_y(max(y0 + 5.5, bottom))

    def _text_height(self, text_w, line_h, text):
        """Estimate multi_cell rendered height using word-wrap simulation."""
        self.set_font("Helvetica", "", FS_SMALL)
        total = 0.0
        for para in text.split("\n"):
            if not para.strip():
                total += line_h
                continue
            words = para.split()
            line_w = 0.0
            lines = 1
            for word in words:
                ww = self.get_string_width(word + " ")
                if line_w > 0 and line_w + ww > text_w:
                    lines += 1
                    line_w = ww
                else:
                    line_w += ww
            total += lines * line_h
        return total

    def callout(self, label, text, label_color=ACCENT_CYAN, fill=PANEL_DARK, indent=0):
        """Small labelled callout box — height sized to actual wrapped text."""
        x0  = M_L + indent
        w   = INNER_W - indent
        tw  = w - 6          # body text cell width (same as multi_cell arg)
        lh  = 4.2            # line height
        self.set_y(self.get_y() + 1)
        y0  = self.get_y()
        h   = 5.5 + self._text_height(tw, lh, text) + 3   # label row + body + bottom pad
        self.panel(x0, y0, w, h, fill=fill, border=label_color)
        # Coloured left strip
        self.set_fill_color(*label_color)
        self.rect(x0, y0, 2.5, h, style="F")
        # Label text
        self.set_xy(x0 + 4, y0 + 1.5)
        self.set_font("Courier", "B", FS_SMALL)
        self.set_text_color(*label_color)
        self.cell(20, 4, label)
        # Body text — starts below label row with left padding past the strip
        self.set_xy(x0 + 4, y0 + 5.5)
        self.set_font("Helvetica", "", FS_SMALL)
        self.set_text_color(*TEXT_MAIN)
        self.multi_cell(tw, lh, text, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
        self.set_y(y0 + h + 2)

    def draw_radial_diagram(self, cx, cy, ring_r=35):
        """Render the 8-option root radial menu as a circular diagram."""
        import math
        # (angle_deg, label, game_color)  — colors from sRootSlices[] in practice_menu.c
        options = [
            (270, "RESTART",  (180,  60,  60)),
            (315, "DISPLAY",  ( 60, 160, 160)),
            (  0, "SAVE",     ( 60, 140, 180)),
            ( 45, "LOAD",     ( 60, 180, 100)),
            ( 90, "LEVELS",   (180, 140,  60)),
            (135, "LOADOUT",  (140,  60, 180)),
            (180, "CHEATS",   (200,  80,  80)),
            (225, "SD CARD",  ( 80, 200, 120)),
        ]
        outer_r = ring_r + 7   # outer glow circle radius

        # Outer space-dark ring
        self.set_fill_color(*PAGE_BG)
        self.set_draw_color(*BORDER)
        self.set_line_width(0.5)
        self.ellipse(cx - outer_r, cy - outer_r, outer_r * 2, outer_r * 2, style="FD")

        # Inner ring
        self.set_fill_color(*PANEL_DARK)
        self.set_draw_color(*BORDER)
        self.set_line_width(0.2)
        self.ellipse(cx - ring_r + 1, cy - ring_r + 1,
                     (ring_r - 1) * 2, (ring_r - 1) * 2, style="F")

        # Spokes (from edge of center circle to inner edge of pill zone)
        cr = 10   # center circle radius
        for angle_deg, label, color in options:
            rad = math.radians(angle_deg)
            sx = cx + (cr + 1) * math.cos(rad)
            sy = cy + (cr + 1) * math.sin(rad)
            ex = cx + (ring_r - 5) * math.cos(rad)
            ey = cy + (ring_r - 5) * math.sin(rad)
            self.set_draw_color(*[min(c + 30, 255) for c in color])
            self.set_line_width(0.35)
            self.line(sx, sy, ex, ey)

        # Node pills
        pill_w, pill_h = 26, 8
        for angle_deg, label, color in options:
            rad = math.radians(angle_deg)
            nx = cx + ring_r * math.cos(rad)
            ny = cy + ring_r * math.sin(rad)
            px = nx - pill_w / 2
            py = ny - pill_h / 2

            # Glow border (slightly darker shade of option color)
            glow = tuple(max(c - 50, 0) for c in color)
            self.set_fill_color(*glow)
            self.set_draw_color(*color)
            self.set_line_width(0.5)
            self._rr(px - 1, py - 1, pill_w + 2, pill_h + 2, r=2.5, style="FD")

            # Main fill (slightly brighter)
            bright = tuple(min(c + 25, 220) for c in color)
            self.set_fill_color(*bright)
            self.set_draw_color(*[min(c + 80, 255) for c in color])
            self.set_line_width(0.25)
            self._rr(px, py, pill_w, pill_h, r=1.8, style="FD")

            # Label
            self.set_xy(px, py + 1.3)
            self.set_font("Courier", "B", 6.5)
            self.set_text_color(255, 255, 255)
            self.cell(pill_w, pill_h - 2, label, align="C")

        # Center circle with direction hint
        self.set_fill_color(*PAGE_BG)
        self.set_draw_color(*BORDER)
        self.set_line_width(0.5)
        self.ellipse(cx - cr, cy - cr, cr * 2, cr * 2, style="FD")
        self.set_xy(cx - cr, cy - 5)
        self.set_font("Courier", "B", 5.5)
        self.set_text_color(60, 215, 65)
        self.cell(cr * 2, 5, "STICK", align="C")
        self.set_xy(cx - cr, cy)
        self.set_font("Courier", "", 5)
        self.set_text_color(*TEXT_DIM)
        self.cell(cr * 2, 5, "THEN  A", align="C")

    def vspace(self, mm=2):
        self.set_y(self.get_y() + mm)

    # ── Two-column helpers ────────────────────────────────────────────────────

    def col_left(self):
        self.set_xy(M_L, self.get_y())
        return INNER_W / 2 - 3

    def col_right(self, y=None):
        if y is None:
            y = self.get_y()
        self.set_xy(M_L + INNER_W / 2 + 3, y)
        return INNER_W / 2 - 3

    # ── Quick-ref table row ───────────────────────────────────────────────────

    def _qr_row(self, combo, action, alt=False):
        x0 = M_L + 2
        y0 = self.get_y()
        h  = 5.5
        fill = PANEL_MID if alt else PANEL_DARK
        self.set_fill_color(*fill)
        self.rect(x0, y0, INNER_W - 4, h, style="F")
        # combo col
        self.set_xy(x0 + 1, y0 + 0.6)
        self.set_font("Courier", "B", FS_KEY)
        self.set_text_color(*ACCENT_GOLD)
        self.cell(80, 4, combo)
        # action col
        self.set_xy(x0 + 82, y0 + 0.6)
        self.set_font("Helvetica", "", FS_BODY)
        self.set_text_color(*TEXT_MAIN)
        self.cell(INNER_W - 86, 4, action)
        self.set_y(y0 + h)


# ── Tiny helper: rounded rectangle via line_to ────────────────────────────────
# fpdf2 rounded rects use _draw_rounded_rect; we use _rr() helper above.


def build_pdf():
    pdf = GuidePDF()
    pdf.set_title("SF64 Practice ROM Player Guide")
    pdf.set_author("SF64 Practice ROM")

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 1 - COVER + QUICK REFERENCE
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    # ── Cover: full-page star field already drawn by header() ────────────────

    # Nebula glow strip behind the title (orange-red, like the planet in the art)
    import math
    glow_y = M_T - 6
    glow_h = 52
    # Layered rectangles fading outward to simulate a glow
    for i, (alpha_r, alpha_g, alpha_b) in enumerate([
        (80, 30, 8), (55, 20, 5), (35, 12, 3), (18, 6, 1)
    ]):
        margin = i * 2.5
        pdf.set_fill_color(alpha_r, alpha_g, alpha_b)
        pdf.rect(M_L - margin, glow_y - margin,
                 INNER_W + margin * 2, glow_h + margin * 2, style="F")

    # Arwing wing panel — left side of banner
    pdf.set_fill_color(*PANEL_DARK)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.5)
    pdf.polygon([
        (M_L - 14, glow_y + 2),
        (M_L + 34, glow_y + 2),
        (M_L + 22, glow_y + glow_h - 2),
        (M_L - 14, glow_y + glow_h - 2),
    ], style="FD")
    # Inner panel highlight
    pdf.set_fill_color(*PANEL_MID)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.25)
    pdf.polygon([
        (M_L - 8, glow_y + 6),
        (M_L + 26, glow_y + 6),
        (M_L + 17, glow_y + glow_h - 6),
        (M_L - 8, glow_y + glow_h - 6),
    ], style="FD")

    # Arwing wing panel — right side of banner
    pdf.set_fill_color(*PANEL_DARK)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.5)
    right_x = M_L + INNER_W
    pdf.polygon([
        (right_x + 14, glow_y + 2),
        (right_x - 34, glow_y + 2),
        (right_x - 22, glow_y + glow_h - 2),
        (right_x + 14, glow_y + glow_h - 2),
    ], style="FD")
    pdf.set_fill_color(*PANEL_MID)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.25)
    pdf.polygon([
        (right_x + 8, glow_y + 6),
        (right_x - 26, glow_y + 6),
        (right_x - 17, glow_y + glow_h - 6),
        (right_x + 8, glow_y + glow_h - 6),
    ], style="FD")

    # Title text: STAR FOX 64 in gradient green
    pdf.set_xy(M_L + 36, glow_y + 6)
    pdf.set_font("Courier", "B", 26)
    pdf.set_text_color(*GREEN_BOT)
    pdf.cell(0, 12, "STAR FOX 64")

    # Dark outline effect (simulate the logo's black outline): draw behind in near-black
    pdf.set_xy(M_L + 37, glow_y + 7)
    pdf.set_text_color(10, 10, 10)
    pdf.set_font("Courier", "B", 26)

    # Re-draw green on top
    pdf.set_xy(M_L + 36, glow_y + 6)
    pdf.set_text_color(*GREEN_BOT)
    pdf.cell(0, 12, "STAR FOX 64")

    # Subtitle line
    pdf.set_xy(M_L + 36, glow_y + 20)
    pdf.set_font("Courier", "B", 13)
    pdf.set_text_color(*ACCENT_GOLD)
    pdf.cell(0, 7, "PRACTICE ROM")

    # Version + description
    pdf.set_xy(M_L + 36, glow_y + 29)
    pdf.set_font("Courier", "B", 9)
    pdf.set_text_color(*TEXT_DIM)
    pdf.cell(0, 5, "PLAYER GUIDE  v0.5")

    pdf.set_xy(M_L + 36, glow_y + 36)
    pdf.set_font("Helvetica", "", 7.5)
    pdf.set_text_color(160, 180, 220)
    pdf.multi_cell(INNER_W - 55, 4,
        "Save states  *  Frame advance  *  Hitbox visualizers  *  Free camera  *  Input macros",
        new_x=XPos.LMARGIN, new_y=YPos.NEXT)

    # Cobalt separator line below banner
    pdf.set_y(glow_y + glow_h + 3)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.5)
    pdf.line(M_L, pdf.get_y(), M_L + INNER_W, pdf.get_y())
    pdf.set_y(pdf.get_y() + 3)

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 2 - WELCOME / INTRO
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    # Title panel
    banner_h = 28
    glow_y2  = M_T - 2
    # Subtle nebula glow behind title
    for r, g, b in [(60, 22, 5), (40, 14, 3), (20, 7, 1)]:
        pdf.set_fill_color(r, g, b)
        pdf.rect(M_L - 3, glow_y2 - 3, INNER_W + 6, banner_h + 6, style="F")

    pdf.set_fill_color(*PANEL_DARK)
    pdf.set_draw_color(*BORDER)
    pdf.set_line_width(0.4)
    pdf._rr(M_L, glow_y2, INNER_W, banner_h, r=2, style="FD")
    # Left cobalt accent bar
    pdf.set_fill_color(*BORDER)
    pdf.rect(M_L, glow_y2, 4, banner_h, style="F")

    pdf.set_xy(M_L + 8, glow_y2 + 5)
    pdf.set_font("Courier", "B", 16)
    pdf.set_text_color(60, 215, 65)
    pdf.cell(0, 8, "WELCOME, BETA TESTER")

    pdf.set_xy(M_L + 8, glow_y2 + 15)
    pdf.set_font("Courier", "B", 8)
    pdf.set_text_color(*ACCENT_GOLD)
    pdf.cell(0, 5, "Star Fox 64 Practice ROM  *  Beta Release")

    pdf.set_y(glow_y2 + banner_h + 6)

    # Body — sincere thank-you note
    para1 = (
        "Thank you sincerely for testing out the beta version of the Star Fox 64 "
        "Practice ROM. This ROM attempts to maintain as much vanilla code as possible "
        "while unlocking helpful practice tools similar to those found in other N64 "
        "practice ROMs. I want it to feel like Star Fox 64 - just with extra tools "
        "layered on top, not replacing anything you already know."
    )
    para2 = (
        "This early on, there are bound to be bugs and crashes. If you run into "
        "anything - a freeze, a wrong value, a menu that behaves unexpectedly - please "
        "send it over and I will get a fix released as soon as possible. Every report "
        "helps make the ROM better for everyone."
    )
    para3 = (
        "New versions are released at:  sageraces.com"
    )
    para4 = (
        "Beta / staging builds can be found at:  staging.sageraces.com"
    )

    pdf.set_font("Helvetica", "", 9)
    pdf.set_text_color(*TEXT_MAIN)
    pdf.multi_cell(INNER_W, 5.2, para1, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
    pdf.vspace(4)

    pdf.set_font("Helvetica", "", 9)
    pdf.multi_cell(INNER_W, 5.2, para2, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
    pdf.vspace(5)

    # URL callout boxes
    for label, url in [
        ("RELEASES",  "sageraces.com"),
        ("BETA",      "staging.sageraces.com"),
    ]:
        x0 = M_L
        y0 = pdf.get_y()
        h  = 12
        pdf.panel(x0, y0, INNER_W, h, fill=PANEL_DARK, border=BORDER)
        pdf.set_fill_color(*BORDER)
        pdf.rect(x0, y0, 4, h, style="F")
        pdf.set_xy(x0 + 7, y0 + 2)
        pdf.set_font("Courier", "B", FS_SMALL)
        pdf.set_text_color(*TEXT_DIM)
        pdf.cell(28, 4, label)
        pdf.set_xy(x0 + 36, y0 + 2)
        pdf.set_font("Courier", "B", 9)
        pdf.set_text_color(60, 215, 65)
        pdf.cell(0, 4, url)
        pdf.set_y(y0 + h + 3)

    pdf.vspace(6)

    # Bug-reporting callout
    pdf.callout(
        "REPORTING BUGS",
        "Please include: what level you were on, what you were doing when it crashed "
        "or broke, and whether you can reproduce it. Screenshots or video are a big "
        "help. Anything you share makes the next version better.",
        label_color=ACCENT_GOLD,
        fill=PANEL_DARK,
    )

    pdf.vspace(4)

    # SD card prominent warning
    pdf.callout(
        "SD CARD  -  NOT WORKING YET",
        "The SD card save/load feature is under active development and does NOT "
        "currently work on EverDrive cartridges. Do not rely on it to save your work. "
        "RAM save slots (D-Left / D-Right) work fine. SD card support will be "
        "enabled in a future release once it is stable.",
        label_color=ACCENT_RED,
        fill=(30, 5, 8),
    )

    pdf.vspace(4)

    # Closing note - centered, dimmed
    pdf.set_font("Helvetica", "I", 8)
    pdf.set_text_color(*TEXT_DIM)
    pdf.multi_cell(INNER_W, 5,
        "This guide covers the controls and features included in this beta build. "
        "If something in the ROM does not match what is described here, that is a bug - "
        "please report it.",
        new_x=XPos.LMARGIN, new_y=YPos.NEXT, align="C")

    # ── Section: Controls Quick Reference ─────────────────────────────────────
    pdf.add_page()
    pdf.section_header("QUICK REFERENCE  -  CONTROLS")

    def _table_header(pdf, label):
        x0 = M_L + 2
        y0 = pdf.get_y()
        pdf.set_fill_color(*GREEN_TOP)
        pdf.rect(x0, y0, INNER_W - 4, 6, style="F")
        pdf.set_xy(x0 + 2, y0 + 1)
        pdf.set_font("Courier", "B", FS_SMALL)
        pdf.set_text_color(*WHITE)
        pdf.cell(80, 4, label)
        pdf.cell(INNER_W - 86, 4, "ACTION")
        pdf.set_y(y0 + 6)

    # ── During gameplay (menu CLOSED) ────────────────────────────────────────
    pdf.sub_header("DURING GAMEPLAY  (radial menu closed)", color=ACCENT_GOLD)
    _table_header(pdf, "INPUT")

    gameplay_rows = [
        ("Z + D-Right",  "Open the practice radial menu"),
        ("D-Down",        "Pause / unpause the game (frame advance)"),
        ("D-Up",          "Step one frame forward (while paused)"),
        ("D-Left",        "Quick-save to the active slot"),
        ("D-Right",       "Quick-load from the active slot"),
    ]
    for i, (combo, action) in enumerate(gameplay_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(4)

    # ── While radial menu is open ─────────────────────────────────────────────
    pdf.sub_header("WHILE RADIAL MENU IS OPEN", color=ACCENT_GOLD)
    _table_header(pdf, "INPUT")

    menu_rows = [
        ("Analog Stick + A",       "Navigate and select menu options"),
        ("B",                       "Go back / close the menu"),
        ("L  (alone)",              "Cycle active save slot backward"),
        ("R  (alone)",              "Cycle active save slot forward"),
        ("Z",                       "Save to SD card  (NOT WORKING YET on EverDrive)"),
        ("Z + B",                   "Load from SD card  (NOT WORKING YET on EverDrive)"),
        ("Hold START  (~1.5 sec)",  "Return to the title screen"),
    ]
    for i, (combo, action) in enumerate(menu_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(3)
    pdf.callout("NOTE", (
        "D-pad shortcuts (save, load, pause, frame step) are ONLY active during "
        "normal gameplay. They do not fire while the radial menu is open.\n"
        "L and R slot-cycle work only when no D-pad button is held simultaneously."
    ), label_color=ACCENT_CYAN)

    # ── Section: Radial Menu Navigation (own page) ────────────────────────────
    pdf.add_page()
    pdf.section_header("RADIAL MENU NAVIGATION")

    pdf.body(
        "Press Z + D-Right during gameplay to open the practice radial menu. "
        "Tilt the analog stick toward an option to highlight it, then press A to select. "
        "Press B to close or go back. Some options open a second radial layer."
    )
    pdf.vspace(3)

    # ── Circular diagram ──────────────────────────────────────────────────────
    diagram_h = 88   # mm of vertical space for the circle (ring_r=35 + pill + margins)
    cx = PW / 2
    cy = pdf.get_y() + diagram_h / 2
    pdf.draw_radial_diagram(cx, cy, ring_r=35)
    pdf.set_y(cy + 35 + 8 + 4)   # ring_r + half-pill + gap

    # ── Compact option descriptions ───────────────────────────────────────────
    pdf.vspace(2)
    radial_options = [
        ("RESTART",  (180,  60,  60), "Instantly restart the current level."),
        ("DISPLAY",  ( 60, 160, 160), "Open the display submenu (overlays, camera, macro)."),
        ("SAVE",     ( 60, 140, 180), "Save your state to the active slot."),
        ("LOAD",     ( 60, 180, 100), "Load state from the active slot."),
        ("LEVELS",   (180, 140,  60), "Open the level select screen."),
        ("LOADOUT",  (140,  60, 180), "Configure gear before launching a level."),
        ("CHEATS",   (200,  80,  80), "Toggle gameplay cheats."),
        ("SD CARD",  ( 80, 200, 120), "SD card saves  (NOT WORKING YET on EverDrive)."),
    ]
    pill_lw = 24   # label column width
    desc_x  = M_L + pill_lw + 4
    desc_w  = INNER_W - pill_lw - 4
    half    = 4
    col2_x  = M_L + INNER_W / 2 + 2
    col2_desc_x = col2_x + pill_lw + 4
    col2_desc_w = INNER_W / 2 - pill_lw - 4 - 2

    left_opts  = radial_options[:half]
    right_opts = radial_options[half:]
    row_h = 8
    y_start = pdf.get_y()

    for i, (label, color, desc) in enumerate(left_opts):
        y0 = y_start + i * row_h
        bright = tuple(min(c + 20, 220) for c in color)
        pdf.set_fill_color(*bright)
        pdf.set_draw_color(*[min(c + 80, 255) for c in color])
        pdf.set_line_width(0.3)
        pdf._rr(M_L, y0, pill_lw, 6.5, r=1.5, style="FD")
        pdf.set_xy(M_L, y0 + 1.0)
        pdf.set_font("Courier", "B", 6.5)
        pdf.set_text_color(255, 255, 255)
        pdf.cell(pill_lw, 5, label, align="C")
        pdf.set_xy(desc_x, y0 + 0.5)
        pdf.set_font("Helvetica", "", FS_SMALL)
        pdf.set_text_color(*TEXT_MAIN)
        pdf.cell(desc_w - 6, 6, desc)

    for i, (label, color, desc) in enumerate(right_opts):
        y0 = y_start + i * row_h
        bright = tuple(min(c + 20, 220) for c in color)
        pdf.set_fill_color(*bright)
        pdf.set_draw_color(*[min(c + 80, 255) for c in color])
        pdf.set_line_width(0.3)
        pdf._rr(col2_x, y0, pill_lw, 6.5, r=1.5, style="FD")
        pdf.set_xy(col2_x, y0 + 1.0)
        pdf.set_font("Courier", "B", 6.5)
        pdf.set_text_color(255, 255, 255)
        pdf.cell(pill_lw, 5, label, align="C")
        pdf.set_xy(col2_desc_x, y0 + 0.5)
        pdf.set_font("Helvetica", "", FS_SMALL)
        pdf.set_text_color(*TEXT_MAIN)
        pdf.cell(col2_desc_w, 6, desc)

    pdf.set_y(y_start + len(left_opts) * row_h + 2)

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 2 - CORE FEATURES
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    # ── Save States ───────────────────────────────────────────────────────────
    pdf.section_header("SAVE STATES")
    pdf.body(
        "The practice ROM supports up to 4 RAM save slots (requires Expansion Pak). "
        "Snapshots capture your exact position, velocity, enemy state, charge level, "
        "phase progress, and all game flags - everything needed to resume precisely."
    )
    pdf.vspace(1)

    # During gameplay shortcuts
    pdf.sub_header("DURING GAMEPLAY  (menu closed)", color=ACCENT_GOLD)
    gameplay_save_rows = [
        ("D-Left",   "Quick-save to the active slot (also works while paused)"),
        ("D-Right",  "Quick-load from the active slot"),
    ]
    for i, (combo, action) in enumerate(gameplay_save_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(3)

    # While menu is open shortcuts
    pdf.sub_header("WHILE RADIAL MENU IS OPEN", color=ACCENT_GOLD)
    menu_save_rows = [
        ("Menu > SAVE",   "Save via radial menu (same as D-Left during gameplay)"),
        ("Menu > LOAD",   "Load via radial menu (same as D-Right during gameplay)"),
        ("L  (alone)",    "Cycle slot backward  (slot 1 -> 4 -> 3 -> ...)"),
        ("R  (alone)",    "Cycle slot forward   (slot 1 -> 2 -> 3 -> ...)"),
        ("Z",             "Save to SD card (NOT WORKING YET on EverDrive)"),
        ("Z + B",         "Load from SD card (NOT WORKING YET on EverDrive)"),
    ]
    for i, (combo, action) in enumerate(menu_save_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(2)
    pdf.callout("TIP",
        "If you save while the game is paused (frame-advance mode), loading "
        "that slot restores the paused state automatically - great for "
        "practising a single frame window repeatedly.",
        label_color=ACCENT_GRN)

    # ── Level Select ──────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("LEVEL SELECT")
    pdf.body(
        "Open with Menu > LEVELS. Use the analog stick or D-pad to browse all "
        "levels and pick a checkpoint phase. Press A to launch. "
        "The Boss Test option (Corneria Carrier) lets you practice the end-game "
        "boss fight from any position."
    )
    pdf.vspace(1)
    pdf.callout("NOTE",
        "The Loadout screen (Menu > LOADOUT) is separate from level select. "
        "Configure your gear first, then pick a level to launch with those settings.",
        label_color=ACCENT_CYAN)

    # ── Loadout ───────────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("LOADOUT")
    pdf.body(
        "Configure everything about your starting state before launching a level. "
        "Settings persist between launches until you change them."
    )
    pdf.vspace(1)

    loadout_items = [
        ("LASER",           "SINGLE / TWIN / HYPER  (default: HYPER)"),
        ("BOMBS",           "Starting bomb count"),
        ("LIVES",           "Starting life count"),
        ("GOLD RINGS",      "Starting gold ring count"),
        ("HEALTH",          "SHORT (normal) or LONG (silver Arwing)"),
        ("RIGHT / LEFT WING","NONE / BROKEN / INTACT"),
        ("FALCO / SLIPPY / PEPPY", "ALIVE or DOWN"),
        ("EXPERT",          "Toggle expert mode on/off"),
        ("HIT COUNT",       "Starting hit count (useful for route planning)"),
        ("PREV PLANETS",    "Mark which planets are already cleared (affects "
                            "route-dependent stage unlocks such as Sector Z)"),
    ]
    for key, desc in loadout_items:
        pdf.bullet(key, desc, key_w=52, indent=2)

    # ── Cheats ────────────────────────────────────────────────────────────────
    pdf.add_page()
    pdf.section_header("CHEATS")
    pdf.body(
        "Toggle cheats from Menu > CHEATS. Active cheats show a coloured "
        "indicator label in the HUD. All cheats persist until manually turned off."
    )
    pdf.vspace(1)

    cheat_items = [
        ("AUTO CS",   "Fires a charge shot at the earliest possible frame (timer > 10). "
                      "Useful for verifying charge-shot windows without manual timing."),
        ("INF HP",    "Shields never deplete - you cannot die."),
        ("INF BOMB",  "Bomb count stays at 9."),
        ("INF LIFE",  "Lives stay at 99."),
        ("INF BST",   "Boost meter never depletes - boost freely without penalty."),
    ]
    for key, desc in cheat_items:
        pdf.bullet(key, desc, key_w=22, indent=2)

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 3 - DISPLAY SUBMENU
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    pdf.section_header("DISPLAY SUBMENU")
    pdf.body(
        "Open with Menu > DISPLAY (point stick NE, press A). A second radial "
        "layer appears with 7 options arranged around the screen."
    )
    pdf.vspace(2)

    # ── Skip Cutscenes ────────────────────────────────────────────────────────
    pdf.sub_header("SKIP CUTS  -  Automatic cutscene skip")
    pdf.body(
        "When enabled, all in-level cutscenes are skipped automatically. "
        "Toggle on/off from the Display radial. Active by default."
    )
    pdf.vspace(2)

    # ── Input Display ─────────────────────────────────────────────────────────
    pdf.sub_header("INPUTS  -  Live input overlay")
    pdf.body(
        "Draws a live overlay of your current controller inputs on screen - "
        "buttons, stick position, triggers. Useful for tutorial recording or "
        "reviewing your own inputs frame by frame alongside frame-advance."
    )
    pdf.vspace(2)

    # ── Free Camera ───────────────────────────────────────────────────────────
    pdf.sub_header("CAMERA  -  Free camera mode")
    pdf.callout("NOTE",
        "You must pause the game (D-Down) before enabling Free Camera. "
        "The CAMERA option is greyed out in the menu when the game is running.",
        label_color=ACCENT_RED, fill=(30, 5, 8))
    pdf.vspace(1)
    pdf.body(
        "Detaches the camera from Fox so you can fly freely through the level. "
        "Useful for studying geometry, hitboxes, and spawn zones."
    )
    pdf.vspace(1)

    cam_rows = [
        ("Analog Stick",  "Look (yaw/pitch)"),
        ("C-Up / C-Down", "Move camera forward / backward"),
        ("C-Left / C-Right", "Strafe camera left / right"),
        ("R  (hold)",     "Move faster (5x boost)"),
        ("D-Left / D-Right", "Cycle placement object (None > Bomb > Explosion)"),
        ("A  (no object)", "Toggle the camera HUD overlay"),
        ("A  (with object)", "Place a range-visualizer marker at the aim point"),
        ("Z",             "Toggle hitbox rendering on/off"),
        ("B",             "Exit free camera and return to normal view"),
    ]
    for i, (combo, action) in enumerate(cam_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(2)
    pdf.callout("TIP",
        "Use D-Left/D-Right to select a Bomb or Explosion object, then press A "
        "to drop a range-visualizer marker at the crosshair. The ring shows the "
        "blast radius - helpful for learning charge-shot and bomb positioning.",
        label_color=ACCENT_GRN)

    # ── Stats Overlay ─────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.sub_header("STATS  -  HUD stats overlay")
    pdf.body(
        "Opens a sub-list where individual stat counters can be toggled on/off:"
    )
    pdf.vspace(1)

    stat_items = [
        ("HUD OVERLAY",    "Master toggle for the entire stats bar"),
        ("LAG FRAMES",     "Count of lag frames accumulated this level"),
        ("SPEED",          "Current arwing speed (base + boost combined)"),
        ("CHARGE TIMING",  "After firing a charge shot, shows how many frames "
                           "early or late you released relative to the ideal window"),
        ("MISSED INPUTS",  "Tracks how many input windows were missed"),
        ("HIT TRACKING",   "Tracks hit-count events (direct hits, bonuses, despawns)"),
    ]
    for key, desc in stat_items:
        pdf.bullet(key, desc, key_w=42, indent=2)

    # ── CS Meter ──────────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.sub_header("CS METER  -  Charge shot charge bar")
    pdf.body(
        "Adds a visual progress bar below the reticle showing how charged your "
        "shot is. The bar fills from 0 to 100% as the charge timer climbs from "
        "0 to 10 - hitting 100% is the first frame you can release for a charge "
        "shot. The bar stays solid once lock-on search activates (timer > 20)."
    )

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 4 - VISUALIZERS, ENEMY HP, MACRO
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    # ── Visualizers ───────────────────────────────────────────────────────────
    pdf.section_header("VISUALIZERS")
    pdf.body(
        "Open via Display > VISUALS. Renders coloured geometric overlays on top "
        "of the game world to help study hitboxes and actor spawn triggers."
    )
    pdf.vspace(1)

    pdf.sub_header("Hitbox options")
    hb_items = [
        ("HITBOXES",        "Master toggle - enables the hitbox rendering system"),
        ("ACTORS",          "Draw hitboxes around enemy actors"),
        ("SCENERY",         "Draw hitboxes around destructible scenery"),
        ("ITEMS",           "Draw hitboxes around collectibles (rings, bombs, upgrades)"),
        ("PLAYER",          "Draw the player Arwing hitbox"),
        ("FLASH",           "Flash actors bright white when they take a hit"),
    ]
    for key, desc in hb_items:
        pdf.bullet(key, desc, key_w=28, indent=2)

    pdf.vspace(2)
    pdf.sub_header("Spawn zone options")
    sz_items = [
        ("SPAWN ZONES",     "Master toggle - show actor spawn trigger volumes"),
        ("SPAWN ACTORS",    "Highlight enemy spawn triggers"),
        ("SPAWN ITEMS",     "Highlight item spawn triggers"),
        ("SPAWN SCENERY",   "Highlight scenery spawn triggers"),
    ]
    for key, desc in sz_items:
        pdf.bullet(key, desc, key_w=36, indent=2)

    # ── Enemy HP ──────────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("ENEMY HP DISPLAY")
    pdf.body(
        "Shows live HP numbers floating above enemies. Accessed via "
        "Visualizers > ENEMY HP. Options:"
    )
    pdf.vspace(1)

    hp_items = [
        ("SHOW",         "ON / OFF - master toggle for enemy HP numbers"),
        ("SORT",         "NEAREST (closest enemy first) or HIGH HP (descending HP)"),
        ("MIN HP",       "Only show enemies at or above this HP threshold (0 = all, "
                         "options: 0 / 1 / 5 / 10 / 25 / 50)"),
        ("FILTER",       "ALL enemies, or BOSSES ONLY"),
        ("MODELS",       "Toggle rendering of enemy 3D models on/off - hide to "
                         "reduce visual clutter while still seeing HP numbers"),
    ]
    for key, desc in hp_items:
        pdf.bullet(key, desc, key_w=22, indent=2)

    pdf.vspace(2)
    pdf.callout("TIP",
        "Setting FILTER to BOSSES ONLY and hiding enemy MODELS is useful during "
        "boss fights - you see the boss HP bar without cluttering the screen.",
        label_color=ACCENT_GRN)

    # ── Macro system ──────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("INPUT MACRO  -  Record and Replay")
    pdf.body(
        "The macro system records your controller inputs frame-by-frame and plays "
        "them back deterministically. Open via Display > MACRO."
    )
    pdf.vspace(1)

    macro_items = [
        ("RECORD",      "Press to start recording; press again to stop. "
                        "Inputs are captured from the current game position."),
        ("PLAY",        "Replay the recorded inputs. If BIND STATE is on, "
                        "playback first restores the bound save state."),
        ("REWIND",      "Jump the playback head back to frame 0."),
        ("TRIM",        "Discard everything after the current playback position."),
        ("FRAMES",      "Displays the total number of recorded frames (read-only)."),
        ("BIND STATE",  "Link the macro to the active save state. "
                        "Playback will auto-load that state before replaying."),
        ("LOOP",        "Continuously replay the macro from the start."),
    ]
    for key, desc in macro_items:
        pdf.bullet(key, desc, key_w=32, indent=2)

    pdf.vspace(2)
    pdf.callout("TIP",
        "Workflow: save your position (D-Left), open Macro > RECORD, perform the "
        "inputs you want to study, stop recording, then enable BIND STATE and "
        "press PLAY. The macro will reload your save state and replay the exact "
        "same inputs on each loop - perfect for frame-perfect technique practice.",
        label_color=ACCENT_GRN)

    # ══════════════════════════════════════════════════════════════════════════
    # PAGE 5 - SD CARD, TIPS & TRICKS
    # ══════════════════════════════════════════════════════════════════════════
    pdf.add_page()

    # ── SD Card ───────────────────────────────────────────────────────────────
    pdf.section_header("SD CARD SAVES")

    pdf.callout(
        "NOT WORKING YET ON EVERDRIVE",
        "SD card saving is in active development and does NOT currently work on "
        "EverDrive cartridges. Do not use it to store saves you care about - "
        "it may crash or silently fail. This section documents the intended "
        "behaviour once the feature ships in a stable release.",
        label_color=ACCENT_RED,
        fill=(30, 5, 8),
    )

    pdf.vspace(2)
    pdf.body(
        "Once finished, the SD CARD feature will let you persist snapshots across "
        "sessions to a FAT-formatted SD card (targeting EverDrive-64 X7/X8). "
        "RAM save slots are lost when the console is powered off - SD saves will "
        "be how you keep setups permanently."
    )
    pdf.vspace(1)

    sd_items = [
        ("SD SAVE",   "Name and write the current state to the SD card. "
                      "A simple on-screen keyboard lets you enter a file name."),
        ("SD LOAD",   "Browse saved snapshots on the card and load one. "
                      "Use the stick/D-pad to scroll, A to select, B to cancel."),
    ]
    for key, desc in sd_items:
        pdf.bullet(key, desc, key_w=22, indent=2)

    pdf.vspace(2)
    pdf.callout("WHEN IT IS READY",
        "Watch sageraces.com and staging.sageraces.com for updates. "
        "SD card support will be announced when it is stable and tested on hardware.",
        label_color=ACCENT_GRN)

    # ── Frame Advance details ─────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("FRAME ADVANCE - DETAILED REFERENCE")
    pdf.body(
        "Frame advance lets you step through gameplay one frame at a time - "
        "invaluable for studying hitbox interactions, input windows, and timing."
    )
    pdf.vspace(1)

    fa_rows = [
        ("D-Down",       "Toggle pause on/off"),
        ("D-Up (tap)",   "If running: pause. If already paused: step one frame"),
        ("D-Up (hold)",  "After a short delay (~12 frames), auto-step at 2-frame cadence"),
    ]
    for i, (combo, action) in enumerate(fa_rows):
        pdf._qr_row(combo, action, alt=(i % 2 == 1))

    pdf.vspace(2)
    pdf.callout("NOTE",
        "While paused, buttons you hold are re-injected as fresh presses on each "
        "stepped frame. This means holding A while stepping will fire lasers or "
        "accumulate charge shot charge, just as it would during normal play.",
        label_color=ACCENT_CYAN)

    # ── Tips & Tricks ─────────────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.section_header("TIPS & TRICKS")

    tips = [
        ("Charge shot window practice",
         "Enable CS METER from Display > CS METER. The bar reaches full "
         "(100%) at the exact frame you can release for a charge shot. "
         "Enable CHARGE TIMING (Stats overlay) to see whether you released "
         "early or late, measured in frames."),
        ("Studying a specific section",
         "Save at the start of the section (D-Left), play through it, then "
         "quick-load (D-Right) to return instantly. No menus needed - you "
         "can repeat the segment as fast as you can press D-Right."),
        ("Comparing routes",
         "Use multiple save slots (L/R to cycle) to save checkpoints at "
         "different points and measure route times against each other."),
        ("Hitbox verification",
         "Pause the game, enable Free Camera and Hitboxes (VISUALS), then "
         "use the camera to inspect hitbox geometry from any angle. "
         "Place a bomb-range marker (D-Right, then A) to see whether "
         "enemies fall within charge-shot blast radius."),
        ("Boss practice",
         "Use Menu > LEVELS > BOSS TEST to jump straight into Corneria "
         "Carrier boss. Combine with INF HP cheat to study attack patterns "
         "without dying."),
        ("Return to title quickly",
         "Hold START for about 1.5 seconds. The game fades to the title "
         "screen. Useful for resetting a run without powering off."),
    ]

    for title, body in tips:
        y0 = pdf.get_y()
        # Left accent bar colour
        pdf.set_fill_color(*ACCENT_GRN)
        pdf.rect(M_L, y0, 2, 1, style="F")  # placeholder - overwritten below

        # Panel
        # measure multi_cell height first (rough: 1 body row = ~4.5 mm)
        lines_est = max(1, len(body) // 80 + 1)
        h = 5 + lines_est * 4.5 + 2
        pdf.panel(M_L, y0, INNER_W, h, fill=PANEL_DARK, border=ACCENT_GRN)
        pdf.set_fill_color(*ACCENT_GRN)
        pdf.rect(M_L, y0, 2.5, h, style="F")

        pdf.set_xy(M_L + 4, y0 + 1.2)
        pdf.set_font("Courier", "B", FS_BODY)
        pdf.set_text_color(*ACCENT_GRN)
        pdf.cell(INNER_W - 6, 4, title.upper())

        pdf.set_xy(M_L + 4, y0 + 5.5)
        pdf.set_font("Helvetica", "", FS_SMALL)
        pdf.set_text_color(*TEXT_MAIN)
        pdf.multi_cell(INNER_W - 6, 4.2, body, new_x=XPos.LMARGIN, new_y=YPos.NEXT)
        bottom = pdf.get_y()
        pdf.set_y(max(y0 + h, bottom) + 2)

    # ── Bottom rule & version ─────────────────────────────────────────────────
    pdf.vspace(3)
    pdf.h_rule(color=BORDER)
    pdf.set_font("Courier", "", 6.5)
    pdf.set_text_color(*TEXT_DIM)
    pdf.cell(0, 5,
        "SF64 Practice ROM v0.5  |  github.com/qwoodmansee/sf64-practice-rom  |  "
        "For questions, visit the SF64 Speedrunning Discord",
        align="C")

    pdf.output(OUTPUT)
    print(f"Wrote: {OUTPUT}")


if __name__ == "__main__":
    build_pdf()
