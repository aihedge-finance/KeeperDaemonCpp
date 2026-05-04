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
# Binaries are copied to /usr/local/bin so they are available to all users.
RUN curl -L https://foundry.paradigm.xyz | bash \
    && /root/.foundry/bin/foundryup \
    && cp /root/.foundry/bin/anvil /usr/local/bin/anvil \
    && cp /root/.foundry/bin/cast  /usr/local/bin/cast  \
    && cp /root/.foundry/bin/forge /usr/local/bin/forge

# Pre-detect Conan profile so first `make deps` is faster
RUN conan profile detect --force
