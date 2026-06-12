import { describe, it, expect } from 'vitest';
import { tokenWidth, type BtnToken } from '../src/widgets/buttons';

// All 13 token types
const ALL_TOKENS: BtnToken[] = ['A', 'B', 'Z', 'R', 'L', 'START', 'CR', 'DU', 'DD', 'DL', 'DR', '+', '/'];

describe('tokenWidth', () => {
  it('returns a positive number for every token type', () => {
    for (const tok of ALL_TOKENS) {
      expect(tokenWidth(tok), `tokenWidth('${tok}') should be positive`).toBeGreaterThan(0);
    }
  });

  it('covers all 13 token types exhaustively', () => {
    expect(ALL_TOKENS).toHaveLength(13);
    const widths = ALL_TOKENS.map(tokenWidth);
    expect(widths.every(w => typeof w === 'number' && w > 0)).toBe(true);
  });

  it('Z is wider than A', () => {
    expect(tokenWidth('Z')).toBeGreaterThan(tokenWidth('A'));
  });

  it('+ is narrower than A', () => {
    expect(tokenWidth('+')).toBeLessThan(tokenWidth('A'));
  });

  it('/ is narrower than +', () => {
    expect(tokenWidth('/')).toBeLessThan(tokenWidth('+'));
  });

  it('L and R have the same width', () => {
    expect(tokenWidth('L')).toBe(tokenWidth('R'));
  });

  it('L/R are wider than A', () => {
    expect(tokenWidth('L')).toBeGreaterThan(tokenWidth('A'));
  });

  it('DPad directions are all equal', () => {
    const dpadWidth = tokenWidth('DU');
    expect(tokenWidth('DD')).toBe(dpadWidth);
    expect(tokenWidth('DL')).toBe(dpadWidth);
    expect(tokenWidth('DR')).toBe(dpadWidth);
  });

  it('START width equals A width (both use BTN_SZ + 2)', () => {
    expect(tokenWidth('START')).toBe(tokenWidth('A'));
  });

  it('CR width equals A width (both use BTN_SZ + 2)', () => {
    expect(tokenWidth('CR')).toBe(tokenWidth('A'));
  });

  it('Z width is 22', () => {
    expect(tokenWidth('Z')).toBe(22);
  });

  it('+ width is 9', () => {
    expect(tokenWidth('+')).toBe(9);
  });

  it('/ width is 7', () => {
    expect(tokenWidth('/')).toBe(7);
  });
});
