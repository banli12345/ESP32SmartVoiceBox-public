import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import struct
import zlib
from tools.sd_uploader import protocol as p
from tools.sd_uploader import transport as t

class FakeSerial:
    """Emulated peer exercises real packet streaming, retry and commit order."""
    def __init__(self, lose_data_ack=False, reject_finish=False, cancel=None):
        self.port=None; self.baudrate=115200; self.is_open=False
        self.rx=bytearray(); self.manifest=[]; self.parts={}; self.files={}
        self.last_seq=None; self.last_reply=b''; self.requests=[]
        self.lost=False; self.lose_data_ack=lose_data_ack
        self.reject_finish=reject_finish; self.cancel=cancel
        self.finished=set(); self.commit_count=0
    @property
    def in_waiting(self): return len(self.rx)
    def open(self): self.is_open=True
    def close(self): self.is_open=False
    def reset_input_buffer(self): self.rx.clear()
    def read(self, size=1):
        data=bytes(self.rx[:size]); del self.rx[:size]; return data
    def write(self, data):
        if data==b'@SDUP1 HELLO\n':
            self.rx.extend(b'normal boot log\n@SDUP1 READY 1 921600\n')
            return len(data)
        frame=p.pop_frame(bytearray(data)); assert frame
        self.requests.append(frame)
        if frame.opcode==p.ABORT:
            self.parts.clear(); self.rx.extend(p.encode_frame(p.ACK,frame.sequence)); return len(data)
        if frame.sequence==self.last_seq:
            self.rx.extend(self.last_reply); return len(data)
        if frame.opcode==p.BEGIN:
            payload=frame.payload; at=2
            for _ in range(struct.unpack_from('<H',payload)[0]):
                length,size,crc=struct.unpack_from('<HII',payload,at); at+=10
                path=payload[at:at+length].decode(); at+=length
                self.manifest.append((path,size,crc)); self.parts[path]=bytearray()
            assert at==len(payload)
        if frame.opcode==p.DATA:
            index,offset=struct.unpack_from('<HI',frame.payload)
            path=self.manifest[index][0]; assert offset==len(self.parts[path])
            self.parts[path].extend(frame.payload[6:])
        if frame.opcode==p.FINISH:
            index=struct.unpack('<H',frame.payload)[0]; path,size,crc=self.manifest[index]
            assert len(self.parts[path])==size and zlib.crc32(self.parts[path])==crc
            self.finished.add(index)
            self.rx.extend(p.encode_frame(p.PROGRESS,frame.sequence,struct.pack('<I',size)))
        if frame.opcode==p.COMMIT:
            assert len(self.finished)==len(self.manifest)
            self.commit_count+=1; self.files.update({k:bytes(v) for k,v in self.parts.items()})
        self.last_seq=frame.sequence
        if frame.opcode==p.FINISH and self.reject_finish:
            self.last_reply=p.encode_frame(p.ERROR,frame.sequence,b'SD verification checksum mismatch')
        else: self.last_reply=p.encode_frame(p.ACK,frame.sequence)
        if frame.opcode==p.DATA and self.lose_data_ack and not self.lost:
            self.lost=True; return len(data)
        self.rx.extend(self.last_reply)
        if frame.opcode==p.DATA and self.cancel: self.cancel.set()
        return len(data)

class TransportTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.root=Path(self.temp.name)
        self.clock=0
    def tearDown(self): self.temp.cleanup()
    def tick(self): self.clock+=0.02; return self.clock
    def file(self, name, data, target):
        path=self.root/name; path.write_bytes(data)
        return SimpleNamespace(local_path=path,sd_path=target)
    def run_upload(self, board, files, cancel=None):
        events=[]
        uploader=t.SerialUploader('COM_TEST',cancel_event=cancel,progress=events.append)
        with patch.object(t.serial,'Serial',return_value=board), patch.object(t.time,'monotonic',self.tick), patch.object(t.time,'sleep'):
            uploader.upload(files)
        return events
    def test_pair_commit_and_utf8(self):
        video=b'x'*p.CHUNK_SIZE+b'SDU1\x00\xffabc'
        files=[self.file('video.rvf',video,'/videos/视频.rvf'),self.file('audio.wav',b'PCM','/videos/视频.wav')]
        board=FakeSerial(); events=self.run_upload(board,files)
        self.assertEqual(board.files['/videos/视频.rvf'],video)
        self.assertEqual(board.files['/videos/视频.wav'],b'PCM')
        self.assertEqual(board.commit_count,1)
        self.assertTrue(any(e['stage']=='verify' for e in events))
        self.assertEqual(board.requests[-1].opcode,p.EXIT)
        self.assertFalse(board.is_open)
    def test_lost_ack_retries_without_duplicate_bytes(self):
        board=FakeSerial(lose_data_ack=True)
        self.run_upload(board,[self.file('p.bin',b'x'*20000,'/imges/p.bin')])
        self.assertEqual(board.files['/imges/p.bin'],b'x'*20000)
        seqs=[f.sequence for f in board.requests if f.opcode==p.DATA]
        self.assertEqual(seqs[0],seqs[1])
    def test_verification_error_never_commits(self):
        board=FakeSerial(reject_finish=True)
        board.files['/music/a.wav']=b'original'
        with self.assertRaises(t.UploadError):
            self.run_upload(board,[self.file('a.wav',b'new','/music/a.wav')])
        self.assertEqual(board.files['/music/a.wav'],b'original')
        self.assertEqual(board.commit_count,0)
        self.assertFalse(board.parts)
    def test_cancel_after_chunk_keeps_original(self):
        event=threading.Event(); board=FakeSerial(cancel=event)
        board.files['/imges/a.bin']=b'original'
        with self.assertRaises(t.UploadCancelled):
            self.run_upload(board,[self.file('a.bin',b'x'*20000,'/imges/a.bin')],event)
        self.assertEqual(board.files['/imges/a.bin'],b'original')
        self.assertFalse(board.parts)
    def test_empty_lrc(self):
        board=FakeSerial()
        self.run_upload(board,[self.file('a.lrc',b'','/music/a.lrc')])
        self.assertEqual(board.files['/music/a.lrc'],b'')
    def test_duplicate_and_invalid_paths_fail_before_connection(self):
        board=FakeSerial()
        f=self.file('a.bin',b'x','/imges/a.bin')
        with self.assertRaises(t.UploadError): self.run_upload(board,[f,f])
        with self.assertRaises(t.UploadError):
            self.run_upload(board,[self.file('b.bin',b'x','/fonts/b.bin')])
        self.assertFalse(board.requests)
    def test_cancel_before_connect(self):
        event=threading.Event(); event.set(); board=FakeSerial()
        with self.assertRaises(t.UploadCancelled):
            self.run_upload(board,[self.file('a.bin',b'x','/imges/a.bin')],event)
        self.assertFalse(board.requests)

class FramingTests(unittest.TestCase):
    def test_noise_fragmentation_and_bad_crc(self):
        valid=p.encode_frame(p.ACK,4,b'good')
        broken=bytearray(valid); broken[-1]^=1
        buf=bytearray(b'log\n'+broken+valid[:7])
        self.assertIsNone(p.pop_frame(buf))
        buf.extend(valid[7:]); frame=p.pop_frame(buf)
        self.assertEqual(frame,p.Frame(p.ACK,4,b'good'))
        self.assertFalse(buf)
    def test_crc_matches_standard(self): self.assertEqual(zlib.crc32(b'123456789'),0xcbf43926)
    def test_paths(self):
        for path in ('/wallpapers/wallpaper2.bin','/music/晴天.lrc','/imges/照片.bin','/videos/ikun20.wav'):
            self.assertEqual(p.validate_sd_path(path),path)
        for path in ('/music/../a.wav','/music/x/a.wav','/fonts/a.bin','/music/.a.wav','/music/a.wav ', '/music/'+'中'*20+'.wav'):
            with self.assertRaises(ValueError): p.validate_sd_path(path)

if __name__=='__main__': unittest.main()
