FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive VCPKG_ROOT=/opt/vcpkg
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        clang-tidy clang-format ca-certificates \
        libudev-dev libkmod-dev kmod libsystemd-dev qt6-base-dev umockdev libumockdev-dev dbus \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

# sdbus-c++ v2 (pinned to match the dev host's portage 2.3.1): Ubuntu 24.04
# ships 1.4 and v2.0 was a breaking API rewrite — see the Phase 4 spec.
# Installing it here makes find_package(sdbus-c++ 2) succeed, so the
# linux-debug configure below defaults DEVMGR_WITH_SDBUS=ON (devmgrd + the
# D-Bus channel + devmgr_ipc build). `dbus` above provides dbus-run-session
# for the devmgr_ipc round-trip ctest.
RUN git clone --depth 1 --branch v2.3.1 https://github.com/Kistler-Group/sdbus-cpp.git /tmp/sdbus-cpp \
    && cmake -S /tmp/sdbus-cpp -B /tmp/sdbus-cpp/build \
         -DCMAKE_BUILD_TYPE=Release -DSDBUSCPP_BUILD_CODEGEN=OFF \
    && cmake --build /tmp/sdbus-cpp/build -j \
    && cmake --install /tmp/sdbus-cpp/build \
    && rm -rf /tmp/sdbus-cpp

WORKDIR /src
COPY . .
RUN cmake --preset linux-debug && cmake --build --preset linux-debug
CMD ["ctest", "--test-dir", "build/linux-debug", "--output-on-failure"]
