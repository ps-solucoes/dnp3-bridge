# Build container for dnp3-bridge: every build (amd64 and armhf) runs in here,
# in CI and locally. Debian trixie is the only target OS, so building on
# trixie guarantees the packages match the board's glibc and gRPC.
#
#   docker build -t dnp3-bridge-build -f docker/build.Dockerfile docker
#   docker/run.sh cmake --workflow --preset dev

FROM debian:trixie

ARG DEBIAN_FRONTEND=noninteractive

# gRPC and protobuf are installed for both architectures (all Multi-Arch:
# same); protoc and grpc_cpp_plugin come from the amd64 compilers. The armhf
# libraries also let qemu run the tests and dpkg-shlibdeps resolve the
# package dependencies.
RUN dpkg --add-architecture armhf \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        crossbuild-essential-armhf \
        cmake \
        git \
        ca-certificates \
        file \
        qemu-user \
        libgrpc++-dev \
        libprotobuf-dev \
        protobuf-compiler-grpc \
        libgrpc++-dev:armhf \
        libprotobuf-dev:armhf \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
