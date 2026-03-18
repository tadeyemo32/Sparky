#!/usr/bin/env bash
# =============================================================================
# SparkyServer — Linux VPS hardening setup
# Run as root on a fresh Ubuntu 22.04 / Debian 12 VPS.
#
# What this does:
#   1. Creates a dedicated low-privilege user (sparky)
#   2. Sets directory permissions so only that user can read the DB / key
#   3. Configures ufw firewall with rate limiting on port 7777
#   4. Installs the systemd service
#   5. Optionally hardens sshd
# =============================================================================
set -euo pipefail

# ---------- Config (edit before running) ----------
SERVER_USER="sparky"
SERVER_DIR="/opt/sparky"
BINARY="SparkyServer"
SERVICE_FILE="$(dirname "$0")/sparky.service"
# --------------------------------------------------

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[x]${NC} $*"; exit 1; }

[[ $EUID -ne 0 ]] && error "Run as root (sudo ./setup.sh)"

# ---- 1. Create server user ----
info "Creating low-privilege user: $SERVER_USER"
if id "$SERVER_USER" &>/dev/null; then
    warn "User $SERVER_USER already exists — skipping"
else
    useradd --system --no-create-home --shell /usr/sbin/nologin "$SERVER_USER"
fi

# ---- 2. Create server directory ----
info "Setting up $SERVER_DIR"
mkdir -p "$SERVER_DIR"
chown root:"$SERVER_USER" "$SERVER_DIR"
chmod 750 "$SERVER_DIR"

# If binary already present, set permissions
if [[ -f "$SERVER_DIR/$BINARY" ]]; then
    chown root:"$SERVER_USER" "$SERVER_DIR/$BINARY"
    chmod 550 "$SERVER_DIR/$BINARY"
else
    warn "$SERVER_DIR/$BINARY not found — copy it there before starting the service"
fi

# ---- 3. Key file permissions ----
KEY_FILE="$SERVER_DIR/sparky.key"
if [[ -f "$KEY_FILE" ]]; then
    info "Securing key file"
    chown "$SERVER_USER":"$SERVER_USER" "$KEY_FILE"
    chmod 400 "$KEY_FILE"
else
    warn "Key file not found at $KEY_FILE"
    warn "Generate one:  ./SparkyServer --gen-key > $KEY_FILE && chmod 400 $KEY_FILE"
    warn "               chown $SERVER_USER:$SERVER_USER $KEY_FILE"
fi

# DB file
DB_FILE="$SERVER_DIR/sparky.db"
if [[ -f "$DB_FILE" ]]; then
    chown "$SERVER_USER":"$SERVER_USER" "$DB_FILE"
    chmod 600 "$DB_FILE"
fi

# ---- 4. ufw firewall ----
info "Configuring ufw"
if ! command -v ufw &>/dev/null; then
    apt-get install -y ufw
fi

ufw --force reset
ufw default deny incoming
ufw default allow outgoing

# SSH — allow but rate-limit (6 attempts per 30 s)
ufw limit ssh

# SparkyServer port — allow but rate-limit at the ufw level.
# This is a coarse-grained per-IP throttle that complements
# the in-process RateLimiter (which is per-connection, not per-packet).
# ufw limit works at the connection level (6 new connections / 30 s).
ufw limit 7777/tcp comment "SparkyServer — rate limited"

ufw --force enable
ufw status verbose

# Optional: iptables rule to also limit new connections per IP more strictly
# (ufw limit is a simpler alias for the rule below)
# iptables -A INPUT -p tcp --dport 7777 -m conntrack --ctstate NEW \
#          -m recent --set
# iptables -A INPUT -p tcp --dport 7777 -m conntrack --ctstate NEW \
#          -m recent --update --seconds 60 --hitcount 15 -j DROP

# ---- 5. Install systemd service ----
info "Installing systemd service"
if [[ ! -f "$SERVICE_FILE" ]]; then
    error "Service file not found: $SERVICE_FILE — copy scripts/sparky.service first"
fi
cp "$SERVICE_FILE" /etc/systemd/system/sparky.service
sed -i "s|__SERVER_DIR__|$SERVER_DIR|g"  /etc/systemd/system/sparky.service
sed -i "s|__SERVER_USER__|$SERVER_USER|g" /etc/systemd/system/sparky.service
sed -i "s|__BINARY__|$BINARY|g"           /etc/systemd/system/sparky.service
systemctl daemon-reload
systemctl enable sparky
info "Service installed. Start with: systemctl start sparky"

# ---- 6. SSH hardening (optional) ----
read -rp "Harden sshd (disable root login, disable password auth)? [y/N] " ans
if [[ "${ans,,}" == "y" ]]; then
    info "Hardening sshd"
    sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin no/'       /etc/ssh/sshd_config
    sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config
    systemctl reload sshd
    warn "Password SSH disabled — ensure your SSH key is installed before logging out!"
fi

# ---- 7. Python admin tool deps ----
info "Installing Python admin dependencies"
if command -v pip3 &>/dev/null; then
    pip3 install sqlcipher3 2>/dev/null \
        || warn "sqlcipher3 install failed — run: apt install libsqlcipher-dev && pip3 install sqlcipher3"
fi

echo ""
info "Setup complete!"
echo ""
echo "  Next steps:"
echo "  1. Copy SparkyServer binary to $SERVER_DIR/"
echo "  2. Generate a DB key:  SparkyServer --gen-key > $SERVER_DIR/sparky.key"
echo "     chmod 400 $SERVER_DIR/sparky.key && chown $SERVER_USER:$SERVER_USER $SERVER_DIR/sparky.key"
echo "  3. Set env in service:  systemctl edit sparky  → add Environment=SPARKY_DB_KEYFILE=..."
echo "  4. Start:  systemctl start sparky"
echo "  5. Watch:  journalctl -fu sparky"
echo ""
