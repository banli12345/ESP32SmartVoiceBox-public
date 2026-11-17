import struct
import subprocess
import tempfile
import threading
import unittest
import wave
from pathlib import Path
from unittest.mock import patch
from PIL import Image
from tools.sd_uploader.converters import (MediaJob, find_ffmpeg, predicted_targets,
    plan_jobs, prepare_jobs, copy_lyrics, export_files, run_worker)
from tools.sd_uploader.transport import UploadCancelled

class MediaTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.root=Path(self.temp.name)
        self.out=self.root/'prepared'
    def tearDown(self): self.temp.cleanup()
    def image(self,name='图.png',size=(100,50)):
        path=self.root/name; Image.new('RGBA',size,(255,0,0,255)).save(path); return path
    def ffmpeg(self,*args):
        subprocess.run([find_ffmpeg(),'-hide_banner','-loglevel','error','-y',*args],check=True,
                       creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    def test_wallpaper_header_pixels_and_slot(self):
        files=prepare_jobs([MediaJob('wallpaper',self.image(),3)],self.out)
        self.assertEqual(files[0].sd_path,'/wallpapers/wallpaper3.bin')
        data=files[0].local_path.read_bytes()
        self.assertEqual(struct.unpack_from('<I',data)[0],5|(480<<10)|(480<<21))
        self.assertEqual(len(data),4+480*480*3)
        self.assertEqual(data[4:7],b'\x00\xf8\xff')
    def test_photo_keeps_whole_image_with_black_bars(self):
        files=prepare_jobs([MediaJob('photo',self.image())],self.out)
        data=files[0].local_path.read_bytes()
        self.assertEqual(struct.unpack_from('<I',data)[0],5|(480<<10)|(450<<21))
        self.assertEqual(len(data),4+480*450*3)
        self.assertEqual(data[4:7],b'\x00\x00\xff')
        center=4+(225*480+240)*3
        self.assertEqual(data[center:center+3],b'\x00\xf8\xff')
    def test_png_alpha_is_preserved(self):
        source=self.root/'transparent.png'; Image.new('RGBA',(480,480),(0,255,0,42)).save(source)
        files=prepare_jobs([MediaJob('wallpaper',source)],self.out)
        self.assertEqual(files[0].local_path.read_bytes()[4:7],b'\xe0\x07\x2a')
    def test_lyrics_preserve_original_bytes(self):
        text='[00:01.23]中文歌词\r\n'
        for encoding in ('utf-8-sig','utf-16','gb18030'):
            source=self.root/'歌词.lrc'; source.write_bytes(text.encode(encoding))
            target=self.out/'music/歌词.lrc'; copy_lyrics(source,target)
            self.assertEqual(target.read_bytes(),source.read_bytes())
    def test_standalone_lrc_bypasses_media_converter(self):
        source=self.root/'歌词.lrc'
        for raw in (b'',b'\xef\xbb\xbf[00:01.23]first\r\n[00:02.00]second\n',b'\xff\x00\xfe'):
            with self.subTest(raw=raw):
                source.write_bytes(raw)
                events=[]
                with patch('tools.sd_uploader.converters.run_worker',side_effect=AssertionError('LRC must not invoke a converter')):
                    files=prepare_jobs([MediaJob('music',source)],self.out,progress=events.append)
                self.assertEqual([file.sd_path for file in files],['/music/歌词.lrc'])
                self.assertEqual(files[0].local_path.read_bytes(),raw)
                self.assertEqual(source.read_bytes(),raw)
                self.assertEqual(events[0]['message'],'正在准备歌词 歌词.lrc')
    def test_mp3_canonical_wav_and_auto_lrc(self):
        source=self.root/'音乐.mp3'
        self.ffmpeg('-f','lavfi','-i','sine=frequency=440:duration=0.2','-ac','2',str(source))
        lyric=source.with_suffix('.lrc'); lyric.write_bytes('[00:00.00]测试'.encode('gb18030'))
        jobs=plan_jobs([MediaJob('music',lyric),MediaJob('music',source)])
        self.assertEqual(len(jobs),1)
        files=prepare_jobs(jobs,self.out)
        self.assertEqual([f.sd_path for f in files],['/music/音乐.wav','/music/音乐.lrc'])
        self.assertEqual(files[1].local_path.read_bytes(),lyric.read_bytes())
        data=files[0].local_path.read_bytes()
        self.assertEqual(data[12:16],b'fmt '); self.assertEqual(data[36:40],b'data')
        self.assertEqual(struct.unpack_from('<I',data,40)[0],len(data)-44)
        with wave.open(str(files[0].local_path)) as wav:
            self.assertEqual((wav.getframerate(),wav.getnchannels(),wav.getsampwidth()),(44100,2,2))
            self.assertGreater(wav.getnframes(),0)
    def test_video_and_sound_share_duration(self):
        source=self.root/'video.mp4'
        self.ffmpeg('-f','lavfi','-i','color=red:s=32x24:r=20:d=0.4',
                    '-f','lavfi','-i','sine=frequency=440:duration=0.4',
                    '-c:v','libx264','-pix_fmt','yuv420p','-c:a','aac','-shortest',str(source))
        files=prepare_jobs([MediaJob('video',source)],self.out)
        self.assertEqual([f.sd_path for f in files],['/videos/video.rvf','/videos/video.wav'])
        header=struct.unpack('<4sHHHHIIIII',files[0].local_path.read_bytes()[:32])
        self.assertEqual(header[:7],(b'RVF1',1,32,320,240,20000,8))
        with wave.open(str(files[1].local_path)) as wav:
            self.assertEqual(wav.getnframes(),17640)
    def test_silent_video_has_no_companion(self):
        source=self.root/'silent.mp4'
        self.ffmpeg('-f','lavfi','-i','color=blue:s=32x24:r=20:d=0.2','-c:v','libx264',str(source))
        files=prepare_jobs([MediaJob('video',source)],self.out)
        self.assertEqual(len(files),1)
    def test_duplicate_wallpaper_and_long_chinese_name(self):
        source=self.image()
        with self.assertRaises(ValueError): plan_jobs([MediaJob('wallpaper',source),MediaJob('wallpaper',source)])
        long=self.image('中'*20+'.png')
        with self.assertRaises(ValueError): predicted_targets(MediaJob('photo',long))
    def test_invalid_names_and_wrong_types(self):
        reserved=self.image('CON.png')
        with self.assertRaises(ValueError): predicted_targets(MediaJob('photo',reserved))
        with self.assertRaises(ValueError): predicted_targets(MediaJob('music',self.image()))
    def test_cancel_before_conversion(self):
        event=threading.Event(); event.set()
        with self.assertRaises(UploadCancelled): prepare_jobs([MediaJob('photo',self.image())],self.out,event)
        self.assertFalse(self.out.exists())
    def test_cancel_live_ffmpeg_worker(self):
        source=self.root/'long.mp4'
        self.ffmpeg('-f','lavfi','-i','color=blue:s=32x24:r=20:d=5','-c:v','libx264',str(source))
        target=self.root/'cancel.rvf'; event=threading.Event()
        timer=threading.Timer(0.01,event.set); timer.start()
        try:
            with self.assertRaises(UploadCancelled): run_worker('video',source,target,20,event)
        finally: timer.cancel()
    def test_local_export_keeps_previous_files_on_failure(self):
        files=prepare_jobs([MediaJob('photo',self.image())],self.out)
        destination=self.root/'export'; (destination/'imges').mkdir(parents=True)
        target=destination/'imges/图.bin'; target.mkdir()
        with self.assertRaises(ValueError): export_files(files,destination)
        self.assertTrue(target.is_dir())
        self.assertFalse(list(destination.rglob('*.new')))
        target.rmdir(); target.write_bytes(b'old')
        export_files(files,destination)
        self.assertEqual(target.read_bytes(),files[0].local_path.read_bytes())

if __name__=='__main__': unittest.main()
