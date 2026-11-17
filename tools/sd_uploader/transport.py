"""Stop-and-wait UART uploader with readback CRC and a batch commit."""
from __future__ import annotations
import struct
import threading
import time
import zlib
from pathlib import Path
from typing import TYPE_CHECKING, Callable, Sequence
import serial
from . import protocol as p
if TYPE_CHECKING:
    from .converters import PreparedFile

class UploadError(Exception):
    pass

class UploadCancelled(Exception):
    pass

class SerialUploader:
    def __init__(self, port: str, baudrate: int = 921600,
                 cancel_event: threading.Event | None = None,
                 progress: Callable[[dict], None] | None = None,
                 log: Callable[[str], None] | None = None):
        if baudrate != 921600:
            raise UploadError('当前固件使用 921600 波特率上传')
        self.port, self.baudrate = port, baudrate
        self.cancel_event = cancel_event or threading.Event()
        self.progress = progress or (lambda event: None)
        self.log = log or (lambda message: None)
        self._serial = None
        self._rx = bytearray()
        self._sequence = 1
        self._ready = False
        self._committed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self._serial:
            self._serial.close()
            self._serial = None
        self._ready = False

    def _check_cancel(self):
        if self.cancel_event.is_set():
            raise UploadCancelled('已取消，未提交的素材不会替换原文件')

    def _event(self, stage, path='', done=0, total=0, **extra):
        self.progress(dict(stage=stage, path=path, done=done, total=total, **extra))

    def _connect(self):
        self._event('connect', message='正在连接设备并进入上传模式（设备将重启）')
        self._serial = serial.Serial(port=None, baudrate=115200,
                                     timeout=0.1, write_timeout=5)
        self._serial.port = self.port
        self._serial.dtr = False
        self._serial.rts = False
        try:
            self._serial.open()
        except (OSError, serial.SerialException) as exc:
            raise UploadError(f'无法打开 {self.port}，请检查 USB 连接并关闭串口监视器：{exc}') from exc
        self._serial.reset_input_buffer()
        deadline, next_hello = time.monotonic() + 75, 0.0
        line_buffer = bytearray()
        while time.monotonic() < deadline:
            self._check_cancel()
            now = time.monotonic()
            if now >= next_hello:
                self._serial.write(b'@SDUP1 HELLO\n')
                next_hello = now + 1
            data = self._serial.read(max(1, min(self._serial.in_waiting, 4096)))
            line_buffer.extend(data)
            while b'\n' in line_buffer:
                line, _, rest = line_buffer.partition(b'\n')
                line_buffer = bytearray(rest)
                line = line.rstrip(b'\r')
                if line.startswith(b'@SDUP1 ERROR '):
                    raise UploadError(line[13:].decode('utf-8', 'replace'))
                if line == b'@SDUP1 READY 1 921600':
                    self._serial.baudrate = self.baudrate
                    self._serial.reset_input_buffer()
                    self._rx.clear()
                    time.sleep(0.15)
                    self._ready = True
                    self._exchange(p.PING)
                    self.log(f'已连接 {self.port}，上传波特率 {self.baudrate}')
                    return
            if len(line_buffer) > 4096:
                del line_buffer[:-128]
        raise UploadError('设备未响应。请检查 SD 卡和串口，并先烧录支持串口上传的新固件')

    def _receive(self, deadline, check_cancel=True):
        while time.monotonic() < deadline:
            if check_cancel:
                self._check_cancel()
            frame = p.pop_frame(self._rx)
            if frame:
                return frame
            data = self._serial.read(max(1, min(self._serial.in_waiting, 4096)))
            self._rx.extend(data)
        raise TimeoutError('等待设备应答超时')

    def _exchange(self, opcode, payload=b'', timeout=6, on_progress=None,
                  check_cancel=True, maximum_time=None):
        seq = self._sequence
        packet = p.encode_frame(opcode, seq, payload)
        hard_deadline = time.monotonic() + (maximum_time or timeout * 3 + 5)
        for attempt in range(3):
            if check_cancel:
                self._check_cancel()
            self._serial.write(packet)
            deadline = min(time.monotonic() + timeout, hard_deadline)
            try:
                while True:
                    frame = self._receive(deadline, check_cancel)
                    if frame.sequence != seq:
                        continue
                    if frame.opcode == p.PROGRESS:
                        if on_progress:
                            on_progress(frame.payload)
                        deadline = min(time.monotonic() + timeout, hard_deadline)
                        continue
                    if frame.opcode == p.ERROR:
                        message = frame.payload.decode('utf-8', 'replace')
                        if message.startswith('retry:'):
                            break
                        raise UploadError(f'设备拒绝上传：{message}')
                    if frame.opcode == p.ACK:
                        self._sequence += 1
                        return
            except TimeoutError:
                pass
            self.log(f'应答丢失或校验失败，正在重试（{attempt + 1}/3）')
        if opcode == p.COMMIT:
            raise UploadError('提交确认超时，素材可能已保存；请重连后检查，勿按未上传处理')
        raise UploadError('串口应答超时，请检查连接；原文件尚未被本批替换')

    def _manifest(self, files):
        if not files or len(files) > p.MAX_FILES:
            raise UploadError('一批需包含 1 至 32 个转换后的文件')
        rows, seen, items = [], set(), []
        for f in files:
            self._check_cancel()
            try:
                target = p.validate_sd_path(f.sd_path)
            except ValueError as exc:
                raise UploadError(str(exc)) from exc
            if target.casefold() in seen:
                raise UploadError(f'队列中有重复的 SD 文件：{target}')
            seen.add(target.casefold())
            source = Path(f.local_path)
            if not source.is_file():
                raise UploadError(f'转换后的文件不存在：{source}')
            size = source.stat().st_size
            if size > 0x7fffffff:
                raise UploadError('单个文件不能超过 2 GiB')
            crc = 0
            with source.open('rb') as stream:
                while data := stream.read(1024 * 1024):
                    self._check_cancel()
                    crc = zlib.crc32(data, crc)
            encoded = target.encode('utf-8')
            rows.append(struct.pack('<HII', len(encoded), size, crc) + encoded)
            items.append((source, target, size))
        return struct.pack('<H', len(rows)) + b''.join(rows), items

    def _abort(self):
        if not self._ready or not self._serial or not self._serial.is_open:
            return
        try:
            # ABORT uses sequence zero and is accepted even after a lost ACK.
            self._serial.write(p.encode_frame(p.ABORT, 0))
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if self._receive(deadline, False).sequence == 0:
                    break
        except (OSError, serial.SerialException, TimeoutError):
            pass

    def upload(self, files: Sequence['PreparedFile']) -> None:
        self._sequence, self._committed = 1, False
        try:
            manifest, items = self._manifest(files)
            self._connect()
            self._exchange(p.BEGIN, manifest)
            aggregate_total, aggregate_done = sum(item[2] for item in items), 0
            for index, (source, target, size) in enumerate(items):
                self.log(f'上传 {target}（{size:,} 字节）')
                self._event('upload', target, 0, size,
                            aggregate_done=aggregate_done, aggregate_total=aggregate_total)
                offset = 0
                with source.open('rb') as stream:
                    while chunk := stream.read(p.CHUNK_SIZE):
                        self._exchange(p.DATA, struct.pack('<HI', index, offset) + chunk)
                        offset += len(chunk)
                        self._event('upload', target, offset, size,
                                    aggregate_done=aggregate_done + offset,
                                    aggregate_total=aggregate_total)
                self._event('verify', target, 0, size, message='正在从 SD 读回校验')
                def verified(payload):
                    if len(payload) == 4:
                        self._event('verify', target, struct.unpack('<I', payload)[0], size)
                self._exchange(p.FINISH, struct.pack('<H', index), on_progress=verified,
                               maximum_time=max(30, size / 20000 + 30))
                aggregate_done += size
                self._event('verify', target, size, size)
            self._check_cancel()
            self._event('commit', message='正在提交整批素材，请保持连接')
            # Commit is the irreversible boundary; finish it even if Cancel is
            # pressed after it begins, and report its actual outcome.
            self._exchange(p.COMMIT, timeout=15, check_cancel=False)
            self._committed = True
            try:
                self._exchange(p.EXIT, timeout=3, check_cancel=False)
            except UploadError:
                self.log('素材已提交，设备重启确认未收到；必要时手动重启设备')
            self.log('全部素材已校验并保存到 SD 卡，设备正在重启加载')
        except UploadCancelled:
            self._abort()
            raise
        except (OSError, serial.SerialException) as exc:
            self._abort()
            if self._committed:
                self.log('素材已提交，串口在设备重启时断开')
            else:
                raise UploadError(f'串口连接中断：{exc}') from exc
        except Exception:
            self._abort()
            raise
        finally:
            self.close()
