minimp3 upstream: https://github.com/lieff/minimp3
Vendored from master on 2026-11-14; immutable upstream file SHA-256 checksums:
{
  "minimp3.h": "57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad",
  "LICENSE": "6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01"
}
License: CC0-1.0 (LICENSE).
Local patches: MINIMP3_SCRATCH_STORAGE allows an explicit storage class for scratch. MINIMP3_SCRATCH_POINTER optionally supplies instance-owned scratch; Mp3Source uses this to keep playback and WAV conversion independently reentrant with scratch in PSRAM. Both hooks default to upstream automatic storage when unused. No decoder algorithm changes.
