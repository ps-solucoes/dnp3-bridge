# Cross toolchain for the BeagleBone Black (Debian trixie armhf), using
# Debian's crossbuild-essential-armhf. Keep Debian's default code generation
# (ARMv7-A, Thumb-2, VFPv3-D16): no -mcpu/-mfpu flags.
#
# The armhf libraries are Debian multiarch packages under
# /usr/lib/arm-linux-gnueabihf, which CMake searches through
# CMAKE_LIBRARY_ARCHITECTURE; no find root path, so protoc and
# grpc_cpp_plugin are found as host programs in /usr/bin.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# The plain pkg-config answers for amd64, so FindOpenSSL and gRPC's re2
# lookup would otherwise pick the x86_64 libraries.
set(PKG_CONFIG_EXECUTABLE arm-linux-gnueabihf-pkg-config)

# GCC notes where it passes a parameter whose ABI changed in GCC 7.1 (certain
# aggregates, e.g. inside std::map); nothing here is linked against pre-7.1 code.
set(CMAKE_CXX_FLAGS_INIT -Wno-psabi)

# ctest runs armhf test binaries through qemu user-mode emulation.
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-arm)

set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf)
