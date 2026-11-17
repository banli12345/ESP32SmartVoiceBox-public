from __future__ import annotations
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from PIL import Image, ImageOps
from scripts.convert_app_icons import encode_lvgl_bin
from .protocol import validate_sd_path, MAX_FILES
from .transport import UploadCancelled

ROOT = Path(__file__).resolve().parents[2]
KINDS = {'wallpaper': '壁纸', 'music': '音乐和歌词', 'photo': '照片', 'video': '视频和音轨'}

@dataclass(frozen=True)
class PreparedFile:
    local_path: Path
    sd_path: str

@dataclass(frozen=True)
class MediaJob:
    kind: str
    source: Path
    wallpaper_slot: int = 1
    fps: int = 20

def application_dir() -> Path:
    return Path(sys.executable).parent if getattr(sys, 'frozen', False) else ROOT

def find_ffmpeg() -> str:
    candidates = [Path(getattr(sys, '_MEIPASS', ROOT)) / 'ffmpeg.exe',
                  Path(getattr(sys, '_MEIPASS', ROOT)) / 'ffmpeg-win-x86_64-v7.1.exe',
                  ROOT / '.tools/python-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe']
    for path in candidates:
        if path.is_file():
            return str(path)
    if executable := shutil.which('ffmpeg'):
        return executable
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError as exc:
        raise RuntimeError('找不到 FFmpeg，请安装 imageio-ffmpeg 或使用打包版上位机') from exc

def validate_name(stem: str):
    reserved = {'CON','PRN','AUX','NUL', *(f'COM{i}' for i in range(1,10)), *(f'LPT{i}' for i in range(1,10))}
    if not stem or stem.endswith(('.', ' ')) or stem.split('.')[0].upper() in reserved:
        raise ValueError('文件名为空或为 Windows 保留名称')
    validate_sd_path(f'/music/{stem}.wav')

def predicted_targets(job: MediaJob) -> list[str]:
    source = Path(job.source)
    if not source.is_file():
        raise ValueError(f'素材不存在：{source}')
    ext = source.suffix.lower()
    stem = source.stem
    if job.kind == 'wallpaper':
        if ext not in {'.png','.jpg','.jpeg'} or job.wallpaper_slot not in (1,2,3):
            raise ValueError('请选择 PNG/JPG 壁纸和位置 1、2 或 3')
        return [f'/wallpapers/wallpaper{job.wallpaper_slot}.bin']
    validate_name(stem)
    if job.kind == 'photo' and ext in {'.png','.jpg','.jpeg'}:
        return [validate_sd_path(f'/imges/{stem}.bin')]
    if job.kind == 'music' and ext == '.lrc':
        return [validate_sd_path(f'/music/{stem}.lrc')]
    if job.kind == 'music' and ext == '.mp3':
        targets = [validate_sd_path(f'/music/{stem}.wav')]
        if source.with_suffix('.lrc').is_file():
            targets.append(validate_sd_path(f'/music/{stem}.lrc'))
        return targets
    if job.kind == 'video' and ext == '.mp4' and job.fps in (10,15,20,25,30):
        return [validate_sd_path(f'/videos/{stem}.rvf'),validate_sd_path(f'/videos/{stem}.wav')]
    raise ValueError('素材类型不匹配，请选择对应类别的文件')

def plan_jobs(jobs: list[MediaJob]) -> list[MediaJob]:
    seen = {}
    planned = []
    # An explicitly selected LRC already paired with its selected MP3 is useful
    # input, not a duplicate upload. Prefer the MP3 job's automatic companion.
    auto_lyrics = {str(Path(j.source).with_suffix('.lrc').resolve()).casefold()
                   for j in jobs if j.kind=='music' and Path(j.source).suffix.lower()=='.mp3'}
    for job in jobs:
        if job.kind=='music' and Path(job.source).suffix.lower()=='.lrc' and str(Path(job.source).resolve()).casefold() in auto_lyrics:
            continue
        targets = predicted_targets(job)
        for target in targets:
            key = target.casefold()
            if key in seen:
                raise ValueError(f'队列目标重复：{target}。请更换文件名或壁纸位置')
            seen[key] = job
        planned.append(job)
    if not planned:
        raise ValueError('请先添加素材')
    if len(seen)>MAX_FILES:
        raise ValueError('每批最多 32 个输出文件，视频会占用画面和音轨两个位置')
    return planned

def copy_lyrics(source: Path, target: Path):
    """Prepare LRC without decoding or changing any bytes."""
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target)

def _check_cancel(event):
    if event and event.is_set(): raise UploadCancelled('素材转换已取消')

