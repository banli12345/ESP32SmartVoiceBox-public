"""Import a PRIVATE JSON AI configuration over the existing UART uploader.

The receiving firmware validates it, commits it to NVS, and removes the SD
import file on the following boot. This tool never prints the key.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.sd_uploader.transport import SerialUploader


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="Local private JSON file; keep outside source control")
    parser.add_argument("--port", default="COM79")
    args = parser.parse_args()
    source = args.config.resolve()
    if source.stat().st_size > 2048:
        parser.error("Configuration exceeds 2048 bytes")
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    if data.get("version") != 1 or not all(isinstance(data.get(k), str) and data[k] for k in ("api_base", "api_key", "model")):
        parser.error("Require version 1, api_base, api_key and model")
    if not data["api_base"].startswith("https://"):
        parser.error("HTTPS API base required")
    with SerialUploader(args.port, log=lambda text: print(text, flush=True)) as uploader:
        uploader.upload([SimpleNamespace(local_path=source, sd_path="/config/ai.json")])
    print("AI configuration transferred. The device imports it into NVS on boot; check the AI page.")


if __name__ == "__main__":
    main()
