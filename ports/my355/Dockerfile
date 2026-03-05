# MY355 toolchain container (prebuilt on GHCR)
# This image bundles the cross compiler, sysroot, and supporting libraries for the Miyoo Flip.
FROM ghcr.io/loveretro/my355-toolchain:latest

# Align with docker-compose/Makefile.docker volume mount
WORKDIR /workspace
VOLUME ["/workspace"]

CMD ["/bin/bash"]
