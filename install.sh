#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

usage() {
  cat <<'HELP'
Usage: ./install.sh [--configure-only] [--help]

Interactive setup for Heltec Wireless Paper V1.1/V1.2 or Wireless Tracker V1.1.
Choose the board, USB serial port, and optional station Wi-Fi connection.
The script creates a local Python environment, installs tools, builds firmware,
offers a private flash backup, and asks before uploading to the selected board.

--configure-only  Save private Wi-Fi settings without installing tools or flashing.
Wi-Fi passwords are hidden at entry and never placed in command arguments.
Credentials, backups, and personal firmware builds stay local and must not be shared.
Linux/macOS require Python 3.10+ with venv support. No system packages or sudo are used.
On Windows, use the documented PlatformIO commands or run Bash in a suitable setup.

Wireless Paper API by Ramazan Yavuz. Provided as is, without warranty of any kind.
You accept all risk from installation and use.
HELP
}

configure_only=false
case "${1:-}" in
  --help|-h) usage; exit 0 ;;
  --configure-only) configure_only=true ;;
  '') ;;
  *) usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then usage >&2; exit 2; fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
cd -- "$root"
command -v python3 >/dev/null || { echo 'Install Python 3 with venv support, then rerun this script.' >&2; exit 1; }
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' || { echo 'Python 3.10 or newer is required.' >&2; exit 1; }
trap 'unset wifi_password wifi_confirm; echo "Installation stopped. Existing flash backups are kept in .local/backups/." >&2' ERR
trap 'unset wifi_password wifi_confirm; exit 130' INT TERM

ask_yes() {
  local answer
  read -r -p "$1" answer || { echo 'No input received; stopping.' >&2; exit 1; }
  case "$answer" in y|Y|yes|YES) return 0;; '') [[ "$2" == yes ]];; *) return 1;; esac
}

configure_wifi() {
  local detected_ssid='' wifi_ssid='' wifi_password='' wifi_confirm='' keep=false
  if [[ -f include/secrets.h ]]; then
    if ask_yes 'Keep the existing private Wi-Fi settings? [Y/n] ' yes; then keep=true; fi
  fi
  if "$keep"; then echo 'Keeping include/secrets.h.'; return; fi
  if command -v nmcli >/dev/null; then
    detected_ssid=$(python3 - <<'PY'
import subprocess
try:
    active=subprocess.check_output(['nmcli','-t','-f','UUID,TYPE','connection','show','--active'],text=True,stderr=subprocess.DEVNULL)
    for line in active.splitlines():
        uuid,kind=line.split(':',1)
        if kind in ('802-11-wireless','wifi'):
            name=subprocess.check_output(['nmcli','--escape','no','-g','802-11-wireless.ssid','connection','show','uuid',uuid],text=True,stderr=subprocess.DEVNULL).rstrip('\n')
            print(name,end='');break
except (OSError,subprocess.SubprocessError,ValueError):
    pass
PY
)
  fi
  while true; do
    wifi_ssid=''; wifi_password=''
    if ask_yes 'Automatically join a Wi-Fi network after startup? [Y/n] ' yes; then
      if [[ -n "$detected_ssid" ]]; then
        read -r -p "Wi-Fi network name [$detected_ssid]: " wifi_ssid
        wifi_ssid=${wifi_ssid:-$detected_ssid}
      else
        read -r -p 'Wi-Fi network name: ' wifi_ssid
      fi
      if [[ -z "$wifi_ssid" ]]; then echo 'Enter a network name, or choose hotspot-only setup.'; continue; fi
      read -r -s -p 'Wi-Fi password (leave empty for an open network): ' wifi_password; printf '\n'
      read -r -s -p 'Confirm password: ' wifi_confirm; printf '\n'
      if [[ "$wifi_password" != "$wifi_confirm" ]]; then echo 'Passwords do not match. Try again.'; continue; fi
    fi
    if [[ -f include/secrets.h ]]; then
      mkdir -p .local/backups
      cp -p -- include/secrets.h ".local/backups/secrets-$(date -u +%Y%m%dT%H%M%SZ)-$$.h"
      chmod 600 .local/backups/secrets-*-"$$".h
    fi
    if printf '%s\0%s\0' "$wifi_ssid" "$wifi_password" | python3 tools/wifi_config.py include/secrets.h; then break; fi
    echo 'The previous configuration was kept. Please enter valid settings.'
  done
  unset wifi_password wifi_confirm
  echo 'Saved private settings in include/secrets.h (owner access only).'
}

