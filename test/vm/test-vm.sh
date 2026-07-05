#!/usr/bin/env bash
set -e

VM_ID="334af2a" # Replace with your vagrant ID if it changes

echo "==> Uploading code to VM (excluding .git and build)..."
tar -czf - --exclude='.git' --exclude='build' . | vagrant ssh $VM_ID -c 'rm -rf ~/cross-device-manager && mkdir -p ~/cross-device-manager && tar -xzf - -C ~/cross-device-manager'

echo "==> Building project inside VM..."
vagrant ssh $VM_ID -c 'cd ~/cross-device-manager && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake -Dsdbus-c++_DIR=/usr/local/lib/cmake/sdbus-c++ -DDEVMGR_WITH_SDBUS=ON && cmake --build build'

echo "==> Linking binary path..."
vagrant ssh $VM_ID -c 'cd ~/cross-device-manager && mkdir -p build/linux-debug/daemon && ln -sfn ../../daemon/devmgrd build/linux-debug/daemon/devmgrd'

echo "==> Running Phase 4 Smoke Test..."
vagrant ssh $VM_ID -c 'cd ~/cross-device-manager && sudo ./test/vm/phase4-smoke.sh /sys/bus/usb/devices/3-1'
