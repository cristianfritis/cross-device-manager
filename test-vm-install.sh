#!/usr/bin/env bash
# Install-smoke driver (packaging-deb spec exit gate): builds the deb in the
# CI container, then installs and exercises it in the Vagrant VM via
# test/vm/install-smoke.sh. This deliberately tests the artifact, not the
# build tree — pass --fresh to destroy/re-provision the VM first so the
# install lands on a clean Ubuntu.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"
VM_DIR="$PROJECT_ROOT/test/vm"
DIST_DIR="$PROJECT_ROOT/dist"

if [ "${1:-}" = "--fresh" ]; then
    echo "==> Recreating VM from scratch..."
    (cd "$VM_DIR" && vagrant destroy -f && vagrant up)
fi

echo "==> Building deb in the packaging container (ubuntu:22.04 base)..."
mkdir -p "$DIST_DIR"
docker compose -f test/docker-compose.yml build pkg
docker compose -f test/docker-compose.yml run --rm \
    -v "$PROJECT_ROOT:/host:ro" -v "$DIST_DIR:/dist" pkg bash -c '
    set -e
    cp -a /host/. /live && cd /live && rm -rf build dist
    export VCPKG_ROOT=/opt/vcpkg
    cmake --preset linux-packaged
    cmake --build build/linux-packaged -j"$(nproc)"
    cpack --config build/linux-packaged/CPackConfig.cmake -B /dist
    rm -rf /dist/_CPack_Packages
'
DEB=$(ls "$DIST_DIR"/devmgr_*_amd64.deb)
echo "==> Built: $DEB"

echo "==> Uploading deb + smoke script to VM..."
(cd "$VM_DIR" && vagrant ssh -c 'rm -rf ~/install-smoke && mkdir -p ~/install-smoke')
tar -czf - -C "$DIST_DIR" "$(basename "$DEB")" -C "$PROJECT_ROOT/test/vm" install-smoke.sh \
    | (cd "$VM_DIR" && vagrant ssh -c 'tar -xzf - -C ~/install-smoke')

echo "==> Running install smoke inside VM..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/install-smoke && chmod +x install-smoke.sh && sudo ./install-smoke.sh ./devmgr_*_amd64.deb')