configure_wifi
if "$configure_only"; then echo 'Configuration saved. No firmware was built or uploaded.'; exit 0; fi

printf '\nBoard to install:\n  1) Wireless Paper V1.1\n  2) Wireless Tracker V1.1 (160 x 80 TFT)\n  3) Wireless Paper V1.1.1 / V1.2\n'
while true; do
  read -r -p 'Board [1]: ' choice
  case "${choice:-1}" in
    1) environment=paper; break;;
    2) environment=tracker; break;;
    3) environment=paper12; break;;
    *) echo 'Choose 1, 2, or 3.';;
  esac
done

if [[ ! -x .venv/bin/python ]]; then python3 -m venv .venv; fi
python="$root/.venv/bin/python"
"$python" -m pip install --disable-pip-version-check -r requirements.txt
ports=()
while IFS= read -r item; do ports+=("$item"); done < <("$python" - <<'PY'
from serial.tools import list_ports
for port in sorted(list_ports.comports(),key=lambda p:p.device):
    print(port.device+' | '+port.description)
PY
)
printf '\nDetected serial ports:\n'
if (( ${#ports[@]} )); then
  for (( i=0; i<${#ports[@]}; i++ )); do printf '  %s) %s\n' "$((i+1))" "${ports[$i]}"; done
else
  echo '  None detected. Connect a USB data cable or enter a port path.'
fi
while true; do
  read -r -p 'Port number or full serial port path: ' selected
  if [[ "$selected" =~ ^[0-9]+$ && ${#selected} -le 3 ]]; then
    index=$((10#$selected-1))
    if (( index >= 0 && index < ${#ports[@]} )); then port=${ports[$index]%% | *}; break; fi
  elif [[ -n "$selected" ]]; then port=$selected; break
  fi
  echo 'Select an available port or enter its path.'
done

echo "Building $environment firmware. This private build may contain Wi-Fi credentials."
"$python" -m platformio run -e "$environment"
if ask_yes 'Save a private backup of the existing board flash first? [Y/n] ' yes; then
  mkdir -p .local/backups
  backup=$(mktemp -d "$root/.local/backups/$environment-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")
  "$python" -m esptool --port "$port" read-flash 0 ALL "$backup/original-flash.bin"
  "$python" - "$backup" <<'PY'
from pathlib import Path
import hashlib,sys
folder=Path(sys.argv[1]);path=folder/'original-flash.bin';path.chmod(0o600)
(folder/'original-flash.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  original-flash.bin\n')
PY
  echo "Backup saved in $backup"
fi
printf '\nReady: environment %s, USB port %s.\n' "$environment" "$port"
if ! ask_yes "Replace this board's firmware now? [y/N] " no; then echo 'Build and private settings kept. Nothing uploaded.'; exit 0; fi
"$python" -m platformio run -e "$environment" -t upload --upload-port "$port"
printf '\nInstallation complete. Read the screen for the hotspot name.\n'
echo 'Join it and open http://192.168.4.1/ on port 80.'
echo 'The hotspot opens for 60 minutes, stays open for connected clients, and has a saved always-on option.'
echo 'The web UI links to /api. Fresh devices start with LoRa and TX disabled.'
echo 'Existing saved radio and hotspot settings survive firmware updates.'
