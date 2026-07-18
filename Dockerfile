# BASE is parameterized for the release path: packaged artifacts must be built
# on the OLDEST supported series (22.04) so the binaries' glibc/libstdc++
# symbol versions and dpkg-shlibdeps Depends resolve on both 22.04 and 24.04
# (the t64 packages Provide the old names on amd64). Dev/CI gates keep 24.04.
ARG BASE=ubuntu:24.04
FROM ${BASE}

ENV DEBIAN_FRONTEND=noninteractive VCPKG_ROOT=/opt/vcpkg
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        clang-tidy clang-format ca-certificates \
        libudev-dev libkmod-dev kmod libsystemd-dev qt6-base-dev umockdev libumockdev-dev dbus \
        libgl1-mesa-dev \
        dpkg-dev fakeroot file shellcheck \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

# sdbus-c++ v2 (pinned to match the dev host's portage 2.3.1): Ubuntu 24.04
# ships 1.4 and v2.0 was a breaking API rewrite — see the Phase 4 spec.
# Installing it here makes find_package(sdbus-c++ 2) succeed, so the
# linux-debug configure below defaults DEVMGR_WITH_SDBUS=ON (devmgrd + the
# D-Bus channel + devmgr_ipc build). `dbus` above provides dbus-run-session
# for the devmgr_ipc round-trip ctest.
# Static + PIC so packaged builds (DEVMGR_PACKAGED_BUILD) can link sdbus-c++
# into the shipped binaries — the deb must not depend on a library Ubuntu only
# provides as an incompatible 1.x (packaging-deb spec). Dev/CI builds link the
# same static archive; behavior is identical.
RUN git clone --depth 1 --branch v2.3.1 https://github.com/Kistler-Group/sdbus-cpp.git /tmp/sdbus-cpp \
    && cmake -S /tmp/sdbus-cpp -B /tmp/sdbus-cpp/build \
         -DCMAKE_BUILD_TYPE=Release -DSDBUSCPP_BUILD_CODEGEN=OFF \
         -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && cmake --build /tmp/sdbus-cpp/build -j \
    && cmake --install /tmp/sdbus-cpp/build \
    && rm -rf /tmp/sdbus-cpp

WORKDIR /src
COPY . .
RUN cmake --preset linux-debug && cmake --build --preset linux-debug
CMD ["ctest", "--test-dir", "build/linux-debug", "--output-on-failure"]
