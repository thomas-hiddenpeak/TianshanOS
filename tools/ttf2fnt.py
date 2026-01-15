#!/usr/bin/env python3
"""
TTF to FNT Converter for TianShanOS LED Matrix

Converts TrueType fonts to compact bitmap font format (.fnt) for
embedded LED matrix display. Supports ASCII and CJK characters.

Font file format (.fnt):
  - Header (16 bytes): magic, version, dimensions, glyph count
  - Index table: sorted by codepoint for binary search
  - Bitmap data: packed bits, ceil(width*height/8) bytes per glyph

Usage:
  python ttf2fnt.py BoutiqueBitmap9x9.ttf -o boutique9x9.fnt --size 9
  python ttf2fnt.py font.ttf -o ascii.fnt --size 9 --charset ascii
  python ttf2fnt.py font.ttf -o cjk.fnt --size 9 --charset gb2312

Requirements:
  pip install pillow fonttools

Author: TianShanOS Team
License: MIT
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow is required. Install with: pip install pillow")
    sys.exit(1)

# Font file magic and version
FONT_MAGIC = b'TFNT'
FONT_VERSION = 1

# Character sets
CHARSET_ASCII = list(range(0x20, 0x7F))  # 32-126, 95 chars

# GB2312 常用汉字 (简化版，约3000字)
# 完整版需要从 GB2312 编码表生成
def get_gb2312_chars():
    """Generate GB2312 character list (simplified Chinese)"""
    chars = []
    # Level 1: 3755 chars (0xB0A1 - 0xD7F9)
    for b1 in range(0xB0, 0xD8):
        for b2 in range(0xA1, 0xFF):
            if b1 == 0xD7 and b2 > 0xF9:
                break
            try:
                char = bytes([b1, b2]).decode('gb2312')
                chars.append(ord(char))
            except:
                pass
    return chars

# 常用500字 (精选高频汉字)
COMMON_500 = """
的一是不了在人有我他这个们中来上大为和国地到以说时要就出会可也你对生能而子那得于着下自之年过发后作里用道行所然家种事成方多经么去法学如都同现当没动面起看定天分还进好小部其些主样理心她本前开但因只从想实日军者意无力它与长把机十民第公此已工使情明性知全三又关点正业外将两高间由问很最重并物手应战向头文体政美相见被利什二等产或新己制身果加西斯月话合回特代内信表化老给世位次度门任常先海通教儿原东声提立及比员解水名真论处走义各入几口认条平系气题活尔更别打女变四神总何电数安少报才结反受目太量再感建务做接必场件计管期市直德资命山金指克许统区保至队形社便空决治展马原术备半办青省列习响约支般史感劳便团统程府论青规热世调断识离满领馆术造类济程参切白快况候观越织装影算低持音众书布复容儿须际商非验连断深难近矿千周委素技备任环战续轮层取置候率胜持务算亲极注联效限备劳志请片端义止送决算片段断完调效限端段式存注影刻料备况周规众武
""".strip().replace('\n', '')

CHARSET_COMMON_500 = [ord(c) for c in COMMON_500 if ord(c) > 127]


def render_glyph(font, codepoint, size):
    """
    Render a single character to a bitmap.
    
    Returns:
        bytes: Packed bitmap data, or None if character not available
        int: Actual width of the glyph
    """
    char = chr(codepoint)
    
    # Create image slightly larger than font size
    img_size = size + 4
    img = Image.new('1', (img_size, img_size), 0)
    draw = ImageDraw.Draw(img)
    
    # Get character bounding box
    try:
        bbox = font.getbbox(char)
        if bbox is None:
            return None, 0
    except:
        return None, 0
    
    # Calculate centering offset
    char_width = bbox[2] - bbox[0]
    char_height = bbox[3] - bbox[1]
    
    # For pixel fonts, use exact positioning
    x_offset = (size - char_width) // 2
    y_offset = (size - char_height) // 2 - bbox[1]
    
    # Draw character
    draw.text((x_offset, y_offset), char, font=font, fill=1)
    
    # Crop to exact size
    img = img.crop((0, 0, size, size))
    
    # Convert to packed bits
    pixels = list(img.getdata())
    packed = []
    byte = 0
    bit_pos = 0
    
    for pixel in pixels:
        if pixel:
            byte |= (1 << (7 - bit_pos))
        bit_pos += 1
        if bit_pos == 8:
            packed.append(byte)
            byte = 0
            bit_pos = 0
    
    # Handle remaining bits
    if bit_pos > 0:
        packed.append(byte)
    
    return bytes(packed), char_width


def create_font_file(ttf_path, output_path, size, charset_name, verbose=False):
    """
    Convert TTF font to .fnt format.
    
    Args:
        ttf_path: Path to TTF file
        output_path: Output .fnt file path
        size: Font size in pixels
        charset_name: 'ascii', 'common500', or 'gb2312'
        verbose: Print progress
    """
    # Load TTF font
    try:
        font = ImageFont.truetype(str(ttf_path), size)
    except Exception as e:
        print(f"Error loading font: {e}")
        return False
    
    # Select character set
    if charset_name == 'ascii':
        codepoints = CHARSET_ASCII
    elif charset_name == 'common500':
        codepoints = CHARSET_ASCII + CHARSET_COMMON_500
    elif charset_name == 'gb2312':
        codepoints = CHARSET_ASCII + get_gb2312_chars()
    else:
        print(f"Unknown charset: {charset_name}")
        return False
    
    # Remove duplicates and sort
    codepoints = sorted(set(codepoints))
    
    if verbose:
        print(f"Processing {len(codepoints)} characters...")
    
    # Render all glyphs
    glyphs = []
    for i, cp in enumerate(codepoints):
        bitmap, width = render_glyph(font, cp, size)
        if bitmap:
            glyphs.append((cp, bitmap, width))
        
        if verbose and (i + 1) % 500 == 0:
            print(f"  Processed {i + 1}/{len(codepoints)}")
    
    if verbose:
        print(f"Successfully rendered {len(glyphs)} glyphs")
    
    if not glyphs:
        print("Error: No glyphs could be rendered")
        return False
    
    # Calculate sizes
    bytes_per_glyph = (size * size + 7) // 8
    header_size = 16
    index_entry_size = 6  # 2 bytes codepoint + 4 bytes offset
    index_size = len(glyphs) * index_entry_size
    
    # Build file
    with open(output_path, 'wb') as f:
        # Write header (16 bytes)
        # magic (4) + version (1) + width (1) + height (1) + flags (1) + 
        # glyph_count (4) + index_offset (4)
        header = struct.pack('<4sBBBBII',
            FONT_MAGIC,
            FONT_VERSION,
            size,  # width
            size,  # height
            0,     # flags (0 = monospace)
            len(glyphs),
            header_size  # index starts right after header
        )
        f.write(header)
        
        # Write index table
        bitmap_offset = header_size + index_size
        for cp, bitmap, width in glyphs:
            # codepoint (2 bytes) + offset (4 bytes)
            entry = struct.pack('<HI', cp, bitmap_offset)
            f.write(entry)
            bitmap_offset += len(bitmap)
        
        # Write bitmap data
        for cp, bitmap, width in glyphs:
            f.write(bitmap)
    
    # Report
    file_size = Path(output_path).stat().st_size
    print(f"Created: {output_path}")
    print(f"  Glyphs: {len(glyphs)}")
    print(f"  Size: {size}x{size} pixels")
    print(f"  File size: {file_size:,} bytes ({file_size/1024:.1f} KB)")
    
    return True


def preview_glyph(ttf_path, char, size):
    """Preview a single character rendering (for debugging)"""
    font = ImageFont.truetype(str(ttf_path), size)
    
    img = Image.new('1', (size, size), 0)
    draw = ImageDraw.Draw(img)
    
    bbox = font.getbbox(char)
    if bbox:
        x_offset = (size - (bbox[2] - bbox[0])) // 2
        y_offset = (size - (bbox[3] - bbox[1])) // 2 - bbox[1]
        draw.text((x_offset, y_offset), char, font=font, fill=1)
    
    # Print ASCII art
    pixels = list(img.getdata())
    print(f"Character: '{char}' (U+{ord(char):04X})")
    print("+" + "-" * size + "+")
    for y in range(size):
        row = ""
        for x in range(size):
            row += "█" if pixels[y * size + x] else " "
        print("|" + row + "|")
    print("+" + "-" * size + "+")


def main():
    parser = argparse.ArgumentParser(
        description='Convert TTF font to TianShanOS .fnt format'
    )
    parser.add_argument('ttf', help='Input TTF font file')
    parser.add_argument('-o', '--output', required=True, help='Output .fnt file')
    parser.add_argument('-s', '--size', type=int, default=9, 
                        help='Font size in pixels (default: 9)')
    parser.add_argument('-c', '--charset', default='ascii',
                        choices=['ascii', 'common500', 'gb2312'],
                        help='Character set (default: ascii)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show progress')
    parser.add_argument('--preview', metavar='CHAR',
                        help='Preview a character and exit')
    
    args = parser.parse_args()
    
    ttf_path = Path(args.ttf)
    if not ttf_path.exists():
        print(f"Error: Font file not found: {ttf_path}")
        sys.exit(1)
    
    if args.preview:
        preview_glyph(ttf_path, args.preview, args.size)
        return
    
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    success = create_font_file(
        ttf_path, 
        output_path, 
        args.size, 
        args.charset,
        args.verbose
    )
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
