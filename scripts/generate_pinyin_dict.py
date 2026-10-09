"""Generate the UTF-8 pinyin dictionary consumed by LVGL's Chinese IME.

The generated text file is copied to the SD card and loaded by the firmware
at startup, so the complete table does not consume firmware flash.
"""

from collections import OrderedDict
from pathlib import Path

from pypinyin import Style, pinyin


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "sd_card_files" / "ime" / "pinyin_full.txt"
COMMON_SOURCE = ROOT / "font_assets" / "common_3500.txt"


def load_common_characters() -> list[str]:
    """Read the common simplified-character list in its source order."""
    raw = COMMON_SOURCE.read_bytes()
    text = raw.decode("utf-8")
    seen: set[str] = set()
    characters: list[str] = []
    for char in text:
        if not ("\u4e00" <= char <= "\u9fff") or char in seen:
            continue
        seen.add(char)
        characters.append(char)
    if not characters:
        raise RuntimeError(f"no CJK characters found in {COMMON_SOURCE}")
    return characters


def load_gb2312_characters() -> list[str]:
    """Decode all CJK characters assigned by GB2312, in table order.

    GB2312 contains punctuation and symbols in addition to Chinese.  The
    pinyin dictionary only needs the CJK portion, so non-Hanzi code points are
    filtered out here.  Decoding the byte pairs avoids assuming that the GB
    character set is one contiguous Unicode range.
    """
    characters: list[str] = []
    seen: set[str] = set()
    for lead in range(0xA1, 0xFF):
        for trail in range(0xA1, 0xFF):
            try:
                char = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(char) != 1 or not ("\u4e00" <= char <= "\u9fff"):
                continue
            if char not in seen:
                seen.add(char)
                characters.append(char)
    return characters


def load_dictionary_characters() -> list[str]:
    """Return common characters first, then the remaining GB2312 Hanzi."""
    common = load_common_characters()
    common_set = set(common)
    extras = [char for char in load_gb2312_characters() if char not in common_set]
    return common + extras


def main() -> None:
    # Put the common simplified characters first in every pinyin bucket.  The
    # remaining GB2312 characters keep the dictionary broad without allowing
    # uncommon glyphs to displace the characters users expect to see first.
    common = load_common_characters()
    common_set = set(common)
    gb2312 = load_gb2312_characters()
    characters = common + [char for char in gb2312 if char not in common_set]
    chars = "".join(characters)
    readings = pinyin(chars, style=Style.NORMAL, heteronym=True, errors="ignore")
    candidates: dict[str, OrderedDict[str, None]] = {}
    h_primary_candidates: OrderedDict[str, None] = OrderedDict()
    h_alternate_candidates: OrderedDict[str, None] = OrderedDict()

    for char, char_readings in zip(chars, readings):
        valid_readings = []
        for reading in char_readings:
            reading = reading.lower()
            if not reading.isascii() or not reading.isalpha():
                continue
            valid_readings.append(reading)
            bucket = candidates.setdefault(reading, OrderedDict())
            bucket.setdefault(char, None)
        if any(reading.startswith("h") for reading in valid_readings):
            if valid_readings and valid_readings[0].startswith("h"):
                h_primary_candidates.setdefault(char, None)
            else:
                h_alternate_candidates.setdefault(char, None)

    # LVGL's K26 IME treats a one-letter input as a match for the first
    # dictionary row with that initial. Add an explicit h row so typing "h"
    # yields candidates from all h syllables instead of relying on "ha".
    h_candidates = OrderedDict(
        list(h_primary_candidates.items()) + list(h_alternate_candidates.items())
    )
    preferred_h = (
        "\u597d\u54c8\u6d77\u6c49\u8fd8\u5f88\u540e\u4f1a\u548c\u7ea2"
        "\u82b1\u8bdd\u9ed1\u9ec4\u6e56\u6b22\u6362\u56de\u6d3b\u706b"
    )
    candidates["h"] = OrderedDict(
        [(char, None) for char in preferred_h if char in h_candidates]
        + list(h_candidates.items())
    )

    # LVGL 8's stock indexer expects every a-z initial to have a row.  The
    # keyboard spelling rules reject i/u/v as standalone input, but keeping
    # empty rows for them preserves the positions of all following initials.
    for initial in "abcdefghijklmnopqrstuvwxyz":
        if not any(reading.startswith(initial) for reading in candidates):
            candidates[initial] = OrderedDict()

    lines = []
    for reading in sorted(candidates):
        text = "".join(candidates[reading])
        lines.append(f"{reading}\t{text}")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    payload = ("\n".join(lines) + "\n").encode("utf-8")
    OUTPUT.write_bytes(payload)
    print(
        f"wrote {OUTPUT} ({len(candidates)} pinyin groups, "
        f"{len(characters)} Hanzi: {len(common)} common + "
        f"{len(characters) - len(common)} GB2312 extras)"
    )


if __name__ == "__main__":
    main()
