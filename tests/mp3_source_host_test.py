"""Decode a real MP3 with the actual firmware wrapper (fixture is not committed).
Run: python tests/mp3_source_host_test.py --fixture /path/to/song.mp3 [--cc tcc]
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--cc", default=str(ROOT / ".tools/host-tcc/tcc/tcc.exe"))
    args = parser.parse_args()
    fixture = args.fixture.resolve()
    with tempfile.TemporaryDirectory(prefix="mp3-source-") as tmp:
        directory = Path(tmp)
        (directory / "esp_attr.h").write_text("#pragma once\n#define EXT_RAM_BSS_ATTR\n")
        (directory / "esp_heap_caps.h").write_text(
            "#pragma once\n#include <stdlib.h>\nextern int fail_mp3_alloc;\n"
            "#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n"
            "#define heap_caps_calloc(n,s,c) (fail_mp3_alloc ? NULL : calloc(n,s))\n"
            "#define heap_caps_free(p) free(p)\n")
        empty, junk, tagged = (directory / name for name in ("empty.mp3", "junk.mp3", "tagged.mp3"))
        empty.write_bytes(b"")
        junk.write_bytes(b"not MPEG audio" * 2000)
        tag_size = 128 * 1024
        encoded = bytes((tag_size >> shift) & 127 for shift in (21, 14, 7, 0))
        with tagged.open("wb") as output, fixture.open("rb") as original:
            output.write(b"ID3\x04\x00\x00" + encoded + bytes(tag_size))
            shutil.copyfileobj(original, output)
        exe = directory / "mp3_test.exe"
        subprocess.run([args.cc, f"-I{directory}", f"-I{ROOT / 'src'}",
                        f"-I{ROOT / 'components/minimp3/include'}",
                        str(ROOT / "src/Mp3Source.c"), str(ROOT / "tests/mp3_source_host_test.c"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(fixture), str(empty), str(junk), str(tagged)], check=True)


if __name__ == "__main__":
    main()
