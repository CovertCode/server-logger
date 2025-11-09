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
# Helper functions
# -----------------------------
function info()    { echo -e "\033[1;34m→\033[0m $1"; }
function success() { echo -e "\033[1;32m✓\033[0m $1"; }
function error_exit() { echo -e "\033[1;31m✗ $1\033[0m"; exit 1; }

# -----------------------------
# Parse arguments
# -----------------------------
SERVER_NAME="$1"
API_KEY="$2"
ENDPOINT="$3"

if [[ -z "$SERVER_NAME" || -z "$API_KEY" ]]; then
  echo "Usage: $0 <server_name> <api_key> [endpoint]"
  echo "Example:"
  echo "  bash <(wget -qO- https://cdn.statically.io/gh/CovertCode/server-logger/main/install-stats-logger.sh) us6 Aspirate9-Decoy-Getaway-Net"
  exit 1
fi

ENDPOINT=${ENDPOINT:-$DEFAULT_ENDPOINT}

# -----------------------------
# Install dependencies
# -----------------------------
info "Updating apt and installing dependencies..."
sudo apt update -y
sudo apt install -y wget curl libmbedtls-dev || error_exit "Failed to install dependencies"
success "Dependencies installed (wget, curl, libmbedtls-dev)"

# -----------------------------
# Download binary
# -----------------------------
info "Downloading latest stats_logger binary..."
sudo wget -q -O "$INSTALL_PATH" "$GITHUB_URL" || error_exit "Failed to download binary"
sudo chmod +x "$INSTALL_PATH"
success "Installed binary to $INSTALL_PATH"

# -----------------------------
# Increase inotify limits safely
# -----------------------------
info "Adjusting inotify system limits..."
if ! grep -q "fs.inotify.max_user_watches" /etc/sysctl.conf; then
  echo "fs.inotify.max_user_watches=524288" | sudo tee -a /etc/sysctl.conf >/dev/null
else
  sudo sed -i 's/^fs\.inotify\.max_user_watches=.*/fs.inotify.max_user_watches=524288/' /etc/sysctl.conf
fi

if ! grep -q "fs.inotify.max_user_instances" /etc/sysctl.conf; then
  echo "fs.inotify.max_user_instances=1024" | sudo tee -a /etc/sysctl.conf >/dev/null
else
  sudo sed -i 's/^fs\.inotify\.max_user_instances=.*/fs.inotify.max_user_instances=1024/' /etc/sysctl.conf
fi

sudo sysctl -p >/dev/null
success "Inotify limits updated and applied."

# -----------------------------
# Create systemd service file
# -----------------------------
info "Creating systemd service..."
sudo tee "$SERVICE_FILE" >/dev/null <<EOF
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
success "Service file created at $SERVICE_FILE"

# -----------------------------
# Enable + Start service
# -----------------------------
info "Enabling and starting service..."
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl restart "$SERVICE_NAME"

# -----------------------------
# Verify status
# -----------------------------
sleep 2
if systemctl is-active --quiet "$SERVICE_NAME"; then
  success "Service is running successfully!"
else
  error_exit "Service failed to start. Check: sudo journalctl -u $SERVICE_NAME -xe"
fi

info "Final status:"
sudo systemctl status "$SERVICE_NAME" --no-pager | grep -E "Active|ExecStart" || true
echo ""
success "✅ Installation complete!"
echo "Stats logger is running and sending data to:"
echo "   ${ENDPOINT}"
echo "Server Name: ${SERVER_NAME}"
echo ""
