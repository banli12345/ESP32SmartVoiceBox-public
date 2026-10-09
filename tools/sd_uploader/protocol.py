"""SDUP/1 framing shared with src/serial_upload_protocol.h."""
from __future__ import annotations
import struct
import zlib
from dataclasses import dataclass

MAGIC = b'SDU1'
HEADER = struct.Struct('<4sBBHI')
CRC = struct.Struct('<I')
CHUNK_SIZE = 16384
MAX_PAYLOAD = CHUNK_SIZE + 6
MAX_FILES = 32
PATH_MAX = 63
PING, BEGIN, DATA, FINISH, COMMIT, EXIT, ABORT = range(1, 8)
ACK, ERROR, PROGRESS = 128, 129, 130

@dataclass(frozen=True)
class Frame:
    opcode: int
    sequence: int
    payload: bytes

def encode_frame(opcode: int, sequence: int, payload: bytes = b'') -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('串口数据包过大')
    header = HEADER.pack(MAGIC, opcode, 0, len(payload), sequence)
    return header + payload + CRC.pack(zlib.crc32(header[4:] + payload))

def pop_frame(buffer: bytearray) -> Frame | None:
    """Consume one valid frame, ignoring boot logs and corrupted packets."""
    while True:
        at = buffer.find(MAGIC)
        if at < 0:
            if len(buffer) > 3:
                del buffer[:-3]
            return None
        if at:
            del buffer[:at]
        if len(buffer) < HEADER.size:
            return None
        _, opcode, reserved, length, sequence = HEADER.unpack_from(buffer)
        if reserved or length > MAX_PAYLOAD:
            del buffer[0]
            continue
        total = HEADER.size + length + CRC.size
        if len(buffer) < total:
            return None
        expected = CRC.unpack_from(buffer, total - 4)[0]
        if zlib.crc32(buffer[4:total - 4]) != expected:
            del buffer[0]
            continue
        payload = bytes(buffer[HEADER.size:total - 4])
        del buffer[:total]
        return Frame(opcode, sequence, payload)

def validate_sd_path(path: str) -> str:
    if not isinstance(path, str) or len(path.encode('utf-8')) > PATH_MAX:
        raise ValueError('SD 文件路径过长，中文文件名请缩短')
    pieces = path.split('/')
    if len(pieces) != 3 or pieces[0] or not pieces[2]:
        raise ValueError('SD 目标必须是素材目录中的文件')
    directory, name = pieces[1:]
    if (name.startswith('.') or '..' in name or name.endswith(('.', ' ')) or
        any(ord(c) < 32 or ord(c) == 127 or c in '<>:"\\|?*' for c in name)):
        raise ValueError('文件名包含不支持的字符')
    stem, dot, ext = name.rpartition('.')
    allowed = {'music': {'wav', 'lrc'}, 'imges': {'bin'}, 'videos': {'rvf', 'wav'}}
    if directory == 'wallpapers':
        if name not in {'wallpaper1.bin', 'wallpaper2.bin', 'wallpaper3.bin'}:
            raise ValueError('壁纸必须选择现有的三个位置')
    elif not dot or not stem or ext not in allowed.get(directory, set()):
        raise ValueError('文件类型与 SD 目录不匹配')
    return path
