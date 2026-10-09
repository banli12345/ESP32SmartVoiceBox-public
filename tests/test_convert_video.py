#!/usr/bin/env python3
"""Tests for RVF conversion and the companion PCM WAV soundtrack."""

from __future__ import annotations

import array
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import convert_video as cv  # noqa: E402


def _require_ffmpeg(test: unittest.TestCase) -> str:
    try:
        return cv.find_ffmpeg()
    except RuntimeError as exc:
        test.skipTest(str(exc))
        return ""


def _run_ffmpeg(ffmpeg: str, args: list[str]) -> None:
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", "-y", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace")
        raise RuntimeError(detail)


def _write_video_raw(path: Path, frames: int, width: int = 8, height: int = 8) -> None:
    frame = bytes((40, 80, 120)) * (width * height)
    path.write_bytes(frame * frames)


def _write_stereo(path: Path, samples: list[tuple[int, int]]) -> None:
    data = array.array("h")
    for left, right in samples:
        data.append(left)
        data.append(right)
    path.write_bytes(data.tobytes())


def _ramp(samples: int) -> list[tuple[int, int]]:
    frames = []
    for index in range(samples):
        left = index - 16384
        right = -left if left != -32768 else 32767
        frames.append((left, right))
    return frames


def _constant(samples: int, left: int, right: int) -> list[tuple[int, int]]:
    return [(left, right)] * samples


def _mux_video_audio(
    ffmpeg: str,
    dest: Path,
    video_raw: Path,
    audio_raw: Path,
    *,
    audio_offset: str | None = None,
    video_offset: str | None = None,
    rate: int = 44100,
) -> None:
    command = []
    if video_offset is not None:
        command.extend(["-itsoffset", video_offset])
    command.extend([
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "8x8", "-r", "10",
        "-i", str(video_raw),
    ])
    if audio_offset is not None:
        command.extend(["-itsoffset", audio_offset])
    command.extend([
        "-f", "s16le", "-ar", str(rate), "-ac", "2", "-i", str(audio_raw),
        "-map", "0:v:0", "-map", "1:a:0",
        "-c:v", "mpeg4", "-q:v", "2", "-c:a", "pcm_s16le",
        str(dest),
    ])
    _run_ffmpeg(ffmpeg, command)


