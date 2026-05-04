FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        cmake \
        ninja-build \
        python3-pip \
        python3-minimal \
        pkg-config \
        libssl-dev \
        autoconf \
        libtool \
        gdb \
        clangd \
        cppcheck \
        curl \
    && pip3 install conan --break-system-packages \
    && rm -rf /var/lib/apt/lists/*

# Install Foundry (anvil + cast + forge) for integration tests.
# Fetch the latest stable release tag, then download that tarball directly
# into /usr/local/bin — avoids the foundryup home-dir / permission issues.
RUN FOUNDRY_TAG=$(curl -fsSL -o /dev/null -w '%{url_effective}' \
        https://github.com/foundry-rs/foundry/releases/latest \
        | sed 's|.*/tag/||') \
    && curl -fL "https://github.com/foundry-rs/foundry/releases/download/${FOUNDRY_TAG}/foundry_${FOUNDRY_TAG}_linux_amd64.tar.gz" \
    | tar -xz -C /usr/local/bin anvil cast forge


# Pre-detect Conan profile so first `make deps` is faster
RUN conan profile detect --force
