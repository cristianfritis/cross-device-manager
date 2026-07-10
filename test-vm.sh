#!/usr/bin/env bash
set -e

# Always run from the directory containing this script (the project root)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"
VM_DIR="$PROJECT_ROOT/test/vm"

echo "==> Uploading code to VM (excluding .git, build, .cache)..."
# Tar from project root, but run vagrant ssh from the VM directory
tar -czf - --exclude='.git' --exclude='build' --exclude='.cache' . | (cd "$VM_DIR" && vagrant ssh -c 'rm -rf ~/cross-device-manager && mkdir -p ~/cross-device-manager && tar -xzf - -C ~/cross-device-manager')

echo "==> Building project inside VM..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake -Dsdbus-c++_DIR=/usr/local/lib/cmake/sdbus-c++ -DDEVMGR_WITH_SDBUS=ON && cmake --build build')

echo "==> Linking binary path..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && mkdir -p build/linux-debug/daemon && ln -sfn ../../daemon/devmgrd build/linux-debug/daemon/devmgrd')

echo "==> Running Phase 4 Smoke Test..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && sudo ./test/vm/phase4-smoke.sh /sys/bus/usb/devices/3-1')

echo "==> Running Phase 5 Smoke Test..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && sudo ./test/vm/phase5-smoke.sh /sys/bus/usb/devices/3-1 "$(ls -d /sys/bus/virtio/devices/virtio* 2>/dev/null | head -1)"')
