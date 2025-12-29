TOOLCHAIN_ARCH=`uname -m`
#!/bin/sh

TOOLCHAIN_ARCH=$(uname -m)
CROSS_TRIPLE=aarch64-nextui-linux-gnu
CROSS_ROOT=/opt/${CROSS_TRIPLE}
SYSROOT=${CROSS_ROOT}/${CROSS_TRIPLE}/libc

if [ "$TOOLCHAIN_ARCH" = "aarch64" ]; then
	export PATH="${CROSS_ROOT}/bin:${PATH}"
	export CROSS_COMPILE=${CROSS_TRIPLE}-
	export PREFIX=/usr
else
	export PATH="${CROSS_ROOT}/bin:${PATH}"
	export CROSS_COMPILE=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-
	export PREFIX=${SYSROOT}/usr
fi

export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export UNION_PLATFORM=tg5040
