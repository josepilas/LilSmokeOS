#!/bin/bash
#
# LilSmokeOS — VirtualBox VM launcher
#
# Usage: ./scripts/run-vbox.sh [iso_path]
#   iso_path defaults to ./build/buildroot/output/images/lilsmoke.iso
#
# Creates a VirtualBox VM named "LilSmokeOS" (if it doesn't exist) with
# the right settings for LilSmokeOS:
#   - 256 MB RAM (matches SF3000/GB350 worst case)
#   - 16 MB VRAM (minimum — we use software rendering)
#   - VMSVGA graphics controller (VESA fbdev compatible)
#   - No audio by default (set LILSMOKE_VBOX_AUDIO=1 to enable)
#   - No hard disk (we boot from the ISO)
#   - NAT networking with SSH forwarded from host port 2222 → guest 22
#   - PS/2 keyboard + mouse (VBox's defaults)
#
# Prerequisites:
#   - VirtualBox installed (VBoxManage on PATH)
#   - A completed build (./scripts/build.sh vbox-x86_64 or vbox-test)
#
# See docs/vbox-guide.md for the full setup guide.

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO_PATH="${1:-$LILSMOKE_ROOT/build/buildroot/output/images/lilsmoke.iso}"
VM_NAME="LilSmokeOS"

# --- Sanity checks ---

if ! command -v VBoxManage >/dev/null 2>&1; then
    echo "error: VBoxManage not installed (or not on PATH)" >&2
    echo "  on Debian/Ubuntu: sudo apt install virtualbox" >&2
    echo "  on Fedora:        sudo dnf install VirtualBox" >&2
    echo "  on Arch:          sudo pacman -S virtualbox" >&2
    exit 1
fi

if [ ! -f "$ISO_PATH" ]; then
    echo "error: ISO not found: $ISO_PATH" >&2
    echo "did you run ./scripts/build.sh vbox-x86_64 ?" >&2
    exit 1
fi

echo "[lilsmoke] launching VirtualBox"
echo "[lilsmoke]   VM name:    $VM_NAME"
echo "[lilsmoke]   ISO:        $ISO_PATH"
echo "[lilsmoke]   RAM:        256 MB"
echo "[lilsmoke]   VRAM:       16 MB"
echo "[lilsmoke]   graphics:   VMSVGA (VESA fbdev)"
echo "[lilsmoke]   audio:      ${LILSMOKE_VBOX_AUDIO:-disabled} (set LILSMOKE_VBOX_AUDIO=1 to enable)"
echo "[lilsmoke]   network:    NAT, SSH forwarded to host port 2222"
echo ""

# --- Create the VM if it doesn't exist ---

if ! VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
    echo "[lilsmoke] creating VM: $VM_NAME"
    VBoxManage createvm --name "$VM_NAME" --ostype Linux26_64 --register

    # Base hardware config
    VBoxManage modifyvm "$VM_NAME" \
        --memory 256 \
        --vram 16 \
        --graphicscontroller vmsvga \
        --nic1 nat \
        --audio "${LILSMOKE_VBOX_AUDIO:+dsound}" \
        --audiocontroller ac97 \
        --boot1 dvd \
        --boot2 disk \
        --boot3 none \
        --boot4 none \
        --ioapic on \
        --cpus 1 \
        --rtcuseutc on

    # Forward SSH from host:2222 → guest:22
    VBoxManage modifyvm "$VM_NAME" \
        --natpf1 "ssh,tcp,127.0.0.1,2222,,22"

    # Storage controllers: IDE for the CD, SATA for an optional VDI
    VBoxManage storagectl "$VM_NAME" --name IDE --add ide --controller PIIX4
    VBoxManage storagectl "$VM_NAME" --name SATA --add sata --portcount 4

    echo "[lilsmoke] VM created."
fi

# --- Attach the latest ISO ---

# Detach any existing medium on the IDE secondary (CD) port
VBoxManage storageattach "$VM_NAME" \
    --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium none 2>/dev/null || true

# Attach our ISO
VBoxManage storageattach "$VM_NAME" \
    --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium "$ISO_PATH"

echo "[lilsmoke] attached ISO: $(basename "$ISO_PATH")"

# --- Start the VM ---

# Check if VM is already running
VM_STATE=$(VBoxManage showvminfo "$VM_NAME" --machinereadable \
            | grep ^VMState= | cut -d= -f2 | tr -d '"')

if [ "$VM_STATE" = "running" ]; then
    echo "[lilsmoke] VM is already running. Resetting to boot the new ISO..."
    VBoxManage controlvm "$VM_NAME" reset
elif [ "$VM_STATE" = "saved" ] || [ "$VM_STATE" = "paused" ]; then
    echo "[lilsmoke] VM is $VM_STATE. Powering off and restarting..."
    VBoxManage controlvm "$VM_NAME" poweroff 2>/dev/null || true
    sleep 1
    VBoxManage startvm "$VM_NAME" --type gui
else
    echo "[lilsmoke] starting VM..."
    VBoxManage startvm "$VM_NAME" --type gui
fi

echo ""
echo "[lilsmoke] VM is booting. The window will open shortly."
echo "[lilsmoke] to SSH in (after the device finishes booting):"
echo "  ssh -p 2222 root@127.0.0.1"
echo ""
echo "[lilsmoke] to power off the VM:"
echo "  VBoxManage controlvm $VM_NAME poweroff"
echo ""
echo "[lilsmoke] to delete the VM entirely:"
echo "  VBoxManage unregistervm $VM_NAME --delete"
