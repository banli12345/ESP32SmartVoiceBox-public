// Reconstructed from the running esp32-ai application's PM2 launch metadata.
// Credentials are loaded by dotenv from /srv/esp32-smart-voice-box/.env.
module.exports = {
  apps: [
    {
      name: "esp32-ai",
      cwd: "/srv/esp32-smart-voice-box",
      script: "server.mjs",
      interpreter: "/usr/bin/node",
      exec_mode: "fork",
      instances: 1,
      autorestart: true,
      watch: false
    }
  ]
};
