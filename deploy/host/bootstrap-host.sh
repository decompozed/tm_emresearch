#!/bin/bash
# Host baseline for the ToyMayhem VPS (Ubuntu 24.04). Safe to run again: every step checks first.
#
# DRY RUN BY DEFAULT: without --apply it only prints what it would change and changes nothing.
#
#   sudo ./bootstrap-host.sh                                     dry run, prints the plan
#   sudo ./bootstrap-host.sh --apply --admin-pubkey-file ~/admin.pub --ci-pubkey-file ~/ci.pub
#   sudo ./bootstrap-host.sh --apply --lock-ssh-to-access        second run, after Access SSH was tested
#
# What it does
#   * admin user (sudo, key only) and deploy user (no shell login except one forced command, docker group)
#   * sshd: keys only, no root login, only those two users
#   * SSH through Cloudflare Access: host cloudflared runs a second tunnel for ssh://localhost:22
#   * ufw: deny inbound, allow outbound, allow only the game ports (and SSH on 22 until --lock-ssh-to-access)
#   * unattended-upgrades, fail2ban, time sync (systemd-timesyncd), Docker from Docker's own apt repository, cloudflared
#   * folders: /opt/toymayhem, secrets (root only), logs for the Blue team (group blueteam, read only), backup timer
#
# Lock-out protection: port 22 stays open (rate limited) until you run with --lock-ssh-to-access, which you do only after
# "ssh" through Cloudflare Access worked. If you lock yourself out anyway, use the console of the VPS provider (README.md).
set -euo pipefail

apply=0
lock_ssh=0
admin_user=tmadmin
deploy_user=deploy
admin_pubkey_file=
ci_pubkey_file=
game_ports=(13000 13005 13006)       # TCP: AuthServer, MainServer, CastServer. 13001 is never opened.
base=/opt/toymayhem
logs=/srv/toymayhem/logs
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
deploy_dir=$(dirname "$here")        # the deploy/ folder of the repository

# Docker's apt signing key, fingerprint as published at https://docs.docker.com/engine/install/ubuntu/
docker_key_fpr="9DC858229FC7DD38854AE2D88D81803C0EBFCD88"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --apply) apply=1; shift ;;
        --lock-ssh-to-access) lock_ssh=1; shift ;;
        --admin-user) admin_user=${2:?}; shift 2 ;;
        --deploy-user) deploy_user=${2:?}; shift 2 ;;
        --admin-pubkey-file) admin_pubkey_file=${2:?}; shift 2 ;;
        --ci-pubkey-file) ci_pubkey_file=${2:?}; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

mode="DRY RUN (nothing is changed, add --apply to change)"
((apply)) && mode="APPLY"
echo "bootstrap-host.sh: $mode"

if ((apply)); then
    [[ $EUID -eq 0 ]] || { echo "--apply needs root (use sudo)" >&2; exit 1; }
    . /etc/os-release
    [[ "${ID:-}" == ubuntu && "${VERSION_ID:-}" == 24.04 ]] || { echo "this script is for Ubuntu 24.04, found ${PRETTY_NAME:-unknown}" >&2; exit 1; }
    [[ -n "$admin_pubkey_file" && -s "$admin_pubkey_file" ]] || { echo "--apply needs --admin-pubkey-file with your SSH public key (otherwise nobody could log in after the SSH step)" >&2; exit 1; }
    if [[ -n "$ci_pubkey_file" && ! -s "$ci_pubkey_file" ]]; then echo "--ci-pubkey-file is empty or missing" >&2; exit 1; fi
fi

step() { echo; echo "== $*"; }
say()  { echo "   $*"; }

# run CMD...: print it, run it only with --apply
run() {
    if ((apply)); then
        say "+ $*"
        "$@"
    else
        say "would run: $*"
    fi
}

# write_file PATH MODE OWNER:GROUP  (content on stdin): replaces the file only when the content differs
write_file() {
    local path=$1 fmode=$2 owner=$3 tmp
    tmp=$(mktemp)
    cat > "$tmp"
    if [[ -f "$path" ]] && cmp -s "$tmp" "$path"; then
        say "unchanged: $path"
        rm -f "$tmp"
        return
    fi
    if ((apply)); then
        install -D -m "$fmode" -o "${owner%%:*}" -g "${owner##*:}" "$tmp" "$path"
        say "written: $path"
    else
        say "would write: $path (mode $fmode, owner $owner)"
        sed 's/^/        | /' "$tmp"
    fi
    rm -f "$tmp"
}

