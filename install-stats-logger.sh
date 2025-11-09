#!/bin/bash
set -e

# -----------------------------
# Configuration
# -----------------------------
GITHUB_URL="https://cdn.statically.io/gh/CovertCode/server-logger/main/stats_logger"
SERVICE_NAME="stats_logger"
INSTALL_PATH="/usr/local/bin/$SERVICE_NAME"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
DEFAULT_ENDPOINT="http://152.53.50.193:14782/system-stats"

# -----------------------------
# Prompt user for inputs
# -----------------------------
echo "=== System Stats Logger Installer ==="
read -rp "Enter server name (e.g., in1): " SERVER_NAME
read -rp "Enter API key: " API_KEY
read -rp "Enter endpoint [default: ${DEFAULT_ENDPOINT}]: " ENDPOINT
ENDPOINT=${ENDPOINT:-$DEFAULT_ENDPOINT}

echo ""
echo "Server Name : $SERVER_NAME"
echo "API Key     : $API_KEY"
echo "Endpoint    : $ENDPOINT"
echo ""

# Confirm before proceeding
read -rp "Proceed with installation? (y/n): " confirm
[[ $confirm =~ ^[Yy]$ ]] || { echo "Aborted."; exit 1; }

# -----------------------------
# Download latest binary
# -----------------------------
echo "→ Downloading latest stats_logger binary..."
wget -q -O "$INSTALL_PATH" "$GITHUB_URL"
chmod +x "$INSTALL_PATH"
echo "✓ Installed binary to $INSTALL_PATH"

# -----------------------------
# Create systemd service file
# -----------------------------
echo "→ Creating systemd service..."
cat <<EOF | sudo tee "$SERVICE_FILE" >/dev/null
[Unit]
Description=Lightweight System Stats Logger
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_PATH} ${ENDPOINT} ${SERVER_NAME} ${API_KEY}
Restart=always
RestartSec=5
User=root
WorkingDirectory=/root
StandardOutput=null
StandardError=null

[Install]
WantedBy=multi-user.target
EOF

# -----------------------------
# Enable + Start service
# -----------------------------
echo "→ Enabling and starting service..."
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl restart "$SERVICE_NAME"

# -----------------------------
# Final status
# -----------------------------
sleep 2
echo ""
sudo systemctl status "$SERVICE_NAME" --no-pager | grep -E "Active|ExecStart"
echo ""
echo "✅ Installation complete!"
echo "Stats logger is running as a service and sending data to:"
echo "   $ENDPOINT"
echo ""
