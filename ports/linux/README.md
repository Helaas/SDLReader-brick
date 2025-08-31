# SDL Reader - Linux Port

This directory contains the Linux-specific build configuration for SDL Reader.

**Note**: This build has been tested on Ubuntu 24.04 (Noble). Other Linux distributions may require different package names or additional configuration.

## Dependencies

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libmupdf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev
```

### Other Distributions (Untested)
The following package lists are provided as a starting point but **have not been tested**. Package names and availability may vary:

#### Fedora/RHEL/CentOS
```bash
sudo dnf install gcc-c++ pkg-config SDL2-devel SDL2_ttf-devel mupdf-devel freetype-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel pkg-config sdl2 sdl2_ttf mupdf freetype2
```

#### openSUSE
```bash
sudo zypper install gcc-c++ pkg-config libSDL2-devel libSDL2_ttf-devel mupdf-devel
```

**Note**: You may need to install additional MuPDF dependencies manually on these distributions.

## Building

From the project root directory:
```bash
make linux
```

Or from this directory:
```bash
make
```

### Installing Dependencies Automatically

You can use the provided dependency installation targets for Ubuntu/Debian:

```bash
# For Ubuntu/Debian (tested)
make install-deps

# For other distributions (untested)
make install-deps-fedora   # Fedora/RHEL
make install-deps-arch     # Arch Linux
```

**Note**: The automated installers for non-Ubuntu distributions are untested and may not include all required dependencies.

## Running

After building, the executable will be located at:
```
bin/sdl_reader_cli
```

Run it from the project root with:
```bash
./bin/sdl_reader_cli path/to/your/document.pdf
# or
./bin/sdl_reader_cli path/to/your/comic.cbz
```

## Troubleshooting

### MuPDF Dependencies
MuPDF requires many dependencies for full functionality. If you encounter linking errors, you may need to install additional packages manually. The complete dependency list for Ubuntu/Debian includes:

- **Ubuntu/Debian** (tested): `libmupdf-dev`, `libfreetype6-dev`, `libharfbuzz-dev`, `libjpeg-dev`, `libopenjp2-7-dev`, `libjbig2dec0-dev`, `libgumbo-dev`, `libmujs-dev`
- **Other distributions**: Package names will vary and may not be available in all repositories

### Distribution-Specific Issues
This build configuration has only been tested on **Ubuntu 24.04**. If you're using other Linux distributions:

1. Package names may be different
2. Some dependencies may not be available in default repositories
3. You may need to compile some libraries from source
4. Additional system configuration may be required

### SDL2 Support Issues
Make sure you have SDL2 and SDL2_ttf development packages installed for functionality:
- **SDL2**: `libsdl2-dev` (Debian/Ubuntu), `SDL2-devel` (Fedora), `sdl2` (Arch)
- **SDL2_ttf**: `libsdl2-ttf-dev` (Debian/Ubuntu), `SDL2_ttf-devel` (Fedora), `sdl2_ttf` (Arch)

MuPDF provides native support for CBZ/ZIP comic book archives, so no additional libraries are needed for comic book functionality.

### pkg-config Issues
Ensure `pkg-config` is installed on your system. It's required to locate the SDL2 libraries and headers.

### Contributing
If you successfully build on other Linux distributions, please consider contributing the working dependency lists and any required modifications!