pkg_installed() { dpkg -s "$1" >/dev/null 2>&1; }
need_pkgs() {
    local missing=() p
    for p in "$@"; do pkg_installed "$p" || missing+=("$p"); done
    if ((${#missing[@]})); then
        run env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
    else
        say "packages already installed: $*"
    fi
}

# ---------------------------------------------------------------------------------------------------------------------
step "1. Packages and system updates"
run apt-get update -qq
need_pkgs ca-certificates curl gnupg ufw fail2ban unattended-upgrades apt-listchanges rclone gnupg sudo
if ((apply)); then DEBIAN_FRONTEND=noninteractive apt-get upgrade -y -qq >/dev/null; say "system upgraded"; else say "would run: apt-get upgrade -y"; fi

# ---------------------------------------------------------------------------------------------------------------------
step "2. Time sync and timezone (UTC, so log lines of the three servers and the backups line up)"
run timedatectl set-timezone UTC
run timedatectl set-ntp true
if systemctl is-active --quiet systemd-timesyncd 2>/dev/null; then say "systemd-timesyncd is active"; else run systemctl enable --now systemd-timesyncd; fi

# ---------------------------------------------------------------------------------------------------------------------
step "3. Users: $admin_user (admin, key only) and $deploy_user (CI, forced command)"
if id "$admin_user" >/dev/null 2>&1; then say "user exists: $admin_user"; else run useradd --create-home --shell /bin/bash --groups sudo "$admin_user"; fi
if id "$deploy_user" >/dev/null 2>&1; then say "user exists: $deploy_user"; else run useradd --create-home --shell /bin/bash "$deploy_user"; fi
# No password for either (key only). The admin gets sudo without a password prompt because there is no password to type.
write_file "/etc/sudoers.d/90-$admin_user" 0440 root:root <<EOF
$admin_user ALL=(ALL) NOPASSWD:ALL
EOF
if [[ -n "$admin_pubkey_file" ]]; then
    key=$(head -n1 "$admin_pubkey_file")
    write_file "/home/$admin_user/.ssh/authorized_keys" 0600 "$admin_user:$admin_user" <<EOF
$key
EOF
fi
if [[ -n "$ci_pubkey_file" ]]; then
    key=$(head -n1 "$ci_pubkey_file")
    # The CI key can only run the deploy script. No terminal, no forwarding, no other command.
    write_file "/home/$deploy_user/.ssh/authorized_keys" 0600 "$deploy_user:$deploy_user" <<EOF
command="$base/bin/tm-deploy.sh",no-port-forwarding,no-X11-forwarding,no-agent-forwarding,no-pty $key
EOF
else
    say "no --ci-pubkey-file: the deploy user gets no key yet (the pipeline deploy job stays unusable until you add one)"
fi
if getent group blueteam >/dev/null; then say "group exists: blueteam"; else run groupadd --system blueteam; fi
# tm-ssh: who may log in with a shell (admin, deploy). blueteam: read-only log access over SFTP, nothing else.
if getent group tm-ssh >/dev/null; then say "group exists: tm-ssh"; else run groupadd --system tm-ssh; fi
run usermod -aG tm-ssh "$admin_user"
run usermod -aG tm-ssh "$deploy_user"

# ---------------------------------------------------------------------------------------------------------------------
step "4. sshd: keys only, no root login"
# 00- sorts before 50-cloud-init.conf, and the first value wins in sshd_config
write_file /etc/ssh/sshd_config.d/00-toymayhem.conf 0644 root:root <<EOF
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
AuthenticationMethods publickey
AllowGroups tm-ssh blueteam
MaxAuthTries 3
LoginGraceTime 20
X11Forwarding no
AllowAgentForwarding no
PermitEmptyPasswords no
ClientAliveInterval 300
ClientAliveCountMax 2

# The Blue team can read the logs and nothing else: SFTP only, read only, no shell, no tunnels (so no way to the staff port)
Match Group blueteam
    ForceCommand internal-sftp -R
    AllowTcpForwarding no
    PermitTTY no
    X11Forwarding no
EOF
if ((apply)); then
    if sshd -t; then
        systemctl reload ssh && say "sshd reloaded" || systemctl reload sshd
    else
        echo "sshd -t failed, the config was NOT activated. Fix /etc/ssh/sshd_config.d/00-toymayhem.conf" >&2
        exit 1
    fi
else
    say "would check with: sshd -t, then reload ssh"
fi

# ---------------------------------------------------------------------------------------------------------------------
step "5. cloudflared (official Cloudflare apt repository) and the SSH tunnel through Cloudflare Access"
if [[ -f /usr/share/keyrings/cloudflare-main.gpg ]]; then say "cloudflare key present"; else
    run bash -c 'curl -fsSL https://pkg.cloudflare.com/cloudflare-main.gpg -o /usr/share/keyrings/cloudflare-main.gpg'
fi
write_file /etc/apt/sources.list.d/cloudflared.list 0644 root:root <<EOF
deb [signed-by=/usr/share/keyrings/cloudflare-main.gpg] https://pkg.cloudflare.com/cloudflared any main
EOF
if ! pkg_installed cloudflared; then run apt-get update -qq; fi
need_pkgs cloudflared
# The token of the SSH tunnel lives in a root-only file, not in the unit. Create the tunnel in the Cloudflare dashboard
# (Zero Trust, Networks, Tunnels), public hostname ssh.<domain> -> ssh://localhost:22, then:
#   install -m 0600 /dev/stdin /etc/toymayhem/cloudflared-ssh.token   (paste the token, Ctrl-D)
write_file /etc/systemd/system/cloudflared-ssh.service 0644 root:root <<EOF
[Unit]
Description=Cloudflare tunnel for SSH (protected by Cloudflare Access)
After=network-online.target
Wants=network-online.target
ConditionPathExists=/etc/toymayhem/cloudflared-ssh.token

[Service]
Type=simple
DynamicUser=yes
LoadCredential=token:/etc/toymayhem/cloudflared-ssh.token
ExecStart=/usr/bin/cloudflared --no-autoupdate --metrics 127.0.0.1:20242 tunnel run --token-file %d/token
Restart=on-failure
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
CapabilityBoundingSet=
RestrictAddressFamilies=AF_INET AF_INET6
ProtectKernelTunables=yes
ProtectControlGroups=yes
SystemCallFilter=@system-service

[Install]
WantedBy=multi-user.target
EOF
run install -d -m 0700 /etc/toymayhem
run systemctl daemon-reload
if [[ -s /etc/toymayhem/cloudflared-ssh.token ]]; then
    run systemctl enable --now cloudflared-ssh.service
else
    say "no /etc/toymayhem/cloudflared-ssh.token yet: the SSH tunnel is not started. Public SSH on port 22 stays open meanwhile."
fi

# ---------------------------------------------------------------------------------------------------------------------
step "6. Firewall (ufw): deny inbound, allow outbound, only the game ports"
# Docker publishes ports by its own iptables rules that come BEFORE ufw. So ufw does not filter the published game ports;
# the list of "ports:" in docker-compose.yml is what really decides, and it holds exactly the three game ports.
# ufw protects everything on the host itself (SSH, anything installed later).
run ufw default deny incoming
run ufw default allow outgoing
for p in "${game_ports[@]}"; do run ufw allow "$p/tcp" comment "ToyMayhem game port"; done
if ((lock_ssh)); then
    if ((apply)) && ! systemctl is-active --quiet cloudflared-ssh.service; then
        echo "refusing --lock-ssh-to-access: cloudflared-ssh.service is not running, you would lose SSH" >&2
        exit 1
    fi
    run bash -c "ufw delete limit 22/tcp || true"
    run bash -c "ufw delete allow 22/tcp || true"
    say "SSH is now reachable only through Cloudflare Access (outbound tunnel). Console fallback: README.md"
else
    run ufw limit 22/tcp comment "SSH until Access SSH is proven"
fi
run ufw logging low
run ufw --force enable

# ---------------------------------------------------------------------------------------------------------------------
step "7. fail2ban"
# Behind the Access tunnel every SSH login comes from 127.0.0.1, so localhost must never be banned.
write_file /etc/fail2ban/jail.d/toymayhem.local 0644 root:root <<EOF
[DEFAULT]
ignoreip = 127.0.0.1/8 ::1
bantime = 1h
findtime = 10m
maxretry = 4

[sshd]
enabled = true
backend = systemd
EOF
run systemctl enable --now fail2ban
run systemctl restart fail2ban

# ---------------------------------------------------------------------------------------------------------------------
step "8. Automatic security updates"
write_file /etc/apt/apt.conf.d/20auto-upgrades 0644 root:root <<EOF
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
APT::Periodic::AutocleanInterval "7";
EOF
# No automatic reboot during the hacking week; reboot by hand after kernel updates (README.md)
write_file /etc/apt/apt.conf.d/52toymayhem-unattended 0644 root:root <<EOF
Unattended-Upgrade::Automatic-Reboot "false";
Unattended-Upgrade::Remove-Unused-Dependencies "true";
EOF
run systemctl enable --now unattended-upgrades

# ---------------------------------------------------------------------------------------------------------------------
step "9. Docker Engine (Docker's official apt repository)"
if pkg_installed docker-ce; then say "docker-ce already installed"; else
    run install -d -m 0755 /etc/apt/keyrings
    run bash -c 'curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc && chmod a+r /etc/apt/keyrings/docker.asc'
    if ((apply)); then
        got=$(gpg --show-keys --with-colons /etc/apt/keyrings/docker.asc | awk -F: '$1=="fpr"{print $10; exit}')
        [[ "$got" == "$docker_key_fpr" ]] || { echo "Docker apt key fingerprint is $got, expected $docker_key_fpr. Stopping." >&2; exit 1; }
        say "Docker key fingerprint verified"
    fi
fi
write_file /etc/apt/sources.list.d/docker.list 0644 root:root <<EOF
deb [arch=amd64 signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu noble stable
EOF
if ! pkg_installed docker-ce; then run apt-get update -qq; fi
need_pkgs docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
# live-restore keeps containers running while the Docker daemon restarts for an update
write_file /etc/docker/daemon.json 0644 root:root <<EOF
{
  "log-driver": "json-file",
  "log-opts": { "max-size": "10m", "max-file": "3" },
  "live-restore": true,
  "no-new-privileges": true
}
EOF
run systemctl enable --now docker
run systemctl reload docker
run usermod -aG docker "$deploy_user"
# The admin works with sudo; membership of the docker group is root-equivalent, so the admin does not get it.

# ---------------------------------------------------------------------------------------------------------------------
step "10. Folders, deploy script, settings file"
run install -d -m 0755 -o root -g root "$base" "$base/bin"
# Secret files are mode 0444 inside this folder; only root and the docker group (root-equivalent anyway) can enter it
run install -d -m 0710 -o root -g docker "$base/secrets"
run install -d -m 0750 -o root -g root /etc/toymayhem
# Logs of the three servers (TB_LOG_DIR). The containers run as uid 10001; the Blue team reads through the group blueteam.
run install -d -m 2750 -o 10001 -g blueteam "$(dirname "$logs")" "$logs"
if [[ -f "$deploy_dir/host/tm-deploy.sh" ]]; then run install -m 0755 -o root -g root "$deploy_dir/host/tm-deploy.sh" "$base/bin/tm-deploy.sh"; fi
for f in tm-backup.sh tm-restore.sh; do
    if [[ -f "$deploy_dir/db-backup/$f" ]]; then run install -m 0750 -o root -g root "$deploy_dir/db-backup/$f" "$base/bin/$f"; fi
done
# The deploy user replaces these files on every deploy, in its own folder (see host/tm-deploy.sh)
run install -d -m 0755 -o "$deploy_user" -g "$deploy_user" "$base/release"
if [[ -f "$deploy_dir/docker-compose.yml" && -f "$deploy_dir/../microvolts-db.sql" ]]; then
    run install -m 0644 -o "$deploy_user" -g "$deploy_user" "$deploy_dir/docker-compose.yml" "$base/release/docker-compose.yml"
    run install -m 0644 -o "$deploy_user" -g "$deploy_user" "$deploy_dir/../microvolts-db.sql" "$base/release/microvolts-db.sql"
    run install -D -m 0644 -o "$deploy_user" -g "$deploy_user" "$deploy_dir/db-init/10-schema.sh" "$base/release/db-init/10-schema.sh"
fi
if [[ ! -f "$base/tm.env" && -f "$deploy_dir/tm.env.example" ]]; then
    run install -m 0644 -o root -g root "$deploy_dir/tm.env.example" "$base/tm.env"
    say "edit $base/tm.env (TM_DOMAIN) before the first start"
fi

# ---------------------------------------------------------------------------------------------------------------------
step "11. Nightly backup timer"
for f in tm-backup.service tm-backup.timer; do
    if [[ -f "$deploy_dir/db-backup/$f" ]]; then run install -m 0644 -o root -g root "$deploy_dir/db-backup/$f" "/etc/systemd/system/$f"; fi
done
run systemctl daemon-reload
if [[ -f /etc/toymayhem/backup.env && -f /etc/toymayhem/backup-public.asc ]]; then
    run systemctl enable --now tm-backup.timer
else
    say "backup not configured yet (/etc/toymayhem/backup.env and backup-public.asc missing): timer NOT enabled. See README.md."
fi

echo
if ((apply)); then
    echo "Done. Next: README.md, section 'First deploy'."
else
    echo "Dry run finished, nothing was changed."
fi