def _kill_worker(process):
    if process.poll() is not None: return
    if os.name == 'nt':
        subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],
                       stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                       creationflags=subprocess.CREATE_NO_WINDOW,check=False)
    else:
        import signal
        os.killpg(process.pid,signal.SIGKILL)
    process.wait(timeout=10)

def run_worker(kind, source, target, fps, event):
    ffmpeg = find_ffmpeg()
    if getattr(sys,'frozen',False):
        command = [sys.executable,'--convert-job',kind,str(source),str(target),str(fps),ffmpeg]
    else:
        command = [sys.executable,'-m','tools.sd_uploader.worker',kind,str(source),str(target),str(fps),ffmpeg]
    flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name=='nt' else dict(start_new_session=True)
    with tempfile.TemporaryFile() as log:
        env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
        process = subprocess.Popen(command,stdout=log,stderr=log,cwd=ROOT if not getattr(sys,'frozen',False) else None,env=env,**flags)
        try:
            while process.poll() is None:
                _check_cancel(event)
                time.sleep(0.05)
            _check_cancel(event)
            if process.returncode:
                log.seek(0); text=log.read().decode('utf-8','replace')[-2500:]
                error_file=target.with_name(target.name+'.error.txt')
                if error_file.is_file():
                    text=error_file.read_text(encoding='utf-8')[-2500:]
                raise RuntimeError(f'素材转换失败：{source.name}\n{text}')
        except BaseException:
            _kill_worker(process)
            raise

def prepare_jobs(jobs: list[MediaJob], output_root: Path,
                 cancel_event: threading.Event | None = None, progress=None) -> list[PreparedFile]:
    jobs = plan_jobs(jobs)
    result = []
    progress = progress or (lambda event: None)
    for index, job in enumerate(jobs):
        _check_cancel(cancel_event)
        source = Path(job.source)
        targets = predicted_targets(job)
        message=f'正在准备歌词 {source.name}' if job.kind=='music' and source.suffix.lower()=='.lrc' else f'正在转换 {source.name}'
        progress(dict(stage='convert',path=str(source),done=index,total=len(jobs),message=message))
        target = Path(output_root)/targets[0].lstrip('/')
        target.parent.mkdir(parents=True,exist_ok=True)
        if job.kind in ('wallpaper','photo'):
            with Image.open(source) as image:
                image = ImageOps.exif_transpose(image)
                encode_lvgl_bin(image,target,480,480 if job.kind=='wallpaper' else 450,
                                preserve_aspect=job.kind=='photo')
        elif job.kind=='music' and source.suffix.lower()=='.lrc':
            copy_lyrics(source,target)
        else:
            run_worker(job.kind,source,target,job.fps,cancel_event)
        _check_cancel(cancel_event)
        result.append(PreparedFile(target,targets[0]))
        if job.kind=='music' and source.suffix.lower()=='.mp3' and len(targets)>1:
            lyric_target=Path(output_root)/targets[1].lstrip('/')
            copy_lyrics(source.with_suffix('.lrc'),lyric_target)
            result.append(PreparedFile(lyric_target,targets[1]))
        if job.kind=='video' and target.with_suffix('.wav').is_file():
            result.append(PreparedFile(target.with_suffix('.wav'),targets[1]))
        progress(dict(stage='convert',path=str(source),done=index+1,total=len(jobs)))
    return result

def export_files(files: list[PreparedFile], directory: Path):
    """Replace a local export batch with rollback if any installation fails."""
    import uuid
    replacements = []
    try:
        for f in files:
            target=Path(directory)/f.sd_path.lstrip('/')
            target.parent.mkdir(parents=True,exist_ok=True)
            new=target.with_name(target.name+'.'+uuid.uuid4().hex+'.new')
            shutil.copyfile(f.local_path,new)
            replacements.append((new,target))
        # Generic local batch rollback uses the same filesystem rename rules.
        backups=[]; installed=[]
        try:
            for new,target in replacements:
                if target.exists():
                    if not target.is_file(): raise ValueError(f'目标是目录：{target}')
                    backup=target.with_name(target.name+'.'+uuid.uuid4().hex+'.backup')
                    target.replace(backup); backups.append((backup,target))
                new.replace(target); installed.append(target)
        except BaseException:
            for target in reversed(installed): target.unlink(missing_ok=True)
            for backup,target in reversed(backups): backup.replace(target)
            raise
        for backup,_ in backups: backup.unlink()
    finally:
        for new,_ in replacements: new.unlink(missing_ok=True)
