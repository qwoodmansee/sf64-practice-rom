import { describe, it, expect } from 'vitest';
import { hexToRgb, rgbToHex, luminance, contrastText } from '../src/colors';

describe('hexToRgb', () => {
  it('parses white with hash', () => {
    expect(hexToRgb('#ffffff')).toEqual([255, 255, 255]);
  });

  it('parses white without hash', () => {
    expect(hexToRgb('ffffff')).toEqual([255, 255, 255]);
  });

  it('parses black', () => {
    expect(hexToRgb('#000000')).toEqual([0, 0, 0]);
  });

  it('parses a known color (red)', () => {
    expect(hexToRgb('#ff0000')).toEqual([255, 0, 0]);
  });

  it('parses a known color (green)', () => {
    expect(hexToRgb('#00ff00')).toEqual([0, 255, 0]);
  });

  it('parses a known color (blue)', () => {
    expect(hexToRgb('#0000ff')).toEqual([0, 0, 255]);
  });

  it('parses mixed color', () => {
    expect(hexToRgb('#7FC224')).toEqual([127, 194, 36]);
  });
});

describe('rgbToHex', () => {
  it('converts white', () => {
    expect(rgbToHex(255, 255, 255)).toBe('#ffffff');
  });

  it('converts black', () => {
    expect(rgbToHex(0, 0, 0)).toBe('#000000');
  });

  it('converts red', () => {
    expect(rgbToHex(255, 0, 0)).toBe('#ff0000');
  });

  it('pads single-digit hex components', () => {
    expect(rgbToHex(1, 2, 3)).toBe('#010203');
  });

  it('roundtrips hexToRgb', () => {
    const original = '#3d42a0';
    const [r, g, b] = hexToRgb(original);
    expect(rgbToHex(r, g, b)).toBe(original);
  });
});

describe('luminance', () => {
  it('returns 0 for black', () => {
    expect(luminance(0, 0, 0)).toBe(0);
  });

  it('returns ~1 for white', () => {
    const lum = luminance(255, 255, 255);
    expect(lum).toBeCloseTo(1, 3);
  });

  it('returns a value between 0 and 1 for mid-tone', () => {
    const lum = luminance(128, 128, 128);
    expect(lum).toBeGreaterThan(0);
    expect(lum).toBeLessThan(1);
  });

  it('green channel contributes more than red which contributes more than blue', () => {
    const lumR = luminance(255, 0, 0);
    const lumG = luminance(0, 255, 0);
    const lumB = luminance(0, 0, 255);
    expect(lumG).toBeGreaterThan(lumR);
    expect(lumR).toBeGreaterThan(lumB);
  });
});

describe('contrastText', () => {
  it('returns dark text for a light background', () => {
    expect(contrastText(255, 255, 255)).toBe('#1A1A2E');
  });

  it('returns white text for a dark background', () => {
    expect(contrastText(0, 0, 0)).toBe('#FFFFFF');
  });

  it('returns white text for a dark blue background', () => {
    expect(contrastText(10, 10, 20)).toBe('#FFFFFF');
  });

  it('returns dark text for a bright green background', () => {
    // luminance(127, 194, 36) is > 0.35
    const [r, g, b] = hexToRgb('#7FC224');
    expect(contrastText(r, g, b)).toBe('#1A1A2E');
  });
});
