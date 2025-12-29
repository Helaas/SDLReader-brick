#!/bin/sh
set -e

ARCH=$(uname -m)
CROSS_TRIPLE=aarch64-nextui-linux-gnu
CROSS_ROOT=/opt/${CROSS_TRIPLE}
SYSROOT=${CROSS_ROOT}/${CROSS_TRIPLE}/libc
TOOLCHAIN_REPO="https://github.com/LoveRetro/gcc-arm-8.3-aarch64-tg5040"
TOOLCHAIN_BUILD=${TOOLCHAIN_BUILD:-v8.3.0-20250814-133302-c13dfc38}
SYSROOT_TAR="SDK_usr_tg5040_a133p.tgz"
SYSROOT_URL="https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/${SYSROOT_TAR}"

if [ "$ARCH" = "x86_64" ]; then
	TOOLCHAIN_ARCHIVE="gcc-8.3.0-aarch64-nextui-linux-gnu-x86_64-host.tar.xz"
elif [ "$ARCH" = "aarch64" ]; then
	TOOLCHAIN_ARCHIVE="gcc-8.3.0-aarch64-nextui-linux-gnu-arm64-host.tar.xz"
else
	echo "Unsupported architecture: $ARCH"
	exit 1
fi

TOOLCHAIN_URL="${TOOLCHAIN_REPO}/releases/download/${TOOLCHAIN_BUILD}/${TOOLCHAIN_ARCHIVE}"

mkdir -p "${CROSS_ROOT}" "${SYSROOT}"
cd /tmp

echo "Downloading ${TOOLCHAIN_ARCHIVE} from ${TOOLCHAIN_URL}..."
wget -q "${TOOLCHAIN_URL}" -O toolchain.tar.xz
tar -xf toolchain.tar.xz -C "${CROSS_ROOT}" --strip-components=2
rm -f toolchain.tar.xz

echo "Downloading TG5040 sysroot..."
wget -q "${SYSROOT_URL}" -O "${SYSROOT_TAR}"
tar -xzf "${SYSROOT_TAR}" -C "${SYSROOT}"
rm -f "${SYSROOT_TAR}"
