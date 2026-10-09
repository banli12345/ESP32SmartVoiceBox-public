import "dotenv/config";
import express from "express";
import cors from "cors";
import fs from "node:fs/promises";
import path from "node:path";
import crypto from "node:crypto";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { WebSocketServer } from "ws";

const execFileAsync = promisify(execFile);
const app = express();
const port = Number(process.env.PORT || 3000);
const publicBaseUrl = String(process.env.PUBLIC_BASE_URL || "").replace(/\/+$/, "");
const deepseekApiUrl = String(process.env.DEEPSEEK_API_URL || "");
const ttsScriptPath = String(process.env.TTS_SCRIPT_PATH || "");
const audioDirectory = path.resolve("./public/audio");

if (!process.env.DEEPSEEK_API_KEY || !publicBaseUrl ||
    !deepseekApiUrl || !ttsScriptPath) {
  throw new Error(
    "请配置 DEEPSEEK_API_KEY、PUBLIC_BASE_URL、DEEPSEEK_API_URL 和 TTS_SCRIPT_PATH"
  );
}

await fs.mkdir(audioDirectory, { recursive: true });

app.use(cors());
app.use(express.json({ limit: "1mb" }));
app.use(express.static("public"));
app.use("/audio", express.static(audioDirectory));

const httpServer = app.listen(port, "0.0.0.0", () => {
  console.log(`AI server listening on port ${port}`);
});

const webSocketServer = new WebSocketServer({
  server: httpServer,
  path: "/esp32"
});

const esp32Clients = new Set();

webSocketServer.on("connection", socket => {
  esp32Clients.add(socket);
  console.log(`ESP32 connected, clients=${esp32Clients.size}`);

  socket.on("close", () => {
    esp32Clients.delete(socket);
    console.log(`ESP32 disconnected, clients=${esp32Clients.size}`);
  });

  socket.on("error", error => {
    console.error("WebSocket error:", error.message);
    esp32Clients.delete(socket);
  });
});

function broadcastToEsp32(message) {
  const payload = JSON.stringify(message);

  for (const socket of esp32Clients) {
    if (socket.readyState === 1) {
      socket.send(payload);
    }
  }

  console.log(`Broadcasted reply to ${esp32Clients.size} ESP32 client(s)`);
}

app.get("/api/health", (req, res) => {
  res.json({
    ok: true,
    service: "deepseek-edge-tts-websocket",
    esp32_clients: esp32Clients.size
  });
});

app.post("/api/chat", async (req, res) => {
  let textFile;
  let mp3File;
  let wavFile;

  try {
    const message = String(req.body?.message || "").trim();

    if (!message) {
      return res.status(400).json({
        ok: false,
        error: "message 不能为空"
      });
    }

    console.log("Web chat request:", message);

    const deepseekResponse = await fetch(
      deepseekApiUrl,
      {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Authorization": `Bearer ${process.env.DEEPSEEK_API_KEY}`
        },
        body: JSON.stringify({
          model: "deepseek-chat",
          messages: [
            {
              role: "system",
              content: "你是运行在 ESP32 智能设备上的中文助手。回答简洁，最多三句话。"
            },
            {
              role: "user",
              content: message
            }
          ],
          temperature: 0.7,
          stream: false
        })
      }
    );

    const deepseekData = await deepseekResponse.json();

    if (!deepseekResponse.ok) {
      throw new Error(
        deepseekData.error?.message || "DeepSeek 请求失败"
      );
    }

    const text = deepseekData.choices?.[0]?.message?.content?.trim();

    if (!text) {
      throw new Error("DeepSeek 没有返回文字");
    }

    const id = crypto.randomUUID();

    textFile = path.join(audioDirectory, `${id}.txt`);
    mp3File = path.join(audioDirectory, `${id}.mp3`);
    wavFile = path.join(audioDirectory, `${id}.wav`);

    await fs.writeFile(textFile, text, "utf8");

    await execFileAsync("python3", [
      ttsScriptPath,
      textFile,
      mp3File
    ]);

    await execFileAsync("ffmpeg", [
      "-y",
      "-i",
      mp3File,
      "-ar",
      "16000",
      "-ac",
      "1",
      "-c:a",
      "pcm_s16le",
      wavFile
    ]);

    const audioUrl = `${publicBaseUrl}/audio/${id}.wav`;

    const result = {
      ok: true,
      type: "ai_reply",
      question: message,
      text,
      audio_url: audioUrl,
      audio_format: "wav",
      sample_rate: 16000,
      channels: 1,
      bits_per_sample: 16
    };

    broadcastToEsp32(result);

    await fs.rm(textFile, { force: true });
    await fs.rm(mp3File, { force: true });

    res.json(result);
  } catch (error) {
    console.error("Chat error:", error);

    await Promise.all(
      [textFile, mp3File, wavFile]
        .filter(Boolean)
        .map(file => fs.rm(file, { force: true }))
    );

    res.status(500).json({
      ok: false,
      error: error.message || "服务器处理失败"
    });
  }
});

setInterval(async () => {
  try {
    const files = await fs.readdir(audioDirectory);
    const now = Date.now();

    for (const file of files) {
      if (!file.endsWith(".wav")) continue;

      const filePath = path.join(audioDirectory, file);
      const stat = await fs.stat(filePath);

      if (now - stat.mtimeMs > 24 * 60 * 60 * 1000) {
        await fs.rm(filePath, { force: true });
        console.log("Deleted old audio:", file);
      }
    }
  } catch (error) {
    console.error("Audio cleanup error:", error.message);
  }
}, 60 * 60 * 1000);
