FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive VCPKG_ROOT=/opt/vcpkg
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        clang-tidy clang-format ca-certificates \
        libudev-dev libkmod-dev umockdev libumockdev-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY . .
RUN cmake --preset linux-debug && cmake --build --preset linux-debug
CMD ["ctest", "--test-dir", "build/linux-debug", "--output-on-failure"]
