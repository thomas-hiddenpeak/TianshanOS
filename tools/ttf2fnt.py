#!/usr/bin/env python3
"""
TTF to FNT Converter for TianShanOS LED Matrix

Converts TrueType fonts to compact bitmap font format (.fnt) for
embedded LED matrix display. Exports ALL glyphs from the font.

Font file format (.fnt):
  - Header (16 bytes): magic, version, dimensions, glyph count
  - Index table: sorted by codepoint for binary search
  - Bitmap data: packed bits, ceil(width*height/8) bytes per glyph

Usage:
  python ttf2fnt.py font.ttf -o output.fnt --size 12
  python ttf2fnt.py font.ttf -o output.fnt --size 9 --preview 中

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

try:
    from fontTools.ttLib import TTFont
except ImportError:
    print("Error: fonttools is required. Install with: pip install fonttools")
    sys.exit(1)

# Font file magic and version
FONT_MAGIC = b'TFNT'
FONT_VERSION = 1


def get_font_codepoints(ttf_path):
    """
    Get all Unicode codepoints supported by the font.
    
    Returns:
        list: Sorted list of codepoints
    """
    font = TTFont(str(ttf_path))
    codepoints = set()
    
    for table in font['cmap'].tables:
        if table.isUnicode():
            codepoints.update(table.cmap.keys())
    
    font.close()
    
    # Filter to printable characters (exclude control chars)
    codepoints = [cp for cp in codepoints if cp >= 0x20]
    return sorted(codepoints)


def render_glyph(font, codepoint, size, target_width, target_height):
    """
    Render a single character to a bitmap at native size (no scaling).
    Output bitmap is padded to target_width x target_height.
    
    Args:
        font: PIL ImageFont object
        codepoint: Unicode codepoint to render
        size: Font size for rendering
        target_width: Target bitmap width (for padding)
        target_height: Target bitmap height (for padding)
    
    Returns:
        bytes: Packed bitmap data (target_width x target_height), or None
        int: Actual glyph width (before padding)
    """
    char = chr(codepoint)
    
    # Get character bounding box
    try:
        bbox = font.getbbox(char)
        if bbox is None:
            return None, 0
        # Check if glyph is empty
        if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            # Return minimal bitmap for space
            if codepoint == 0x20:
                empty_size = (target_width * target_height + 7) // 8
                return bytes(empty_size), size // 3
            return None, 0
    except:
        return None, 0
    
    # Render in grayscale then threshold
    glyph_width = bbox[2] - bbox[0]
    glyph_height = bbox[3] - bbox[1]
    
    canvas_w = glyph_width + 2
    canvas_h = glyph_height + 2
    img = Image.new('L', (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((1 - bbox[0], 1 - bbox[1]), char, font=font, fill=255)
    
    # Find actual content bounds
    content_bbox = img.getbbox()
    if content_bbox is None:
        return None, 0
    
    # Crop to content
    glyph_img = img.crop(content_bbox)
    actual_width = content_bbox[2] - content_bbox[0]
    actual_height = content_bbox[3] - content_bbox[1]
    
    # Threshold to binary
    glyph_binary = glyph_img.point(lambda x: 1 if x > 20 else 0, mode='1')
    
    # Create padded output image (centered)
    final_img = Image.new('1', (target_width, target_height), 0)
    x_offset = (target_width - actual_width) // 2
    y_offset = (target_height - actual_height) // 2
    final_img.paste(glyph_binary, (x_offset, y_offset))
    
    # Convert to packed bits
    pixels = list(final_img.getdata())
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
    
    return bytes(packed), actual_width


def create_font_file(ttf_path, output_path, size, verbose=False):
    """
    Convert TTF font to .fnt format with all glyphs.
    
    Output format matches ts_led_font.c expectations:
    - Header: 16 bytes (magic, version, width, height, flags, glyph_count, index_offset)
    - Index: 6 bytes per entry (uint16_t codepoint, uint32_t offset)
    - Bitmaps: Fixed size (width * height + 7) / 8 bytes each
    
    Args:
        ttf_path: Path to TTF file
        output_path: Output .fnt file path
        size: Font size in pixels
        verbose: Print progress
    """
    # Get all codepoints from font
    if verbose:
        print(f"Scanning font for available glyphs...")
    
    codepoints = get_font_codepoints(ttf_path)
    if verbose:
        print(f"Found {len(codepoints)} characters in font")
    
    # Load TTF font
    try:
        font = ImageFont.truetype(str(ttf_path), size)
    except Exception as e:
        print(f"Error loading font: {e}")
        return False
    
    # First pass: determine max dimensions
    if verbose:
        print(f"Pass 1: Scanning glyph dimensions...")
    
    max_width = 0
    max_height = 0
    valid_codepoints = []
    
    for cp in codepoints:
        char = chr(cp)
        try:
            bbox = font.getbbox(char)
            if bbox and bbox[2] > bbox[0] and bbox[3] > bbox[1]:
                # Render to get actual content size
                gw = bbox[2] - bbox[0]
                gh = bbox[3] - bbox[1]
                img = Image.new('L', (gw + 2, gh + 2), 0)
                draw = ImageDraw.Draw(img)
                draw.text((1 - bbox[0], 1 - bbox[1]), char, font=font, fill=255)
                content = img.getbbox()
                if content:
                    w = content[2] - content[0]
                    h = content[3] - content[1]
                    max_width = max(max_width, w)
                    max_height = max(max_height, h)
                    valid_codepoints.append(cp)
            elif cp == 0x20:  # Space
                valid_codepoints.append(cp)
        except:
            pass
    
    if verbose:
        print(f"  Max glyph size: {max_width}x{max_height}")
        print(f"  Valid codepoints: {len(valid_codepoints)}")
    
    if max_width == 0 or max_height == 0:
        print("Error: Could not determine glyph dimensions")
        return False
    
    # Second pass: render all glyphs to fixed size
    if verbose:
        print(f"Pass 2: Rendering {len(valid_codepoints)} glyphs at {max_width}x{max_height}...")
    
    glyphs = []
    for i, cp in enumerate(valid_codepoints):
        bitmap, width = render_glyph(font, cp, size, max_width, max_height)
        if bitmap:
            # Filter: codepoint must fit in uint16_t
            if cp <= 0xFFFF:
                glyphs.append((cp, bitmap, width))
        
        if verbose and (i + 1) % 1000 == 0:
            print(f"  Rendered {i + 1}/{len(valid_codepoints)}")
    
    if verbose:
        print(f"Successfully rendered {len(glyphs)} glyphs")
    
    if not glyphs:
        print("Error: No glyphs could be rendered")
        return False
    
    # Sort by codepoint for binary search
    glyphs.sort(key=lambda x: x[0])
    
    # Calculate sizes (matching ts_led_font.c format)
    # Index entry: uint16_t codepoint (2) + uint32_t offset (4) = 6 bytes
    header_size = 16
    index_entry_size = 6
    index_size = len(glyphs) * index_entry_size
    bytes_per_glyph = (max_width * max_height + 7) // 8
    
    # Build file
    with open(output_path, 'wb') as f:
        # Write header (16 bytes)
        # magic(4) + version(1) + width(1) + height(1) + flags(1) + 
        # glyph_count(4) + index_offset(4)
        header = struct.pack('<4sBBBBII',
            FONT_MAGIC,
            FONT_VERSION,
            max_width,
            max_height,
            0,  # flags: 0 = fixed width
            len(glyphs),
            header_size  # index_offset
        )
        f.write(header)
        
        # Write index table
        bitmap_offset = header_size + index_size
        for cp, bitmap, width in glyphs:
            # codepoint(2) + offset(4)
            entry = struct.pack('<HI', cp, bitmap_offset)
            f.write(entry)
            bitmap_offset += bytes_per_glyph
        
        # Write bitmap data
        for cp, bitmap, width in glyphs:
            # Ensure correct size
            if len(bitmap) < bytes_per_glyph:
                bitmap = bitmap + bytes(bytes_per_glyph - len(bitmap))
            f.write(bitmap[:bytes_per_glyph])
    
    # Report
    file_size = Path(output_path).stat().st_size
    print(f"Created: {output_path}")
    print(f"  Glyphs: {len(glyphs)}")
    print(f"  Max size: {max_width}x{max_height} pixels")
    print(f"  File size: {file_size:,} bytes ({file_size/1024:.1f} KB)")
    
    return True


def preview_glyph(ttf_path, char, size):
    """Preview a single character rendering"""
    font = ImageFont.truetype(str(ttf_path), size)
    
    # Get glyph dimensions first
    bbox = font.getbbox(char)
    if not bbox or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        print(f"Character '{char}' (U+{ord(char):04X}) not available in font")
        return
    
    # Render at native size for preview
    gw = bbox[2] - bbox[0]
    gh = bbox[3] - bbox[1]
    img = Image.new('L', (gw + 2, gh + 2), 0)
    draw = ImageDraw.Draw(img)
    draw.text((1 - bbox[0], 1 - bbox[1]), char, font=font, fill=255)
    
    content = img.getbbox()
    if not content:
        print(f"Character '{char}' (U+{ord(char):04X}) rendered empty")
        return
    
    width = content[2] - content[0]
    height = content[3] - content[1]
    
    # Use same render function
    bitmap, actual_width = render_glyph(font, ord(char), size, width, height)
    
    if bitmap is None:
        print(f"Character '{char}' (U+{ord(char):04X}) render failed")
        return
    
    print(f"Character: '{char}' (U+{ord(char):04X})")
    print(f"Size: {width}x{height} pixels")
    print("+" + "-" * width + "+")
    
    bit_index = 0
    for y in range(height):
        row = "|"
        for x in range(width):
            byte_idx = bit_index // 8
            bit_pos = 7 - (bit_index % 8)
            if byte_idx < len(bitmap) and (bitmap[byte_idx] & (1 << bit_pos)):
                row += "█"
            else:
                row += " "
            bit_index += 1
        print(row + "|")
    print("+" + "-" * width + "+")


def main():
    parser = argparse.ArgumentParser(
        description='Convert TTF font to TianShanOS .fnt format (exports all glyphs)'
    )
    parser.add_argument('ttf', help='Input TTF font file')
    parser.add_argument('-o', '--output', required=True, help='Output .fnt file')
    parser.add_argument('-s', '--size', type=int, default=12, 
                        help='Font size in pixels (default: 12)')
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
        args.verbose
    )
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