def _read_wav(path: Path) -> tuple[dict[str, int], list[tuple[int, int]]]:
    blob = path.read_bytes()
    if len(blob) < cv.WAV_HEADER_SIZE:
        raise AssertionError(f"WAV is shorter than a canonical header: {path}")
    fields = struct.unpack("<4sI4s4sIHHIIHH4sI", blob[:cv.WAV_HEADER_SIZE])
    header = {
        "riff": fields[0],
        "riff_size": fields[1],
        "wave": fields[2],
        "fmt": fields[3],
        "fmt_size": fields[4],
        "format": fields[5],
        "channels": fields[6],
        "rate": fields[7],
        "byte_rate": fields[8],
        "block_align": fields[9],
        "bits": fields[10],
        "data": fields[11],
        "data_size": fields[12],
    }
    payload = blob[cv.WAV_HEADER_SIZE:]
    if len(payload) != header["data_size"] or len(payload) % 4:
        raise AssertionError("WAV payload does not match the data chunk")
    unpacked = struct.unpack("<" + "h" * (len(payload) // 2), payload)
    pairs = list(zip(unpacked[0::2], unpacked[1::2]))
    return header, pairs


def _read_rvf(path: Path) -> tuple[tuple, bytes]:
    blob = path.read_bytes()
    return cv.HEADER.unpack(blob[:cv.HEADER.size]), blob


def _assert_wav_contract(test: unittest.TestCase, path: Path, frame_count: int, fps_milli: int):
    header, pairs = _read_wav(path)
    expected = cv.pcm_sample_count(frame_count, fps_milli)
    test.assertEqual(header["riff"], b"RIFF")
    test.assertEqual(header["wave"], b"WAVE")
    test.assertEqual(header["fmt"], b"fmt ")
    test.assertEqual(header["fmt_size"], 16)
    test.assertEqual(header["format"], 1)
    test.assertEqual(header["channels"], 2)
    test.assertEqual(header["rate"], 44100)
    test.assertEqual(header["byte_rate"], 176400)
    test.assertEqual(header["block_align"], 4)
    test.assertEqual(header["bits"], 16)
    test.assertEqual(header["data"], b"data")
    test.assertEqual(header["data_size"], expected * 4)
    test.assertEqual(header["riff_size"], 36 + expected * 4)
    test.assertEqual(len(pairs), expected)
    test.assertEqual(path.stat().st_size, cv.WAV_HEADER_SIZE + expected * 4)
    return pairs


def _rvf_blob(
    *,
    magic: bytes = b"RVF1",
    version: int = 1,
    header_size: int = 32,
    width: int = 8,
    height: int = 8,
    fps_milli: int = 10000,
    frame_count: int = 2,
    frame_size: int | None = None,
    pixel_format: int = 1,
    payload: bytes | None = None,
    truncate: int = 0,
) -> bytes:
    if frame_size is None:
        frame_size = width * height * 2
    if payload is None:
        payload = b"\x5A" * (frame_size * frame_count)
    if header_size > cv.HEADER.size:
        header = cv.HEADER.pack(
            magic, version, header_size, width, height, fps_milli,
            frame_count, frame_size, pixel_format, 0,
        ) + b"\x00" * (header_size - cv.HEADER.size)
    else:
        header = cv.HEADER.pack(
            magic, version, header_size, width, height, fps_milli,
            frame_count, frame_size, pixel_format, 0,
        )
    blob = header + payload
    if truncate:
        blob = blob[:-truncate]
    return blob


class SampleCountTests(unittest.TestCase):
    def test_half_up_and_known_duration(self) -> None:
        self.assertEqual(cv.HEADER.format, "<4sHHHHIIIII")
        self.assertEqual(cv.HEADER.size, 32)
        self.assertEqual(cv.pcm_sample_count(1, 1001), 44056)
        self.assertEqual(cv.pcm_sample_count(1, 3528000), 13)
        self.assertEqual(cv.pcm_sample_count(663, 20000), 1461915)
        with self.assertRaises(ValueError):
            cv.pcm_sample_count(0, 1000)
        with self.assertRaises(ValueError):
            cv.pcm_sample_count(1, 0)

    def test_companion_path_uses_rvf_stem(self) -> None:
        self.assertEqual(
            cv.companion_wav_path(Path("/videos/ikun20.rvf")),
            Path("/videos/ikun20.wav"),
        )


class ArgumentTests(unittest.TestCase):
    def test_output_cannot_overwrite_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.rvf"
            source.write_bytes(b"original")
            with mock.patch("convert_video.find_ffmpeg") as ffmpeg:
                with self.assertRaises(ValueError):
                    cv.convert(source, source, 8, 8, 10)
                with self.assertRaises(ValueError):
                    cv.convert(source, source.with_suffix(".mp4"), 8, 8, 10)
                ffmpeg.assert_not_called()
            self.assertEqual(source.read_bytes(), b"original")

    def test_invalid_geometry_does_not_invoke_ffmpeg(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "clip.mp4"
            source.write_bytes(b"not media")
            target = root / "clip.rvf"
            keeper = root / "keep.txt"
            keeper.write_bytes(b"keep")
            invalid = [
                (0, 8, 10),
                (-1, 8, 10),
                (481, 8, 10),
                (8, 0, 10),
                (8, 481, 10),
                (8.5, 8, 10),
                (True, 8, 10),
                (8, 8, 0),
                (8, 8, -1),
                (8, 8, 0.5),
                (8, 8, 31),
                (8, 8, float("nan")),
                (8, 8, float("inf")),
                (8, 8, "fast"),
                (8, 8, True),
            ]
            with mock.patch("convert_video.subprocess.run", side_effect=AssertionError("ffmpeg")):
                for width, height, fps in invalid:
                    with self.subTest(width=width, height=height, fps=fps):
                        with self.assertRaises(ValueError):
                            cv.convert(source, target, width, height, fps)
            self.assertFalse(target.exists())
            self.assertEqual(keeper.read_bytes(), b"keep")
            self.assertEqual(list(root.glob("*.new")), [])

    def test_missing_source_and_conflicting_modes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(FileNotFoundError):
                cv.convert(root / "missing.mp4", root / "out.rvf", 8, 8, 10)
            source = root / "clip.wav"
            target = root / "clip.rvf"
            source.write_bytes(b"source-bytes")
            target.write_bytes(b"rvf-bytes")
            with self.assertRaises(ValueError):
                cv.convert(source, target, 8, 8, 10, audio_only=True)
            with self.assertRaises(ValueError):
                cv.convert(source, target, 8, 8, 10, audio=False, audio_only=True)
            self.assertEqual(source.read_bytes(), b"source-bytes")
            self.assertEqual(target.read_bytes(), b"rvf-bytes")

    def test_main_rejects_both_audio_flags(self) -> None:
        with mock.patch("sys.argv", ["convert_video.py", "--no-audio", "--audio-only", "clip.mp4"]):
            self.assertEqual(cv.main(), 1)

    def test_malformed_rvf_is_rejected_without_ffmpeg(self) -> None:
        cases = {
            "bad magic": _rvf_blob(magic=b"NOPE"),
            "truncated header": b"RVF1" + b"\x00" * 8,
            "bad version": _rvf_blob(version=2),
            "short header size": _rvf_blob(header_size=16),
            "width zero": _rvf_blob(width=0, frame_size=128),
            "height too large": _rvf_blob(height=481, frame_size=8 * 481 * 2),
            "fps too low": _rvf_blob(fps_milli=999),
            "fps too high": _rvf_blob(fps_milli=30001),
            "no frames": _rvf_blob(frame_count=0, payload=b""),
            "wrong frame size": _rvf_blob(frame_size=10, payload=b"\x00" * 20),
            "wrong pixel format": _rvf_blob(pixel_format=2),
            "truncated payload": _rvf_blob(truncate=4),
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.mp4"
            source.write_bytes(b"source")
            with mock.patch("convert_video.subprocess.run", side_effect=AssertionError("ffmpeg")):
                for name, blob in cases.items():
                    with self.subTest(name=name):
                        target = root / f"{name}.rvf"
                        companion = cv.companion_wav_path(target)
                        target.write_bytes(blob)
                        companion.write_bytes(b"old-wav")
                        with self.assertRaises(ValueError):
                            cv.convert(source, target, 8, 8, 10, audio_only=True)
                        self.assertEqual(target.read_bytes(), blob)
                        self.assertEqual(companion.read_bytes(), b"old-wav")

    def test_audio_only_missing_rvf_does_not_create_wav(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.mp4"
            source.write_bytes(b"source")
            target = root / "missing.rvf"
            with self.assertRaises(FileNotFoundError):
                cv.convert(source, target, 8, 8, 10, audio_only=True)
            self.assertFalse(cv.companion_wav_path(target).exists())
            self.assertEqual(source.read_bytes(), b"source")


class FFmpegConversionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        try:
            cls.ffmpeg = cv.find_ffmpeg()
        except RuntimeError:
            cls.ffmpeg = ""
            return
        cls._owner = tempfile.TemporaryDirectory()
        cls.root = Path(cls._owner.name)
        video_raw = cls.root / "video.raw"
        _write_video_raw(video_raw, 4)
        ramp = cls.root / "ramp.pcm"
        _write_stereo(ramp, _ramp(44100))
        low = cls.root / "low.pcm"
        high = cls.root / "high.pcm"
        _write_stereo(low, _constant(44100, 1000, -1000))
        _write_stereo(high, _constant(44100, 5000, -5000))
        cls.sync = cls.root / "sync.mkv"
        cls.delayed = cls.root / "delayed.mkv"
        cls.early = cls.root / "early.mkv"
        cls.silent = cls.root / "silent.mkv"
        cls.primary = cls.root / "primary.mkv"
        _mux_video_audio(cls.ffmpeg, cls.sync, video_raw, ramp)
        _mux_video_audio(cls.ffmpeg, cls.delayed, video_raw, ramp, audio_offset="0.2")
        _mux_video_audio(cls.ffmpeg, cls.early, video_raw, ramp, audio_offset="-0.2")
        _run_ffmpeg(cls.ffmpeg, [
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "8x8", "-r", "10",
            "-i", str(video_raw),
            "-frames:v", "4", "-c:v", "mpeg4", "-an", str(cls.silent),
        ])
        _run_ffmpeg(cls.ffmpeg, [
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "8x8", "-r", "10",
            "-i", str(video_raw),
            "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", str(low),
            "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", str(high),
            "-map", "0:v:0", "-map", "1:a:0", "-map", "2:a:0",
            "-disposition:a:0", "0", "-disposition:a:1", "default",
            "-c:v", "mpeg4", "-q:v", "8", "-c:a", "pcm_s16le",
            str(cls.primary),
        ])

    @classmethod
    def tearDownClass(cls) -> None:
        if getattr(cls, "_owner", None) is not None:
            cls._owner.cleanup()

    def setUp(self) -> None:
        if not self.ffmpeg:
            self.skipTest("FFmpeg not found")

    def test_sync_audio_properties_and_exact_sample_count(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "clip.rvf"
            cv.convert(self.sync, target, 8, 8, 10)
            header, _blob = _read_rvf(target)
            magic, version, header_size, width, height, fps_milli, frames, frame_size, pixel, _reserved = header
            self.assertEqual(magic, b"RVF1")
            self.assertEqual(version, 1)
            self.assertEqual(header_size, 32)
            self.assertEqual((width, height, fps_milli, pixel), (8, 8, 10000, 1))
            self.assertEqual(frame_size, 128)
            self.assertGreater(frames, 0)
            self.assertEqual(target.stat().st_size, 32 + frames * frame_size)
            companion = cv.companion_wav_path(target)
            pairs = _assert_wav_contract(self, companion, frames, fps_milli)
            self.assertEqual(pairs[0], (-16384, 16384))
            self.assertEqual(pairs[1], (-16383, 16383))
            self.assertFalse(all(sample == (0, 0) for sample in pairs))
            self.assertEqual(list(Path(temporary).glob("*.new")), [])

    def test_delayed_audio_start_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "clip.rvf"
            cv.convert(self.delayed, target, 8, 8, 10)
            _header, _blob = _read_rvf(target)
            frames = _header[6]
            fps_milli = _header[5]
            pairs = _assert_wav_contract(self, cv.companion_wav_path(target), frames, fps_milli)
            lead = 0
            for sample in pairs:
                if sample != (0, 0):
                    break
                lead += 1
            self.assertEqual(lead, 8820)
            self.assertEqual(pairs[8820], (-16384, 16384))
            self.assertEqual(pairs[8821], (-16383, 16383))

    def test_early_audio_is_trimmed_to_video_start(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "clip.rvf"
            cv.convert(self.early, target, 8, 8, 10)
            header, _blob = _read_rvf(target)
            frames = header[6]
            self.assertEqual(frames, 4, "Delayed video must not gain muxer duplicates")
            fps_milli = header[5]
            pairs = _assert_wav_contract(self, cv.companion_wav_path(target), frames, fps_milli)
            self.assertEqual(pairs[0], (8820 - 16384, -(8820 - 16384)))
            self.assertEqual(pairs[1], (8821 - 16384, -(8821 - 16384)))

    def test_audio_only_preserves_delay_and_rvf_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "clip.rvf"
            blob = _rvf_blob(frame_count=4, fps_milli=10000)
            target.write_bytes(blob)
            cv.convert(self.delayed, target, 8, 8, 10, audio_only=True)
            self.assertEqual(target.read_bytes(), blob)
            pairs = _assert_wav_contract(self, cv.companion_wav_path(target), 4, 10000)
            lead = 0
            for sample in pairs:
                if sample != (0, 0):
                    break
                lead += 1
            self.assertEqual(lead, 8820)
            self.assertEqual(pairs[8820], (-16384, 16384))

    def test_default_audio_stream_is_selected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "clip.rvf"
            cv.convert(self.primary, target, 8, 8, 10)
            header, _blob = _read_rvf(target)
            pairs = _assert_wav_contract(self, cv.companion_wav_path(target), header[6], header[5])
            self.assertEqual(pairs[0], (5000, -5000))
            self.assertTrue(all(sample == (5000, -5000) for sample in pairs[:200]))

    def test_no_audio_stream_removes_stale_wav_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            companion = cv.companion_wav_path(target)
            target.write_bytes(b"old-rvf")
            companion.write_bytes(b"old-wav")
            other = root / "other.wav"
            other.write_bytes(b"keep-other")
            notes = root / "notes.txt"
            notes.write_bytes(b"keep-notes")
            cv.convert(self.silent, target, 8, 8, 10)
            self.assertTrue(target.stat().st_size > len(b"old-rvf"))
            self.assertFalse(companion.exists())
            self.assertEqual(other.read_bytes(), b"keep-other")
            self.assertEqual(notes.read_bytes(), b"keep-notes")
            self.assertEqual(list(root.glob("*.new")), [])

    def test_no_audio_flag_removes_stale_wav(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            companion = cv.companion_wav_path(target)
            companion.write_bytes(b"old-wav")
            cv.convert(self.sync, target, 8, 8, 10, audio=False)
            header, _blob = _read_rvf(target)
            self.assertEqual(header[0], b"RVF1")
            self.assertGreater(header[6], 0)
            self.assertFalse(companion.exists())
            self.assertEqual(list(root.glob("*.new")), [])

    def test_audio_only_keeps_rvf_bytes_and_uses_header_duration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "existing.rvf"
            blob = _rvf_blob(frame_count=3, fps_milli=10000)
            target.write_bytes(blob)
            cv.convert(self.sync, target, 0, 0, 0, audio_only=True)
            self.assertEqual(target.read_bytes(), blob)
            pairs = _assert_wav_contract(self, cv.companion_wav_path(target), 3, 10000)
            self.assertEqual(len(pairs), cv.pcm_sample_count(3, 10000))
            self.assertEqual(pairs[0], (-16384, 16384))
            odd = root / "odd.rvf"
            odd_blob = _rvf_blob(frame_count=1, fps_milli=1001)
            odd.write_bytes(odd_blob)
            cv.convert(self.sync, odd, 1, 1, 1, audio_only=True)
            self.assertEqual(odd.read_bytes(), odd_blob)
            _assert_wav_contract(self, cv.companion_wav_path(odd), 1, 1001)

    def test_failure_preserves_outputs_and_cleans_temps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            companion = cv.companion_wav_path(target)
            target.write_bytes(b"OLD-RVF")
            companion.write_bytes(b"OLD-WAV")
            notes = root / "notes.txt"
            notes.write_bytes(b"keep")
            source = root / "bad.txt"
            source.write_bytes(b"this is not media")
            with self.assertRaises((OSError, ValueError, RuntimeError, subprocess.CalledProcessError)):
                cv.convert(source, target, 8, 8, 10)
            self.assertEqual(target.read_bytes(), b"OLD-RVF")
            self.assertEqual(companion.read_bytes(), b"OLD-WAV")
            self.assertEqual(notes.read_bytes(), b"keep")
            self.assertEqual(list(root.glob("*.new")), [])

    def test_audio_extract_failure_preserves_existing_rvf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            companion = cv.companion_wav_path(target)
            target.write_bytes(b"OLD-RVF")
            companion.write_bytes(b"OLD-WAV")
            real_run = subprocess.run

            def fail_pcm(command, *args, **kwargs):
                tokens = [str(part) for part in command]
                if "-f" in tokens and tokens[tokens.index("-f") + 1] == "s16le":
                    raise subprocess.CalledProcessError(
                        1, command, stderr=b"Error while decoding stream\n"
                    )
                return real_run(command, *args, **kwargs)

            with mock.patch("convert_video.subprocess.run", side_effect=fail_pcm):
                with self.assertRaises((RuntimeError, subprocess.CalledProcessError)):
                    cv.convert(self.sync, target, 8, 8, 10)
            self.assertEqual(target.read_bytes(), b"OLD-RVF")
            self.assertEqual(companion.read_bytes(), b"OLD-WAV")
            self.assertEqual(list(root.glob("*.new")), [])

    def test_audio_only_failure_preserves_both_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            blob = _rvf_blob()
            target.write_bytes(blob)
            companion = cv.companion_wav_path(target)
            companion.write_bytes(b"OLD-WAV")
            source = root / "bad.txt"
            source.write_bytes(b"this is not media")
            with self.assertRaises((OSError, ValueError, RuntimeError, subprocess.CalledProcessError)):
                cv.convert(source, target, 8, 8, 10, audio_only=True)
            self.assertEqual(target.read_bytes(), blob)
            self.assertEqual(companion.read_bytes(), b"OLD-WAV")
            self.assertEqual(list(root.glob("*.new")), [])

    def test_pair_install_failure_restores_both_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            companion = cv.companion_wav_path(target)
            target.write_bytes(b"OLD-RVF")
            companion.write_bytes(b"OLD-WAV")
            pending_wav = cv._temporary_paths(target)[1]
            original_replace = Path.replace

            def fail_audio_install(path, destination):
                if path == pending_wav:
                    raise PermissionError("Simulated locked companion")
                return original_replace(path, destination)

            with mock.patch.object(Path, "replace", fail_audio_install):
                with self.assertRaises(PermissionError):
                    cv.convert(self.sync, target, 8, 8, 10)
            self.assertEqual(target.read_bytes(), b"OLD-RVF")
            self.assertEqual(companion.read_bytes(), b"OLD-WAV")
            self.assertEqual(list(root.glob("*.new")), [])
            self.assertEqual(list(root.glob("*.backup-*")), [])

    def test_nonfile_companion_keeps_existing_video(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "clip.rvf"
            target.write_bytes(b"OLD-RVF")
            companion = cv.companion_wav_path(target)
            companion.mkdir()
            with self.assertRaises(RuntimeError):
                cv.convert(self.sync, target, 8, 8, 10, audio=False)
            self.assertEqual(target.read_bytes(), b"OLD-RVF")
            self.assertTrue(companion.is_dir())
            self.assertEqual(list(root.glob("*.new")), [])


if __name__ == "__main__":
    unittest.main()
