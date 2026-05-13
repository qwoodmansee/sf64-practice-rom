import PDFDocument from 'pdfkit';
import path from 'path';
import { C, W, H, ML, PW, HBAR, MT, MB, FONTS } from './theme';

// ─── Document state ───────────────────────────────────────────────────────────
export let doc: PDFKit.PDFDocument = null as unknown as PDFKit.PDFDocument;
export let pageNum = 0;

export function initDoc(fontsDir: string): PDFKit.PDFDocument {
  doc = new PDFDocument({ size: 'A4', margin: 0, autoFirstPage: false });
  doc.registerFont(FONTS.mono,  path.join(fontsDir, 'ShareTechMono-Regular.ttf'));
  doc.registerFont(FONTS.pixel, path.join(fontsDir, 'PressStart2P-Regular.ttf'));
  doc.registerFont(FONTS.vt,    path.join(fontsDir, 'VT323-Regular.ttf'));
  // Reset pageNum so initDoc can be called cleanly in tests
  pageNum = 0;
  return doc;
}

// ─── Gradient helper ─────────────────────────────────────────────────────────
export function linearGrad(
  x1: number, y1: number, x2: number, y2: number,
  stops: [number, string][]
): string {
  const g = doc.linearGradient(x1, y1, x2, y2);
  for (const [pos, color] of stops) g.stop(pos, color);
  return g as unknown as string;
}

// ─── Starfield (exported so pageCover can call it directly) ───────────────────
export function drawStarfield() {
  let s = (pageNum * 7919 + 31337) >>> 0;
  function r(): number {
    s = ((s * 1664525 + 1013904223) >>> 0);
    return (s >>> 1) / 0x7FFFFFFF;
  }

  doc.save();

  // One soft nebula glow
  doc.fillColor('#1A1050').fillOpacity(0.13);
  doc.circle(r() * W, r() * H, 55 + r() * 90).fill();

  // Star field
  for (let i = 0; i < 140; i++) {
    const x    = r() * W;
    const y    = r() * H;
    const roll = r();
    let radius: number, opacity: number, color: string;

    if (roll < 0.70) {
      radius  = 0.2 + r() * 0.3;
      opacity = 0.10 + r() * 0.13;
      color   = '#FFFFFF';
    } else if (roll < 0.93) {
      radius  = 0.45 + r() * 0.4;
      opacity = 0.13 + r() * 0.20;
      color   = r() > 0.55 ? '#B8CCFF' : '#FFFFFF';
    } else {
      radius  = 0.9 + r() * 0.8;
      opacity = 0.20 + r() * 0.22;
      color   = '#FFFFFF';
    }

    doc.fillColor(color).fillOpacity(opacity);
    doc.circle(x, y, radius).fill();
  }

  doc.fillOpacity(1);
  doc.restore();
}

// ─── Private page chrome helpers ──────────────────────────────────────────────
function pageHeader(section: string) {
  doc.rect(0, 0, W, HBAR).fill(C.panelDark);
  doc.moveTo(0, HBAR).lineTo(W, HBAR).strokeColor(C.grey).lineWidth(0.4).stroke();
  doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(8.5)
     .text('SF64 PRACTICE ROM', ML, 9, { lineBreak: false });
  if (section) {
    doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
       .text(section.toUpperCase(), ML, 10, { width: PW, align: 'right', lineBreak: false });
  }
}

function pageFooter() {
  doc.rect(0, H - HBAR, W, HBAR).fill(C.panelDark);
  doc.moveTo(0, H - HBAR).lineTo(W, H - HBAR).strokeColor(C.grey).lineWidth(0.4).stroke();
  doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
     .text(`sageraces.com  -  page ${pageNum}`, ML, H - 17, { width: PW, align: 'center', lineBreak: false });
}

function pageSideTab(section: string) {
  if (!section) return;
  const tabW = 18;
  const tabX = W - tabW;
  const tabTop = HBAR + 1;
  const tabH   = H - HBAR * 2 - 2;
  const cx = tabX + tabW / 2;
  const cy = tabTop + tabH / 2;

  doc.rect(tabX, tabTop, tabW, tabH).fill(C.panelDark);
  doc.rect(tabX, tabTop, 2, tabH).fill(C.greenBot);

  doc.save();
  doc.translate(cx, cy);
  doc.rotate(90, { origin: [0, 0] });
  doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(7)
     .text(section.toUpperCase(), -(tabH / 2 - 6), -3.5, {
       width: tabH - 12,
       align: 'center',
       lineBreak: false,
       characterSpacing: 1.6,
     });
  doc.restore();
}

// ─── Increment pageNum (for pageCover which handles its own page setup) ───────
export function bumpPageNum() { pageNum++; }

// ─── newPage ──────────────────────────────────────────────────────────────────
export function newPage(section = '') {
  pageNum++;
  doc.addPage();
  doc.rect(0, 0, W, H).fill(C.bg);
  drawStarfield();
  pageHeader(section);
  pageFooter();
  pageSideTab(section);
  doc.y = MT;
}
