# Security

Do not commit WiFi passwords, weather or AI API keys, server addresses that should remain private, SSH credentials, `.env` files, PM2 dumps, device logs, or generated audio. Use the ignored `src/private_config.h` and `server/.env` files for local configuration.

If a credential is exposed, revoke or rotate it first, then remove it from the repository history. For a private disclosure about this project, contact the repository owner through GitHub rather than opening a public issue.

