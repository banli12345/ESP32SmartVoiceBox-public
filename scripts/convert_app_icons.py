#!/usr/bin/env python3
"""Convert app icons, wallpapers, and Photos images to LVGL v8 BIN files."""

from collections import deque
import argparse
from pathlib import Path
import struct

from PIL import Image, ImageOps


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIR = PROJECT_ROOT / "assets" / "icon_sources"
OUTPUT_DIR = PROJECT_ROOT / "sd_card_files" / "icons"
ICON_SIZE = 64

ICONS = {
    "music": "2542688b2f8c3ebe.webp",
    "settings": "2804e9bd0ab2c907.webp",
    "weather": "577590242c3387f6.webp",
    "clock": "df46caefc44daab6.webp",
    "photos": "9fa58dc8a8194be2.webp",
    "browser": "OIP-C (2).webp",
    "video": "ba847f570e8931bc.webp",
    "calendar": "8585ab1e8c87a133.webp",
}

WALLPAPERS = {
    "wallpaper1": "wallpaper1.webp",
    "wallpaper2": "wallpaper2.webp",
    "wallpaper3": "wallpaper3.webp",
}

PHOTO_IMAGES = {
    "image1": "1.jpeg",
    "image2": "2.jpeg",
}


def remove_checkerboard(image: Image.Image, remove_all_neutral=False) -> Image.Image:
    """Make the light checkerboard transparent without erasing enclosed white art."""
    image = image.convert("RGB")
    width, height = image.size
    pixels = list(image.getdata())
    candidate = bytearray(width * height)

    for index, (red, green, blue) in enumerate(pixels):
        if max(red, green, blue) - min(red, green, blue) <= 16 and min(red, green, blue) >= 180:
            candidate[index] = 1

    if remove_all_neutral:
        transparent = candidate
    else:
        transparent = bytearray(width * height)
        queue = deque()

        def add_edge(index):
            if candidate[index] and not transparent[index]:
                transparent[index] = 1
                queue.append(index)

        for x in range(width):
            add_edge(x)
            add_edge((height - 1) * width + x)
        for y in range(height):
            add_edge(y * width)
            add_edge(y * width + width - 1)

        while queue:
            index = queue.popleft()
            x = index % width
            y = index // width
            for ny in range(max(0, y - 1), min(height, y + 2)):
                row = ny * width
                for nx in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = row + nx
                    if candidate[neighbor] and not transparent[neighbor]:
                        transparent[neighbor] = 1
                        queue.append(neighbor)

    rgba = image.convert("RGBA")
    alpha = Image.new("L", (width, height), 255)
    alpha.putdata([0 if value else 255 for value in transparent])
    rgba.putalpha(alpha)
    return rgba


def encode_lvgl_bin(image: Image.Image, target: Path, width: int, height: int = None,
                    preserve_aspect: bool = False) -> None:
    if height is None:
        height = width
    if preserve_aspect:
        image = ImageOps.pad(image, (width, height), method=Image.Resampling.LANCZOS,
                             color=(0, 0, 0))
    else:
        image = ImageOps.fit(image, (width, height), Image.Resampling.LANCZOS)
    canvas = image.convert("RGBA")

    # LVGL v8 header: cf=LV_IMG_CF_TRUE_COLOR_ALPHA (5), then 11-bit width/height.
    header = 5 | (width << 10) | (height << 21)
    payload = bytearray()
    for red, green, blue, alpha in canvas.getdata():
        rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        payload.extend(struct.pack("<HB", rgb565, alpha))

    target.write_bytes(struct.pack("<I", header) + payload)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icon", type=Path,
                        help="convert one image into an app icon BIN")
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE_DIR,
                        help=f"batch source directory (default: {DEFAULT_SOURCE_DIR})")
    parser.add_argument("--name", help="output icon name (without .bin)")
    parser.add_argument("--size", type=int, default=ICON_SIZE,
                        help=f"square icon size in pixels (default: {ICON_SIZE})")
    args = parser.parse_args()

    if args.icon is not None:
        if not args.name:
            parser.error("--name is required together with --icon")
        if args.size <= 0 or args.size > 2047:
            parser.error("--size must be between 1 and 2047")
        source = args.icon.expanduser().resolve()
        if not source.is_file():
            raise FileNotFoundError(f"Icon source not found: {source}")
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        target = OUTPUT_DIR / f"{args.name}.bin"
        encode_lvgl_bin(Image.open(source), target, args.size)
        print(f"{target}: {target.stat().st_size} bytes ({args.size}x{args.size})")
        return

    if args.name:
        parser.error("--name is only valid together with --icon")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    source_dir = args.source_dir.expanduser().resolve()
    for name, filename in ICONS.items():
        source = source_dir / filename
        if not source.is_file():
            raise FileNotFoundError(f"Icon source not found: {source}")
        image = Image.open(source)
        image = remove_checkerboard(image)
        target = OUTPUT_DIR / f"{name}.bin"
        encode_lvgl_bin(image, target, ICON_SIZE)
        print(f"{target.name}: {target.stat().st_size} bytes")

    wallpaper_dir = OUTPUT_DIR.parent / "wallpapers"
    wallpaper_dir.mkdir(parents=True, exist_ok=True)
    for name, filename in WALLPAPERS.items():
        source = source_dir / filename
        if not source.is_file():
            raise FileNotFoundError(f"Wallpaper source not found: {source}")
        target = wallpaper_dir / f"{name}.bin"
        encode_lvgl_bin(Image.open(source), target, 480)
        print(f"{target.name}: {target.stat().st_size} bytes")

    photo_dir = OUTPUT_DIR.parent / "imges"
    photo_dir.mkdir(parents=True, exist_ok=True)
    photo_source_dir = source_dir / "图片"
    for name, filename in PHOTO_IMAGES.items():
        source = photo_source_dir / filename
        if not source.is_file():
            raise FileNotFoundError(f"Photo source not found: {source}")
        target = photo_dir / f"{name}.bin"
        encode_lvgl_bin(Image.open(source), target, 480, 450, preserve_aspect=True)
        print(f"{target.name}: {target.stat().st_size} bytes")


if __name__ == "__main__":
    main()
