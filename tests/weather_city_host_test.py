"""Compile the official city table and resolve real county names. No network."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools" / "weather-city-music-20261117" / "host-city"


def main():
    bundled = ROOT / ".tools" / "host-tcc" / "tcc" / "tcc.exe"
    cc = shutil.which("cl") or shutil.which("gcc") or shutil.which("cc") or shutil.which("tcc")
    if not cc and bundled.exists():
        cc = str(bundled)
    assert cc, "C compiler required"
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "city.exe"
    sources = [str(ROOT / "src" / "WeatherCity.c"), str(ROOT / "tests" / "weather_city_host_test.c")]
    if Path(cc).stem.lower() == "cl":
        cmd = [cc, "/nologo", "/utf-8", "/I" + str(ROOT / "src"), *sources, "/Fe:" + str(exe), "/Fo" + str(OUT) + "\\"]
    else:
        cmd = [cc, "-I" + str(ROOT / "src"), *sources, "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
