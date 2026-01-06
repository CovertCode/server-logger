#!/bin/bash
set -e

# -----------------------------
# Configuration
# -----------------------------
BINARY_URL="https://github.com/CovertCode/server-logger/raw/refs/heads/main/stats_logger"
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
  exit 1
fi

ENDPOINT=${ENDPOINT:-$DEFAULT_ENDPOINT}

# -----------------------------
# Install tools (wget, curl)
# -----------------------------
info "Installing dependencies..."
sudo apt update -y
sudo apt install -y wget curl >/dev/null 2>&1 || true
success "Dependencies installed."

# -----------------------------
# Cleanup existing installation
# -----------------------------
info "Checking for previous versions..."

# Stop and disable the service if it exists
if systemctl list-units --full -all | grep -Fq "$SERVICE_NAME.service"; then
    info "Stopping existing service..."
    sudo systemctl stop "$SERVICE_NAME" 2>/dev/null || true
    sudo systemctl disable "$SERVICE_NAME" 2>/dev/null || true
fi

# Remove the service file
if [ -f "$SERVICE_FILE" ]; then
    info "Removing existing service file..."
    sudo rm -f "$SERVICE_FILE"
    sudo systemctl daemon-reload
fi

# Remove the binary
if [ -f "$INSTALL_PATH" ]; then
    info "Removing existing binary..."
    sudo rm -f "$INSTALL_PATH"
fi

success "Clean slate prepared."

# -----------------------------
# Download universal static binary
# -----------------------------
info "Downloading stats_logger binary..."
sudo wget -q -O "$INSTALL_PATH" "$BINARY_URL" || error_exit "Failed to download binary."
sudo chmod +x "$INSTALL_PATH"
success "Binary installed at $INSTALL_PATH"

# -----------------------------
# Increase inotify limits safely
# -----------------------------
info "Applying inotify limit fixes..."

grep -q "fs.inotify.max_user_watches" /etc/sysctl.conf \
  && sudo sed -i 's/^fs\.inotify\.max_user_watches=.*/fs.inotify.max_user_watches=524288/' /etc/sysctl.conf \
  || echo "fs.inotify.max_user_watches=524288" | sudo tee -a /etc/sysctl.conf >/dev/null

grep -q "fs.inotify.max_user_instances" /etc/sysctl.conf \
  && sudo sed -i 's/^fs\.inotify\.max_user_instances=.*/fs.inotify.max_user_instances=1024/' /etc/sysctl.conf \
  || echo "fs.inotify.max_user_instances=1024" | sudo tee -a /etc/sysctl.conf >/dev/null

sudo sysctl -p >/dev/null
success "Inotify limits applied."

# -----------------------------
# Create systemd service
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

success "Service file created."

# -----------------------------
# Enable + Start service
# -----------------------------
info "Starting systemd service..."
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl restart "$SERVICE_NAME"

# -----------------------------
# Verify service
# -----------------------------
sleep 1
if systemctl is-active --quiet "$SERVICE_NAME"; then
  success "Service is running."
else
  error_exit "Service failed. Check with: sudo journalctl -u $SERVICE_NAME -xe"
fi

info "Final status:"
sudo systemctl status "$SERVICE_NAME" --no-pager | grep -E "Active|ExecStart"

echo ""
success "🎉 Installation complete! Stats logger is now running."
echo "→ Endpoint: $ENDPOINT"
echo "→ Server:   $SERVER_NAME"
echo ""