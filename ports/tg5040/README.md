# TG5040 Port - Docker Development Environment

This directory contains the TG5040-specific build configuration and Docker development environment for the SDLReader project.

Based on the [Trimui toolchain Docker image](https://git.crowdedwood.com/trimui-toolchain/) by neonloop.

## Files in this directory
- `Makefile` - TG5040 application build configuration
- `Makefile.docker` - Docker environment management
- `docker-compose.yml` - Docker Compose setup  
- `Dockerfile` - TG5040 toolchain container image

## Quick Start

### Using Docker Compose (Recommended)
```bash
cd ports/tg5040

# Start development environment
docker-compose up -d dev

# Enter the container
docker-compose exec dev bash

# Build the application (inside container)
cd /root/workspace
make tg5040
```

### Using Docker Makefile
```bash
cd ports/tg5040

# Build toolchain and enter shell
make -f Makefile.docker shell

# Build the application (inside container)  
cd /root/workspace
make tg5040
```

### Direct Build (with toolchain installed)
```bash
# From project root
make tg5040
```

## Installation

With Docker installed and running, `make shell` builds the toolchain and drops into a shell inside the container. The container's `~/workspace` is bound to `./workspace` by default. The toolchain is located at `/opt/` inside the container.

After building the first time, unless a dependency of the image has changed, `make shell` will skip building and drop into the shell.

## Development Workflow

- **Host machine**: Edit source code in the project root (`../../src/`, `../../include/`, etc.)
- **Container**: Build and test inside the Docker container
- **Volume mapping**: The project root is mounted at `/root/workspace` inside the container
- **Toolchain**: Located at `/opt/` inside the container

### Container Details
- The container's `/root/workspace` is mapped to the project root directory
- Source code changes on the host are immediately available in the container
- Built artifacts are stored in the project's `bin/` and `build/` directories

## Platform-Specific Features
The TG5040 build includes:
- **Hardware Power Management**: Port-specific power button handling
  - Power button monitoring via `/dev/input/event1`
  - Short press: System suspend for battery conservation
  - Long press: Safe system shutdown
  - Wake detection with grace period handling
  - Error notifications through GUI callbacks
- **Platform-optimized build flags**: `-DTG5040_PLATFORM`
- **Port-specific source structure**: 
  - `include/power_handler.h` - TG5040 power management interface
  - `src/power_handler.cpp` - Hardware-specific power button implementation
- **Embedded Linux-specific libraries and dependencies**

See [setup-env.sh](./support/setup-env.sh) for some useful vars for compiling that are exported automatically.

## Docker for Mac

Docker for Mac has a memory limit that can make the toolchain build fail. Follow [these instructions](https://docs.docker.com/docker-for-mac/) to increase the memory limit.
