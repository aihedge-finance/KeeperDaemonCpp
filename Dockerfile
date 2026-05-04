# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        git \
        python3-pip \
        pkg-config \
        libssl-dev \
        autoconf \
        libtool \
        ca-certificates \
    && pip3 install conan --break-system-packages \
    && rm -rf /var/lib/apt/lists/*



WORKDIR /app

# Copy dependency manifests first for better layer caching
COPY conanfile.txt CMakeLists.txt ./
COPY src/ ./src/

# Detect Conan profile, install dependencies, then build
RUN conan profile detect --force
RUN conan install . --output-folder=build --build=missing \
        -s build_type=Release \
        -s compiler.cppstd=20
RUN cmake -B build \
        -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -G Ninja
RUN cmake --build build

# ── Stage 2: Runtime ──────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/curve_keeper .

CMD ["./curve_keeper"]
