"""
stitch_sprite_sheet.py  —  UpStage v0.4
========================================
Stitches a folder of numbered PNG frames (e.g. from a Blender render or KnobMan export)
into a single vertical sprite sheet ready for UpStageLookAndFeel.

Usage:
    python stitch_sprite_sheet.py <frames_folder> <output.png> [--frame-size N]

Arguments:
    frames_folder   Folder containing PNG files named 0001.png … NNNN.png
                    (any numeric prefix naming scheme is fine; files are sorted numerically)
    output.png      Output sprite sheet path
    --frame-size N  Resize each frame to N×N pixels  (default: 128)

Example:
    python stitch_sprite_sheet.py blender_renders/ Assets/knob_sheet.png --frame-size 128

Requirements:
    pip install Pillow

Output format:
    A single PNG that is (frame_size) wide by (frame_size × num_frames) tall.
    JUCE reads frame N as:  sheet.getClippedImage({ 0, N * frame_size, frame_size, frame_size })
"""

import sys
import os
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is not installed.  Run:  pip install Pillow")
    sys.exit(1)


def parse_args():
    parser = argparse.ArgumentParser(description="Stitch PNG frames into a JUCE knob sprite sheet.")
    parser.add_argument("frames_folder", help="Folder of numbered PNG frames")
    parser.add_argument("output",        help="Output sprite sheet PNG path")
    parser.add_argument("--frame-size",  type=int, default=128,
                        help="Output frame size in pixels (default 128)")
    return parser.parse_args()


def collect_frames(folder: Path) -> list[Path]:
    """Return all PNG files in folder, sorted numerically by filename."""
    pngs = [f for f in folder.iterdir() if f.suffix.lower() == ".png"]
    if not pngs:
        raise ValueError(f"No PNG files found in {folder}")

    def sort_key(p: Path):
        # Extract leading digits from stem for numeric sort
        stem = p.stem.lstrip("0") or "0"
        try:
            return int("".join(filter(str.isdigit, stem)))
        except ValueError:
            return 0

    return sorted(pngs, key=sort_key)


def stitch(frames: list[Path], output: Path, frame_size: int):
    n = len(frames)
    sheet = Image.new("RGBA", (frame_size, frame_size * n), (0, 0, 0, 0))

    for i, path in enumerate(frames):
        frame = Image.open(path).convert("RGBA")
        if frame.size != (frame_size, frame_size):
            frame = frame.resize((frame_size, frame_size), Image.LANCZOS)
        sheet.paste(frame, (0, i * frame_size))

        if (i + 1) % 16 == 0 or i == n - 1:
            print(f"  Processed {i + 1}/{n} frames…")

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(str(output), "PNG", optimize=True)
    size_kb = output.stat().st_size / 1024
    print(f"\n✅  Sprite sheet saved: {output}")
    print(f"   Dimensions : {frame_size} × {frame_size * n} px")
    print(f"   Frames     : {n}")
    print(f"   File size  : {size_kb:.1f} KB")
    print(f"\nIn Projucer / BinaryData, add this file as a resource named 'knob_sheet_png'")
    print("or load it at runtime via:")
    print("  laf.loadKnobSpriteSheet(juce::File(\"path/to/knob_sheet.png\"));")


def main():
    args = parse_args()
    folder = Path(args.frames_folder)
    output = Path(args.output)

    if not folder.is_dir():
        print(f"ERROR: '{folder}' is not a directory.")
        sys.exit(1)

    print(f"Collecting frames from: {folder}")
    frames = collect_frames(folder)
    print(f"Found {len(frames)} frames.  Frame size: {args.frame_size}px")
    stitch(frames, output, args.frame_size)


if __name__ == "__main__":
    main()
