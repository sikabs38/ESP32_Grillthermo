#!/usr/bin/env bash
#
# Bootstrap des west-Workspace fuer ESP32_Grillthermo.
#
# Verwendung (nach dem Klonen):
#   git clone <repo-url> ESP32_Grillthermo
#   ESP32_Grillthermo/scripts/west-init.sh
#
# Das Script legt den west-Workspace im ELTERNverzeichnis des Repos an
# (Topdir), holt das in west.yml gepinnte Zephyr + die Module und die
# proprietaeren ESP32-WiFi/BT-Blobs.
#
# Anschliessend bauen mit:
#   cd ESP32_Grillthermo
#   west build -b esp32s3_devkitc/esp32s3/procpu -d build app

set -euo pipefail

# ZEPHYR_BASE darf den projektlokalen, gepinnten Zephyr nicht ueberschreiben.
unset ZEPHYR_BASE || true

# Repo-Wurzel = ein Verzeichnis ueber diesem Script.
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd "$REPO_DIR/.." && pwd)"
VENV_DIR="$WORKSPACE_DIR/.venv"

echo ">> Repo:      $REPO_DIR"
echo ">> Workspace: $WORKSPACE_DIR"

# 1) Python-venv anlegen und west darin installieren.
if [ -d "$VENV_DIR" ]; then
	echo ">> venv bereits vorhanden ($VENV_DIR) - ueberspringe Anlegen."
else
	echo ">> Lege venv an: $VENV_DIR"
	python3 -m venv "$VENV_DIR"
fi
# shellcheck source=/dev/null
source "$VENV_DIR/bin/activate"
pip install --quiet west

# 2) Workspace initialisieren (nur, falls noch kein .west existiert).
#    'west init -l' setzt den Topdir auf das Elternverzeichnis des Repos.
if [ -d "$WORKSPACE_DIR/.west" ]; then
	echo ">> Workspace bereits initialisiert (.west vorhanden) - ueberspringe west init."
else
	west init -l "$REPO_DIR"
fi

# 3) Gepinntes Zephyr + Module holen.
west update

# 4) Zephyr-Python-Abhaengigkeiten installieren (nach west update verfuegbar).
pip install --quiet -r "$WORKSPACE_DIR/zephyr/scripts/requirements.txt"

# 5) ESP32-WiFi/BT-Blobs holen (werden von 'west update' nicht mitgeladen).
west blobs fetch hal_espressif

cat <<EOF

==> Workspace bereit.

venv aktivieren (in jeder neuen Shell-Session):
  source "$VENV_DIR/bin/activate"

Firmware bauen:
  cd "$REPO_DIR"
  west build -b esp32s3_devkitc/esp32s3/procpu -d build app

Hinweis: ZEPHYR_BASE darf NICHT global gesetzt sein, sonst baut west gegen
das dort referenzierte Zephyr statt gegen den projektlokal gepinnten Stand.
EOF
