# Optional AI backend template

This directory contains a deployable example backend for the ESP32 AI features. It has no private deployment data, server address, API key, PM2 dump, or generated audio. Configure every deployment value in a local `.env` file; `.env` is ignored by Git.

The Node.js service exposes a browser page, `GET /api/health`, `POST /api/chat`, `WS /esp32`, and generated WAV files under `/audio/`. It calls the configured DeepSeek-compatible endpoint, runs the configured edge-tts script, and converts output to 16000 Hz mono PCM16 WAV for the device.

## Configuration

Copy `.env.example` to `.env` and replace every placeholder. Required values are `DEEPSEEK_API_KEY`, `DEEPSEEK_API_URL`, `PUBLIC_BASE_URL`, and `TTS_SCRIPT_PATH`; `PORT` defaults to 3000.

The firmware uses optional ignored `src/private_config.h`. Copy `src/private_config.h.example` to `src/private_config.h` and fill local values. Public defaults leave AI and weather unconfigured until the developer supplies local settings.

## Local deployment

```bash
npm ci
python3 -m pip install -r requirements.txt
cp .env.example .env
# edit .env with local values
npm start
```

Use `ecosystem.config.cjs` as a generic PM2 template and replace its working directory for the target host. Keep `.env`, SSH credentials, PM2 dumps, and generated audio outside Git.

## Interfaces

- `GET /` serves the browser chat page.
- `GET /api/health` reports service and connected ESP32 clients.
- `POST /api/chat` accepts `{ "message": "你好" }` and returns text plus a WAV URL.
- `WS /esp32` receives broadcast `ai_reply` messages.
- `GET /audio/<id>.wav` serves generated 16000 Hz mono PCM16 audio.

