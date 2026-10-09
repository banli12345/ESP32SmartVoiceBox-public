#!/usr/bin/env python3
"""Convert a video to the RGB565 RVF format streamed by the ESP32 UI.

The RVF output is a 32-byte little-endian header followed by tightly packed
RGB565 little-endian frames. When the source has an audio stream, a companion
WAV is written beside the RVF using the same directory and file stem. The WAV
is 44100 Hz stereo signed PCM16. Its length is the RVF duration
``frame_count * 1000 / fps_milli`` seconds, rounded to a sample count, so a
loop stays in sync. The first video and audio timestamps are kept: a delayed
soundtrack stays silent at the start instead of being shifted to sample zero.

``--no-audio`` writes only the RVF and removes a stale companion WAV.
``--audio-only`` writes that WAV for an existing RVF and does not rewrite frames.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import struct
import subprocess
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = ROOT / "sd_card_files" / "videos"
HEADER = struct.Struct("<4sHHHHIIIII")
MAGIC = b"RVF1"
VERSION = 1
PIXEL_FORMAT_RGB565_LE = 1
MIN_DIMENSION = 1
MAX_DIMENSION = 480
MIN_FPS = 1.0
MAX_FPS = 30.0
MIN_FPS_MILLI = 1000
MAX_FPS_MILLI = 30000
SAMPLE_RATE = 44100
CHANNELS = 2
BITS_PER_SAMPLE = 16
BLOCK_ALIGN = CHANNELS * (BITS_PER_SAMPLE // 8)
BYTE_RATE = SAMPLE_RATE * BLOCK_ALIGN
WAV_HEADER_SIZE = 44
MAX_WAV_DATA_BYTES = 0xFFFFFFFF - (WAV_HEADER_SIZE - 8)
_STREAM_RE = re.compile(
    r"^\s*Stream #\d+:(?P<index>\d+)"
    r"(?:\[[^\]]*\])?"
    r"(?:\([^)]*\))?"
    r":\s*(?P<kind>Video|Audio|Data|Subtitle|Attachment):(?P<rest>.*)$",
    re.MULTILINE,
)
_PTS_RE = r"pts_time:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
_VIDEO_PTS_RE = re.compile(r"\[Parsed_showinfo_[^\n]*" + _PTS_RE)
_AUDIO_PTS_RE = re.compile(r"\[Parsed_ashowinfo_[^\n]*" + _PTS_RE)
_ABSENCE_MARKERS = (
    "matches no streams",
    "does not contain any stream",
)


class AudioAbsent(Exception):
    """The selected input has no decodable audio stream."""


@dataclass(frozen=True)
class StreamInfo:
    index: int
    kind: str
    default: bool
    attached_pic: bool


@dataclass(frozen=True)
class RvfInfo:
    width: int
    height: int
    fps_milli: int
    frame_count: int
    frame_size: int
    header_size: int


def find_ffmpeg() -> str:
    executable = shutil.which("ffmpeg")
    if executable:
        return executable

    local_deps = ROOT / ".tools" / "python-deps"
    if local_deps.is_dir():
        sys.path.insert(0, str(local_deps))
    try:
        import imageio_ffmpeg
    except ImportError as exc:
        raise RuntimeError(
            "FFmpeg not found. Install imageio-ffmpeg with: "
            "py -m pip install imageio-ffmpeg"
        ) from exc
    return imageio_ffmpeg.get_ffmpeg_exe()


def companion_wav_path(rvf_path: Path) -> Path:
    """Return the soundtrack path that belongs to an RVF file."""
    return Path(rvf_path).with_suffix(".wav")


def pcm_sample_count(frame_count: int, fps_milli: int) -> int:
    """Return the stereo frames for ``frame_count * 1000 / fps_milli`` seconds.

    Half-up rounding keeps the WAV length on the same side of an exact half
    sample instead of using banker's rounding.
    """
    if isinstance(frame_count, bool) or not isinstance(frame_count, int):
        raise ValueError("Frame count must be a positive integer")
    if isinstance(fps_milli, bool) or not isinstance(fps_milli, int):
        raise ValueError("FPS milli must be a positive integer")
    if frame_count <= 0 or fps_milli <= 0:
        raise ValueError("RVF duration must be positive")
    numerator = frame_count * 1000 * SAMPLE_RATE
    return (numerator + fps_milli // 2) // fps_milli


def convert(
    source: Path,
    target: Path,
    width: int,
    height: int,
    fps: float,
    *,
    audio: bool = True,
    audio_only: bool = False,
) -> None:
    """Convert ``source`` to ``target``.

    ``audio`` extracts a companion WAV when the source has an audio stream.
    ``audio_only`` reads the duration from an existing RVF and writes only the
    WAV. Width, height, and FPS are ignored in that mode. Passing both
    ``audio_only`` and ``audio=False`` is rejected.
    """
    source = Path(source)
    target = Path(target)
    if target.suffix.lower() != ".rvf":
        raise ValueError("Output must have the .rvf extension")
    if _same_file(source, target):
        raise ValueError("Source and RVF output must be different files")
    if audio_only and not audio:
        raise ValueError("--audio-only cannot be combined with --no-audio")

    companion = companion_wav_path(target)
    if _same_file(source, companion):
        raise ValueError(
            "Refusing to read and write the same companion WAV: "
            f"{source}"
        )
    for temp_path in _temporary_paths(target):
        if _same_file(source, temp_path):
            raise ValueError(
                "Source is a temporary converter path. Choose another output: "
                f"{source}"
            )
    if not source.is_file():
        raise FileNotFoundError(f"Video source not found: {source}")

    if audio_only:
        info = _validate_rvf(target)
        _write_companion_for_existing_rvf(source, target, companion, info)
        return

    fps_milli = _validate_geometry(width, height, fps)
    _write_rvf_and_optional_audio(
        source, target, companion, width, height, fps, fps_milli, audio
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert MP4/video to ESP32 RGB565 RVF and optional PCM WAV"
    )
    parser.add_argument("source", type=Path, help="input video file")
    parser.add_argument("--output", type=Path, help="output .rvf path")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--fps", type=float, default=10.0)
    parser.add_argument(
        "--no-audio",
        action="store_true",
        help="write the RVF only and remove a stale companion WAV",
    )
    parser.add_argument(
        "--audio-only",
        action="store_true",
        help="write a companion WAV for an existing RVF without reencoding frames",
    )
    args = parser.parse_args()
    if args.no_audio and args.audio_only:
        print(
            "Conversion failed: --audio-only cannot be combined with --no-audio",
            file=sys.stderr,
        )
        return 1

    target = args.output or DEFAULT_OUTPUT_DIR / f"{args.source.stem}.rvf"
    try:
        convert(
            args.source,
            target,
            args.width,
            args.height,
            args.fps,
            audio=not args.no_audio,
            audio_only=args.audio_only,
        )
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"Conversion failed: {exc}", file=sys.stderr)
        return 1
    return 0


def _validate_geometry(width: int, height: int, fps: float) -> int:
    """Validate dimensions and FPS before any FFmpeg process starts."""
    _validate_dimension("width", width)
    _validate_dimension("height", height)
    if isinstance(fps, bool):
        raise ValueError("FPS must be a finite number from 1 to 30")
    try:
        fps_value = float(fps)
    except (TypeError, ValueError) as exc:
        raise ValueError("FPS must be a finite number from 1 to 30") from exc
    if not math.isfinite(fps_value) or not (MIN_FPS <= fps_value <= MAX_FPS):
        raise ValueError("FPS must be a finite number from 1 to 30")
    fps_milli = int(round(fps_value * 1000))
    if not (MIN_FPS_MILLI <= fps_milli <= MAX_FPS_MILLI):
        raise ValueError(
            "Rounded FPS header value must be from 1000 to 30000 milli-FPS"
        )
    frame_size = width * height * 2
    if frame_size <= 0 or frame_size > 0xFFFFFFFF:
        raise ValueError("Frame size exceeds the RVF header limit")
    if HEADER.size != 32 or HEADER.size > 0xFFFF:
        raise RuntimeError("RVF header layout is not 32 bytes")
    return fps_milli


def _validate_dimension(name: str, value: int) -> None:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer from 1 to 480")
    if value < MIN_DIMENSION or value > MAX_DIMENSION:
        raise ValueError(f"{name} must be an integer from 1 to 480")


def _validate_rvf(path: Path) -> RvfInfo:
    """Check an existing RVF header and payload without rewriting the file."""
    rvf_path = Path(path)
    if not rvf_path.is_file():
        raise FileNotFoundError(f"RVF file not found: {rvf_path}")
    file_size = rvf_path.stat().st_size
    with rvf_path.open("rb") as handle:
        blob = handle.read(HEADER.size)
        if len(blob) != HEADER.size:
            raise ValueError(f"RVF header is incomplete: {rvf_path}")
        (
            magic,
            version,
            header_size,
            width,
            height,
            fps_milli,
            frame_count,
            frame_size,
            pixel_format,
            _reserved,
        ) = HEADER.unpack(blob)
        if magic != MAGIC:
            raise ValueError(f"RVF magic is invalid: {rvf_path}")
        if version != VERSION:
            raise ValueError(f"RVF version is unsupported: {rvf_path}")
        if header_size < HEADER.size:
            raise ValueError(f"RVF header size is invalid: {rvf_path}")
        if width < MIN_DIMENSION or width > MAX_DIMENSION:
            raise ValueError(f"RVF width is outside 1..480: {rvf_path}")
        if height < MIN_DIMENSION or height > MAX_DIMENSION:
            raise ValueError(f"RVF height is outside 1..480: {rvf_path}")
        if fps_milli < MIN_FPS_MILLI or fps_milli > MAX_FPS_MILLI:
            raise ValueError(f"RVF FPS is outside 1..30: {rvf_path}")
        if frame_count <= 0:
            raise ValueError(f"RVF frame count is invalid: {rvf_path}")
        expected_frame_size = width * height * 2
        if frame_size != expected_frame_size:
            raise ValueError(f"RVF frame size does not match the geometry: {rvf_path}")
        if pixel_format != PIXEL_FORMAT_RGB565_LE:
            raise ValueError(f"RVF pixel format is not RGB565 little-endian: {rvf_path}")
        payload_end = header_size + frame_count * frame_size
        if header_size > file_size or payload_end > file_size:
            raise ValueError(f"RVF payload is shorter than the header declares: {rvf_path}")
        last_frame = header_size + (frame_count - 1) * frame_size
        handle.seek(last_frame)
        if len(handle.read(frame_size)) != frame_size:
            raise ValueError(f"RVF payload is shorter than the header declares: {rvf_path}")
    return RvfInfo(
        width=width,
        height=height,
        fps_milli=fps_milli,
        frame_count=frame_count,
        frame_size=frame_size,
        header_size=header_size,
    )


def _write_rvf_and_optional_audio(
    source: Path,
    target: Path,
    companion: Path,
    width: int,
    height: int,
    fps: float,
    fps_milli: int,
    audio: bool,
) -> None:
    ffmpeg = find_ffmpeg()
    video_stream, audio_stream = _selected_streams(ffmpeg, source)
    if video_stream is None:
        raise RuntimeError(f"No video stream in {source}")

    target.parent.mkdir(parents=True, exist_ok=True)
    temp_rvf, temp_wav, temp_pcm = _temporary_paths(target)
    cleanup = [temp_rvf, temp_wav, temp_pcm]
    try:
        frame_count = _encode_rvf(
            ffmpeg, source, temp_rvf, video_stream.index, width, height, fps, fps_milli
        )
        sample_count = 0
        wrote_audio = False
        if audio and audio_stream is not None:
            try:
                sample_count = _extract_aligned_wav(
                    ffmpeg,
                    source,
                    temp_pcm,
                    temp_wav,
                    video_stream.index,
                    audio_stream.index,
                    frame_count,
                    fps_milli,
                )
                wrote_audio = True
            except AudioAbsent:
                wrote_audio = False
        elif audio:
            wrote_audio = False
        _commit_video_pair(temp_rvf, target, temp_wav if wrote_audio else None, companion)
        _report_video(target, companion, width, height, fps, frame_count, wrote_audio, sample_count)
    finally:
        _discard(cleanup)


def _write_companion_for_existing_rvf(
    source: Path,
    target: Path,
    companion: Path,
    info: RvfInfo,
) -> None:
    ffmpeg = find_ffmpeg()
    _video_stream, audio_stream = _selected_streams(ffmpeg, source)
    if audio_stream is None:
        _remove_stale_soundtrack(companion, source)
        print(f"RVF unchanged: {target}")
        print("Audio: none")
        print("No companion WAV written.")
        return

    _temp_rvf, temp_wav, temp_pcm = _temporary_paths(target)
    cleanup = [temp_wav, temp_pcm]
    video_index = _video_stream.index if _video_stream is not None else None
    absent = False
    try:
        sample_count = _extract_aligned_wav(
            ffmpeg,
            source,
            temp_pcm,
            temp_wav,
            video_index,
            audio_stream.index,
            info.frame_count,
            info.fps_milli,
        )
        temp_wav.replace(companion)
        cleanup.remove(temp_wav)
    except AudioAbsent:
        absent = True
    else:
        absent = False
    finally:
        _discard(cleanup)
    if absent:
        _remove_stale_soundtrack(companion, source)
        print(f"RVF unchanged: {target}")
        print("Audio: none")
        print("No companion WAV written.")
        return

    duration = sample_count / SAMPLE_RATE
    print(f"RVF unchanged: {target}")
    print(
        f"Audio: {SAMPLE_RATE} Hz, stereo, PCM16 LE, "
        f"{sample_count} samples, {duration:.3f} s"
    )
    print(f"Companion: {companion}")
    print("Copy the WAV next to the RVF in the SD card /videos directory.")


def _encode_rvf(
    ffmpeg: str,
    source: Path,
    temp_target: Path,
    video_index: int,
    width: int,
    height: int,
    fps: float,
    fps_milli: int,
) -> int:
    frame_size = width * height * 2
    video_filter = (
        f"fps={fps_milli / 1000},"
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black"
    )
    command = [
        ffmpeg,
        "-hide_banner",
        "-nostdin",
        "-loglevel", "warning",
        "-stats",
        "-i", str(source),
        "-map", f"0:{video_index}",
        "-an",
        "-vf", video_filter,
        "-pix_fmt", "rgb565le",
        # The FPS filter already controls cadence. Prevent the output muxer
        # from inserting leading duplicate frames for a delayed video stream.
        "-fps_mode", "passthrough",
        "-f", "rawvideo",
        "pipe:1",
    ]
    with temp_target.open("w+b") as output:
        output.write(b"\0" * HEADER.size)
        subprocess.run(command, stdout=output, check=True)
        payload_size = output.tell() - HEADER.size
        if payload_size <= 0 or payload_size % frame_size:
            raise RuntimeError(
                f"Invalid raw frame payload: {payload_size} bytes "
                f"for {frame_size}-byte frames"
            )
        frame_count = payload_size // frame_size
        if frame_count > 0xFFFFFFFF:
            raise RuntimeError("Frame count exceeds the RVF header limit")
        output.seek(0)
        output.write(HEADER.pack(
            MAGIC,
            VERSION,
            HEADER.size,
            width,
            height,
            fps_milli,
            frame_count,
            frame_size,
            PIXEL_FORMAT_RGB565_LE,
            0,
        ))
    return frame_count


def _extract_aligned_wav(
    ffmpeg: str,
    source: Path,
    temp_pcm: Path,
    temp_wav: Path,
    video_index: int | None,
    audio_index: int,
    frame_count: int,
    fps_milli: int,
) -> int:
    sample_count = pcm_sample_count(frame_count, fps_milli)
    if sample_count * BLOCK_ALIGN > MAX_WAV_DATA_BYTES:
        raise ValueError("WAV payload exceeds the 32-bit RIFF size")
    if video_index is None:
        video_pts = 0.0
        audio_pts = _probe_audio_pts(ffmpeg, source, audio_index)
    else:
        video_pts, audio_pts = _probe_stream_pts(ffmpeg, source, video_index, audio_index)
    # Raw s16le starts at the first decoded sample. Put that sample back on the
    # video timeline here; do not reset both streams to zero independently.
    leading_samples = _round_half_away_from_zero((audio_pts - video_pts) * SAMPLE_RATE)
    command = [
        ffmpeg,
        "-hide_banner",
        "-nostdin",
        "-loglevel", "error",
        "-y",
        "-i", str(source),
        "-map", f"0:{audio_index}?",
        "-vn",
        "-af",
        "aformat=sample_fmts=s16:channel_layouts=stereo,"
        f"aresample={SAMPLE_RATE}:osf=s16:ochl=stereo",
        "-f", "s16le",
        str(temp_pcm),
    ]
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        detail = _stderr_text(result.stderr)
        if _is_audio_absence(detail):
            raise AudioAbsent(detail)
        tail = "\n".join(detail.strip().splitlines()[-12:])
        raise RuntimeError(f"FFmpeg audio decode failed:\n{tail}") from subprocess.CalledProcessError(
            result.returncode, command, stderr=result.stderr
        )
    if not temp_pcm.is_file():
        raise AudioAbsent("FFmpeg wrote no audio payload")
    _write_aligned_wav(temp_pcm, temp_wav, sample_count, leading_samples)
    return sample_count


def _probe_stream_pts(
    ffmpeg: str,
    source: Path,
    video_index: int,
    audio_index: int,
) -> tuple[float, float]:
    command = [
        ffmpeg,
        "-hide_banner",
        "-nostdin",
        "-nostats",
        "-v", "info",
        "-i", str(source),
        "-map", f"0:{video_index}",
        "-vf", "showinfo",
        "-frames:v", "1",
        "-f", "null", "-",
        "-map", f"0:{audio_index}",
        "-af", "ashowinfo",
        "-frames:a", "1",
        "-f", "null", "-",
    ]
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    text = _stderr_text(result.stderr) + _stderr_text(result.stdout)
    video_match = _VIDEO_PTS_RE.search(text)
    audio_match = _AUDIO_PTS_RE.search(text)
    if video_match is None or audio_match is None:
        tail = "\n".join(text.strip().splitlines()[-12:])
        raise RuntimeError(
            "Could not read the primary video and audio timestamps.\n" + tail
        )
    return float(video_match.group(1)), float(audio_match.group(1))


def _probe_audio_pts(ffmpeg: str, source: Path, audio_index: int) -> float:
    """Read the first audio timestamp when the RVF timeline already starts at zero."""
    command = [
        ffmpeg,
        "-hide_banner",
        "-nostdin",
        "-nostats",
        "-v", "info",
        "-i", str(source),
        "-map", f"0:{audio_index}",
        "-af", "ashowinfo",
        "-frames:a", "1",
        "-f", "null", "-",
    ]
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    text = _stderr_text(result.stderr) + _stderr_text(result.stdout)
    audio_match = _AUDIO_PTS_RE.search(text)
    if audio_match is None:
        tail = "\n".join(text.strip().splitlines()[-12:])
        raise RuntimeError("Could not read the primary audio timestamp.\n" + tail)
    return float(audio_match.group(1))


def _selected_streams(
    ffmpeg: str,
    source: Path,
) -> tuple[StreamInfo | None, StreamInfo | None]:
    streams = _probe_streams(ffmpeg, source)
    return _select_primary(streams, "video"), _select_primary(streams, "audio")


def _probe_streams(ffmpeg: str, source: Path) -> list[StreamInfo]:
    command = [
        ffmpeg,
        "-hide_banner",
        "-nostdin",
        "-nostats",
        "-i", str(source),
    ]
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    text = _stderr_text(result.stderr) + _stderr_text(result.stdout)
    streams = _parse_streams(text)
    if streams:
        return streams
    tail = "\n".join(text.strip().splitlines()[-12:])
    raise RuntimeError(f"Could not read media streams from {source}\n{tail}")


def _parse_streams(text: str) -> list[StreamInfo]:
    start = text.find("Input #0")
    if start < 0:
        return []
    next_input = text.find("\nInput #1", start + 1)
    block = text[start:] if next_input < 0 else text[start:next_input]
    streams: list[StreamInfo] = []
    for match in _STREAM_RE.finditer(block):
        rest = match.group("rest")
        streams.append(StreamInfo(
            index=int(match.group("index")),
            kind=match.group("kind").lower(),
            default="(default)" in rest,
            attached_pic="attached pic" in rest.lower(),
        ))
    return streams


def _select_primary(streams: list[StreamInfo], kind: str) -> StreamInfo | None:
    typed = [
        stream for stream in streams
        if stream.kind == kind and not (kind == "video" and stream.attached_pic)
    ]
    if not typed:
        return None
    for stream in typed:
        if stream.default:
            return stream
    return typed[0]


def _write_aligned_wav(
    pcm_path: Path,
    wav_path: Path,
    sample_count: int,
    leading_samples: int,
) -> None:
    pcm_size = pcm_path.stat().st_size
    if pcm_size % BLOCK_ALIGN:
        raise RuntimeError(
            f"Audio payload is not a whole number of {BLOCK_ALIGN}-byte frames: "
            f"{pcm_size} bytes"
        )
    pcm_samples = pcm_size // BLOCK_ALIGN
    if leading_samples >= 0:
        pad_head = min(leading_samples, sample_count)
        skip = 0
    else:
        pad_head = 0
        skip = min(pcm_samples, -leading_samples)
    body = min(pcm_samples - skip, sample_count - pad_head)
    pad_tail = sample_count - pad_head - body
    with pcm_path.open("rb") as source, wav_path.open("wb") as output:
        output.write(_wav_header(sample_count))
        _write_silence(output, pad_head)
        if body:
            source.seek(skip * BLOCK_ALIGN)
            remaining = body * BLOCK_ALIGN
            while remaining:
                chunk = source.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise RuntimeError(
                        "Audio payload ended early while aligning the soundtrack"
                    )
                if len(chunk) > remaining:
                    chunk = chunk[:remaining]
                output.write(chunk)
                remaining -= len(chunk)
        _write_silence(output, pad_tail)


def _wav_header(sample_count: int) -> bytes:
    data_size = sample_count * BLOCK_ALIGN
    if data_size > MAX_WAV_DATA_BYTES:
        raise ValueError("WAV payload exceeds the 32-bit RIFF size")
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + data_size,
        b"WAVE",
        b"fmt ",
        16,
        1,
        CHANNELS,
        SAMPLE_RATE,
        BYTE_RATE,
        BLOCK_ALIGN,
        BITS_PER_SAMPLE,
        b"data",
        data_size,
    )


def _write_silence(output, sample_count: int) -> None:
    remaining = sample_count * BLOCK_ALIGN
    zeros = b"\0" * min(remaining, 1024 * 1024) if remaining else b""
    while remaining:
        chunk = zeros if remaining >= len(zeros) else zeros[:remaining]
        output.write(chunk)
        remaining -= len(chunk)


def _report_video(
    target: Path,
    companion: Path,
    width: int,
    height: int,
    fps: float,
    frame_count: int,
    wrote_audio: bool,
    sample_count: int,
) -> None:
    duration = frame_count / fps
    print(f"Created: {target}")
    print(
        f"Video: {width}x{height}, {fps:g} FPS, {frame_count} frames, "
        f"{duration:.1f} s"
    )
    if wrote_audio:
        audio_duration = sample_count / SAMPLE_RATE
        print(
            f"Audio: {SAMPLE_RATE} Hz, stereo, PCM16 LE, "
            f"{sample_count} samples, {audio_duration:.3f} s"
        )
        print(f"Companion: {companion}")
        video_mb = target.stat().st_size / (1024 * 1024)
        audio_mb = companion.stat().st_size / (1024 * 1024)
        print(f"File size: {video_mb:.1f} MB video, {audio_mb:.1f} MB audio")
        print("Copy both files to the SD card /videos directory.")
        return
    print("Audio: none")
    print(f"File size: {target.stat().st_size / (1024 * 1024):.1f} MB")
    print("Copy the RVF to the SD card /videos directory.")


def _remove_stale_soundtrack(companion: Path, source: Path) -> None:
    if not companion.exists():
        return
    if not companion.is_file():
        raise RuntimeError(
            f"Companion path is not a file and was left untouched: {companion}"
        )
    if _same_file(companion, source):
        raise ValueError(
            "Refusing to delete the source file while removing a stale soundtrack: "
            f"{companion}"
        )
    companion.unlink()
    print(f"Removed stale soundtrack: {companion}")


def _commit_video_pair(
    temp_video: Path, video: Path, temp_audio: Path | None, audio: Path
) -> None:
    """Keep both previous outputs recoverable until the entire pair is installed."""
    for destination in (video, audio):
        if destination.exists() and not destination.is_file():
            raise RuntimeError(f"Output path is not a file: {destination}")
    backups: list[tuple[Path, Path]] = []
    installed: list[Path] = []
    try:
        for destination in (video, audio):
            if destination.exists():
                backup = destination.with_name(destination.name + ".backup-" + uuid.uuid4().hex)
                destination.replace(backup)
                backups.append((destination, backup))
        temp_video.replace(video)
        installed.append(video)
        if temp_audio is not None:
            temp_audio.replace(audio)
            installed.append(audio)
    except OSError:
        for destination in reversed(installed):
            destination.unlink()
        # If recovery fails, leave the backup files intact for manual recovery.
        for destination, backup in reversed(backups):
            backup.replace(destination)
        raise
    else:
        for _destination, backup in backups:
            backup.unlink()


def _temporary_paths(target: Path) -> tuple[Path, Path, Path]:
    companion = companion_wav_path(target)
    return (
        target.with_suffix(target.suffix + ".new"),
        companion.with_suffix(companion.suffix + ".new"),
        companion.with_suffix(".pcm.new"),
    )


def _discard(paths: list[Path]) -> None:
    for path in list(paths):
        if path.suffix != ".new":
            raise RuntimeError(f"Refusing to delete a non-temporary path: {path}")
        if path.is_file():
            path.unlink()
        paths.remove(path)


def _same_file(left: Path, right: Path) -> bool:
    return left.expanduser().resolve() == right.expanduser().resolve()


def _round_half_away_from_zero(value: float) -> int:
    if value >= 0:
        return int(math.floor(value + 0.5))
    return -int(math.floor(-value + 0.5))


def _stderr_text(data: bytes | None) -> str:
    if not data:
        return ""
    return data.decode("utf-8", "replace")


def _is_audio_absence(text: str) -> bool:
    return any(marker in text for marker in _ABSENCE_MARKERS)


if __name__ == "__main__":
    raise SystemExit(main())
