"""Build a Windows desktop folder with bundled Python, Pillow and FFmpeg."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.sd_uploader.converters import find_ffmpeg

def main():
    work=ROOT/'.tools/pyinstaller-sd-uploader'
    work.mkdir(parents=True,exist_ok=True)
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--windowed',
             '--name','SDMediaUploader','--distpath',str(ROOT/'dist'),
             '--workpath',str(work/'build'),'--specpath',str(work),
             '--paths',str(ROOT),'--add-binary',find_ffmpeg()+';.',
             '--hidden-import','scripts.convert_video','--hidden-import','scripts.convert_app_icons',
             '--exclude-module','numpy','--exclude-module','matplotlib','--exclude-module','pytest',
             '--exclude-module','IPython',str(ROOT/'tools/sd_uploader/entry.py')]
    subprocess.run(command,cwd=ROOT,check=True)
    print(ROOT/'dist/SDMediaUploader/SDMediaUploader.exe')
if __name__=='__main__': main()
