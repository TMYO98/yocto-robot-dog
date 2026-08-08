#!/usr/bin/env bash
# One-time setup of an Ubuntu (WSL2) host for building this Yocto project.
# Does NOT start a build.
#
# Run from inside Ubuntu:
#   bash "/mnt/d/Me/Me Again/robot_core_controler/tools/setup-wsl-host.sh"
set -euo pipefail

SRC_WIN="/mnt/d/Me/Me Again/robot_core_controler"
DEST="$HOME/robot_core_controler"

echo "==> Installing Yocto host packages"
sudo apt-get update
sudo apt-get install -y \
    gawk wget git diffstat unzip texinfo gcc build-essential chrpath socat cpio \
    python3 python3-pip python3-pexpect python3-git python3-jinja2 python3-subunit \
    python3-venv xz-utils debianutils iputils-ping zstd liblz4-tool file locales \
    libacl1 bmap-tools pipx rsync

echo "==> Generating en_US.UTF-8 locale"
sudo locale-gen en_US.UTF-8

echo "==> Installing kas"
pipx install kas || pipx upgrade kas
pipx ensurepath

# Ubuntu 24.04 restricts unprivileged user namespaces via AppArmor, which
# breaks BitBake's network isolation. Allow them (persistently).
if [ -e /proc/sys/kernel/apparmor_restrict_unprivileged_userns ]; then
    echo "==> Allowing unprivileged user namespaces (needed by BitBake)"
    echo 'kernel.apparmor_restrict_unprivileged_userns = 0' | \
        sudo tee /etc/sysctl.d/60-bitbake-userns.conf >/dev/null
    sudo sysctl -p /etc/sysctl.d/60-bitbake-userns.conf
fi

echo "==> Copying project into the Linux filesystem: $DEST"
mkdir -p "$DEST"
rsync -a --exclude build/ "$SRC_WIN/" "$DEST/" 2>/dev/null || cp -r "$SRC_WIN/." "$DEST/"
# Strip any CRLF line endings that may have come from Windows editors
find "$DEST" -type f \( -name '*.bb' -o -name '*.bbappend' -o -name '*.conf' -o -name '*.yml' \
    -o -name '*.sh' -o -name '*.service' -o -name '*.network' -o -name '*.preset' \
    -o -name '*.default' -o -name '*.c' -o -name 'CMakeLists.txt' \) -exec sed -i 's/\r$//' {} +

cd "$DEST"
if [ ! -d .git ]; then
    git init -q
    git add -A
    git -c user.name="setup" -c user.email="setup@localhost" commit -qm "Initial import" || true
fi

echo
echo "Done. Open a new shell (so ~/.local/bin is on PATH), then when ready:"
echo "  cd ~/robot_core_controler && kas build robot-core.yml"
