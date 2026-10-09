# Device information development checkpoint

Local development based on v1.4, saved before implementing the web browser.

- Settings includes a device information page for ESP32-S3 N16R8, firmware version, CPU cores/frequency, Flash, internal heap, PSRAM and LCD refresh rate.
- Capacities show used / total. Flash usage is the running firmware image size; it excludes other Flash data and reserved partitions.
- Firmware size is cached once. Memory values refresh every two seconds while the page is visible.
- Backlight diagnostics record the LEDC attachment, write result, duty and frequency. Display timing and the brightness mapping remain at the existing settings.

Validation on 2026-10-08:

- PlatformIO build and COM3 upload passed; written data hash verified.
- Firmware SHA-256: `62d19ded8bae9cb52c24ed9452101081dcf65b51faca0d48d05c2e1b505a6550` (1,893,088 bytes).
- The opened page logged ESP32-S3 revision 0.2, two cores at 240 MHz, 16 MiB Flash and 8 MiB PSRAM.
- Sample capacities: Flash 1.81 / 16.00 MiB; PSRAM 7.23 / 8.00 MiB. Memory usage varies with active features.
- A 30-second boot/page log check reported no captured fault; LCD refresh was approximately 35–36 FPS.
- New Chinese text was checked against the SD-backed 20px font. Complete visual layout and return navigation still need user confirmation.

This is a development checkpoint, not a new numbered release. Published v1.4 remains unchanged. Private logs, credentials and runtime SD assets remain excluded from Git.

