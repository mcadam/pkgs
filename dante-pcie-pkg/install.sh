#!/usr/bin/env bash
set -euo pipefail

# Digigram LX-DANTE PCIe driver installer (DKMS-based)
#
# Usage: place this script inside the dante-pcie-src/ folder (next to
# dante-pcie.c, dkms.conf, etc.) and run it with sudo on the target machine:
#
#   sudo ./install.sh
#
# It installs dkms + matching kernel headers, stages the source under
# /usr/src, registers it with DKMS, builds, installs, and loads the module.
# Re-running it is safe -- it re-registers cleanly if already installed.

PKG_NAME="dante-pcie"
PKG_VERSION="1.2.4rc03"
SRC_DIR="/usr/src/${PKG_NAME}-${PKG_VERSION}"

if (( EUID != 0 )); then
    echo "Please run as root (sudo ./install.sh)."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Installing dkms and kernel headers for $(uname -r)..."
apt-get update -qq
apt-get install -y dkms "linux-headers-$(uname -r)"

echo "==> Staging source into ${SRC_DIR}..."
rm -rf "${SRC_DIR}"
mkdir -p "${SRC_DIR}"
cp -r "${SCRIPT_DIR}"/* "${SRC_DIR}/"
rm -f "${SRC_DIR}/$(basename "$0")"

if dkms status | grep -q "^${PKG_NAME}/${PKG_VERSION}"; then
    echo "==> ${PKG_NAME}/${PKG_VERSION} already registered with dkms, removing old registration first..."
    dkms remove -m "${PKG_NAME}" -v "${PKG_VERSION}" --all || true
fi

echo "==> Registering with dkms..."
dkms add -m "${PKG_NAME}" -v "${PKG_VERSION}"

echo "==> Building..."
dkms build -m "${PKG_NAME}" -v "${PKG_VERSION}"

echo "==> Installing..."
dkms install -m "${PKG_NAME}" -v "${PKG_VERSION}"

echo "==> Loading module..."
depmod -a
modprobe "${PKG_NAME}"

echo
echo "==> Done. DKMS status:"
dkms status

echo
echo "==> ALSA cards:"
cat /proc/asound/cards

echo
echo "NOTE: this script only handles the kernel module. It does NOT restore"
echo "your ALSA config (asound.conf / DantePCIe.conf) -- put those back"
echo "separately from your other backup before expecting the card to be"
echo "usable through ALSA."
