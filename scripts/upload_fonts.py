"""Upload validated LVGL fonts using the SD uploader's CRC-verified transaction."""
import argparse
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from update_chinese20_font import read_existing_codepoints
from tools.sd_uploader.transport import SerialUploader, UploadError


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--sizes', nargs='+', type=int, choices=(16, 20, 32), default=(16, 20, 32))
    parser.add_argument('--source-dir', type=Path, default=ROOT/'sd_card_files/fonts')
    args = parser.parse_args()
    files = []
    try:
        for size in dict.fromkeys(args.sizes):
            name = f'chinese_{size}.bin'
            source = args.source_dir/name
            if not read_existing_codepoints(source):
                raise ValueError(f'Font has no glyphs: {source}')
            files.append(SimpleNamespace(local_path=source, sd_path=f'/fonts/{name}'))
        with SerialUploader(args.port, log=print) as uploader:
            uploader.upload(files)
    except (OSError, ValueError, UploadError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
