"""Child process for cancellable FFmpeg conversion, also used by the EXE."""
import subprocess
import sys
import wave
from pathlib import Path

def main(argv=None):
    kind,source,target,fps,ffmpeg = argv if argv is not None else sys.argv[1:]
    source,target=Path(source),Path(target)
    # Hide every FFmpeg process, including the existing video converter's probes.
    run=subprocess.run
    def hidden_run(*args,**kwargs):
        if sys.platform=='win32': kwargs.setdefault('creationflags',subprocess.CREATE_NO_WINDOW)
        return run(*args,**kwargs)
    if kind=='video':
        from scripts import convert_video as video
        video.find_ffmpeg=lambda:ffmpeg
        video.subprocess.run=hidden_run
        video.convert(source,target,320,240,float(fps))
    elif kind=='music':
        pcm=target.with_suffix('.pcm.new')
        try:
            hidden_run([ffmpeg,'-hide_banner','-loglevel','error','-y','-i',str(source),
                        '-map','0:a:0','-vn','-ar','44100','-ac','2','-c:a','pcm_s16le',
                        '-f','s16le',str(pcm)],check=True)
            if not pcm.stat().st_size or pcm.stat().st_size%4:
                raise ValueError('音频没有有效 PCM 样本')
            with pcm.open('rb') as data, wave.open(str(target),'wb') as output:
                output.setnchannels(2); output.setsampwidth(2); output.setframerate(44100)
                while chunk:=data.read(1024*1024): output.writeframesraw(chunk)
        finally: pcm.unlink(missing_ok=True)
    else: raise ValueError('Unknown conversion kind')
    return 0

if __name__=='__main__': raise SystemExit(main())
