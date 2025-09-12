FROM debian:buster-slim
ENV DEBIAN_FRONTEND noninteractive

ENV TZ=Europe/Brussels
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# Use archive.debian.org instead of deb.debian.org (Buster is EOL)
RUN sed -i 's|http://deb.debian.org/debian|http://archive.debian.org/debian|g' /etc/apt/sources.list \
 && sed -i 's|http://security.debian.org/debian-security|http://archive.debian.org/debian-security|g' /etc/apt/sources.list \
 && echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid-until

RUN apt-get -y update && apt-get -y install \
    bc \
    build-essential \
    bzip2 \
    bzr \
    cmake \
    cmake-curses-gui \
    cpio \
    git \
    libncurses5-dev \
    libsdl1.2-dev \
    libsdl-image1.2-dev \
    libsdl-ttf2.0-dev \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-ttf-dev \
    locales \
    make \
    rsync \
    scons \
    tree \
    unzip \
    wget \
    curl \
    libharfbuzz-dev \
    libopenjp2-7-dev \
    libjbig2dec0-dev \
    libfreetype6-dev \
    zlib1g-dev \
    libjpeg-dev \
    libpng-dev \
    libglib2.0-dev \
    libfontconfig1-dev \
    liblcms2-dev \
    libcurl4-openssl-dev \
    libarchive-dev \
    pkg-config \
    patchelf \
  && rm -rf /var/lib/apt/lists/*

# Dependencies are installed above
# MuPDF is now built per-port during the build process with CBR support

RUN mkdir -p /root/workspace
WORKDIR /root

COPY support .
RUN ./setup-toolchain.sh
RUN cat setup-env.sh >> .bashrc

VOLUME /root/workspace
WORKDIR /root/workspace

CMD ["/bin/bash"]