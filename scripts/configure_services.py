"""Generate one-shot weather/life and AI SD JSON imports from private services.

The input is a private JSON file. This tool validates it completely, then
writes the weather and AI runtime import documents. It never
prints secret values, never talks to a device or the network, and never
invokes Git.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ConfigError(Exception):
    """A sanitized configuration failure. The message must not contain secrets."""


def _is_version_one(value) -> bool:
    return type(value) is int and value == 1


def _graphic_ascii(value, minimum: int, maximum: int) -> bool:
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        return False
    return all(33 <= ord(ch) <= 126 for ch in value)


def _contains_placeholder(value) -> bool:
    if isinstance(value, str):
        return "YOUR_" in value
    if isinstance(value, dict):
        return any(
            (isinstance(key, str) and "YOUR_" in key) or _contains_placeholder(item)
            for key, item in value.items()
        )
    if isinstance(value, list):
        return any(_contains_placeholder(item) for item in value)
    return False


def valid_api_base(value) -> bool:
    """Match AiConfig::valid for api_base (length, HTTPS, host, forbidden characters)."""
    if not isinstance(value, str) or not 9 <= len(value) <= 191:
        return False
    if not value.startswith("https://"):
        return False
    host_end = len(value)
    for index, ch in enumerate(value[8:]):
        if ch in "/:":
            host_end = index
            break
    if host_end == 0:
        return False
    for ch in value:
        code = ord(ch)
        if code <= 32 or code >= 127 or ch in "@?#\\":
            return False
    return True


def valid_model(value) -> bool:
    """Match AiConfig::valid: 1..63 ASCII letters, digits, or - _ . / :."""
    if not isinstance(value, str) or not 1 <= len(value) <= 63:
        return False
    for ch in value:
        code = ord(ch)
        alnum = 48 <= code <= 57 or 65 <= code <= 90 or 97 <= code <= 122
        if not (alnum or ch in "-_./:"):
            return False
    return True


DEFAULT_ENDPOINTS = {
    "now_url": "http://api.seniverse.com/v3/weather/now.json",
    "daily_url": "http://api.seniverse.com/v3/weather/daily.json",
    "life_url": "https://api.seniverse.com/v3/life/suggestion.json",
}


def valid_weather_url(value) -> bool:
    from urllib.parse import urlsplit
    if not isinstance(value, str) or not 8 <= len(value) <= 191:
        return False
    if not value.startswith(("http://", "https://")):
        return False
    if any(ord(c) <= 32 or ord(c) >= 127 or c in "@?#\\" for c in value):
        return False
    try:
        parsed = urlsplit(value)
        if not parsed.hostname or (parsed.port is not None and not 1 <= parsed.port <= 65535):
            return False
        if not any(c.isalnum() for c in parsed.hostname) or any(
            not (c.isascii() and (c.isalnum() or c in '.-')) for c in parsed.hostname
        ):
            return False
        if parsed.netloc.endswith(":"):
            return False
    except ValueError:
        return False
    return True


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigError("Duplicate JSON field")
        result[key] = value
    return result


def load_services(path: Path) -> tuple[dict, dict]:
    try:
        text = path.read_text(encoding="utf-8-sig")
        data = json.loads(text, object_pairs_hook=unique_object)
    except (OSError, UnicodeError) as exc:
        raise ConfigError("Cannot read the UTF-8 services file") from exc
    except json.JSONDecodeError as exc:
        raise ConfigError("Invalid JSON") from exc
    if _contains_placeholder(data):
        raise ConfigError("Unresolved YOUR_ placeholder")
    if not isinstance(data, dict) or not _is_version_one(data.get("version")):
        raise ConfigError("Services document must be version 1")
    weather, ai = data.get("weather"), data.get("ai")
    if not isinstance(weather, dict) or not isinstance(ai, dict):
        raise ConfigError("Services document needs weather and ai objects")
    if not _graphic_ascii(weather.get("api_key"), 1, 95):
        raise ConfigError("Weather API key is invalid")
    exported_weather = {"version": 1, "api_key": weather["api_key"]}
    for name, default in DEFAULT_ENDPOINTS.items():
        value = weather.get(name, default)
        if not valid_weather_url(value):
            raise ConfigError("Weather endpoint is invalid")
        exported_weather[name] = value
    life_key = weather.get("life_api_key", weather["api_key"])
    if not _graphic_ascii(life_key, 1, 95):
        raise ConfigError("Life API key is invalid")
    exported_weather["life_api_key"] = life_key
    if (not _is_version_one(ai.get("version")) or not valid_api_base(ai.get("api_base"))
        or not _graphic_ascii(ai.get("api_key"), 8, 255)
        or not valid_model(ai.get("model")) or type(ai.get("speak")) is not bool):
        raise ConfigError("AI configuration is invalid")
    exported_ai = {name: ai[name] for name in ("version", "api_base", "api_key", "model", "speak")}
    for payload in (exported_weather, exported_ai):
        if len(render_json(payload).encode("utf8")) > 2048:
            raise ConfigError("Import configuration exceeds 2048 bytes")
    return exported_weather, exported_ai


def render_json(payload: dict) -> str:
    return json.dumps(payload, ensure_ascii=True, indent=2) + "\n"


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.{os.getpid()}.tmp"
    try:
        temporary.write_bytes(text.encode("utf-8"))
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            try:
                temporary.unlink()
            except OSError:
                pass


def configure(source: Path, weather_output: Path, ai_output: Path) -> tuple[Path, Path]:
    paths = [path.resolve() for path in (source, weather_output, ai_output)]
    if len(set(paths)) != 3:
        raise ConfigError("Input and output paths must be distinct")
    for path in (weather_output, ai_output):
        if path.exists() and not path.is_file():
            raise ConfigError("Output path is not a file")
    weather, ai = load_services(source)
    weather_text, ai_text = render_json(weather), render_json(ai)
    atomic_write(weather_output, weather_text)
    atomic_write(ai_output, ai_text)
    return weather_output, ai_output


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Private services JSON with your own values")
    parser.add_argument("--weather-output", type=Path, default=ROOT / "config" / "weather.json")
    parser.add_argument("--ai-output", type=Path, default=ROOT / "config" / "ai.json")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        weather, ai = configure(args.input, args.weather_output, args.ai_output)
    except ConfigError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    except OSError:
        print("Cannot write configuration outputs; check paths and permissions", file=sys.stderr)
        return 1
    print(weather)
    print(ai)
    print("Copy to SD /config/weather.json and /config/ai.json, then restart. No rebuild after the runtime-config firmware is installed.")
    print("Each valid import is saved to NVS and deleted only after successful commit. Keep your editable master file on the computer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
