# SDL Reader - Linux Port

This directory contains the Linux-specific build configuration for SDL Reader.

**Note**: This build has been tested on Ubuntu 24.04 (Noble). Other Linux distributions may require different package names or additional configuration.

## Key Features

- **Built-in CBR Support**: Now includes support for CBR (Comic Book RAR) files alongside CBZ/ZIP comic books
- **WebP Image Support**: Supports WebP images in PDF documents and comic book archives
- **Self-Contained MuPDF**: Automatically downloads and builds MuPDF 1.26.7 with libarchive and WebP support
- **No System MuPDF Required**: No longer depends on system MuPDF packages
- **Nuklear Interface**: Desktop builds gain a Nuklear-powered file browser, font & reading-style menu, and on-screen number pad

## Dependencies

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev libarchive-dev libwebp-dev git
```

### Other Distributions (Untested)
The following package lists are provided as a starting point but **have not been tested**. Package names and availability may vary:

#### Fedora/RHEL/CentOS
```bash
sudo dnf install gcc-c++ pkg-config SDL2-devel SDL2_ttf-devel freetype-devel libarchive-devel libwebp-devel git
```

#### Arch Linux
```bash
sudo pacman -S base-devel pkg-config sdl2 sdl2_ttf freetype2 libarchive libwebp git
```

#### openSUSE
```bash
sudo zypper install gcc-c++ pkg-config libSDL2-devel libSDL2_ttf-devel libarchive-devel libwebp-devel git
```

**Note**: The system no longer requires MuPDF packages as it builds its own copy with CBR and WebP support.

## Building

The build process now automatically downloads and compiles MuPDF 1.26.7 with CBR support if not already present.

From the project root directory:
```bash
make linux
```

Or from this directory:
```bash
make
```

### First Build Process

1. **Automatic MuPDF Setup**: On first build, the system will:
   - Clone MuPDF 1.26.7 from GitHub
   - Apply WebP patch for enhanced image format support
   - Configure it with libarchive support for CBR files
   - Configure it with WebP support for WebP images
   - Build the required MuPDF libraries

2. **Subsequent Builds**: The MuPDF directory is preserved between builds for faster compilation.

### Installing Dependencies Automatically

You can use the provided dependency installation targets for Ubuntu/Debian:

```bash
# For Ubuntu/Debian (tested)
make install-deps

# For other distributions (untested)
make install-deps-fedora   # Fedora/RHEL
make install-deps-arch     # Arch Linux
```

**Note**: The automated installers for non-Ubuntu distributions are untested and may not include all required dependencies. System MuPDF packages are no longer required.

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
# or (NEW!)
./bin/sdl_reader_cli path/to/your/comic.cbr
# or launch the integrated browser (remembers last directory)
./bin/sdl_reader_cli --browse
```

Launching with `--browse` opens the Nuklear-driven file picker, auto-resumes your position using `reading_history.json`, and lets you tweak fonts/themes from a controller. Preferences are saved in the reader state directory (`$SDL_READER_STATE_DIR`, defaulting to `$HOME/config.json`). Set `SDL_READER_DEFAULT_DIR` if you want the picker to start in a specific library folder. Press **X** to toggle the new thumbnail grid, and use the `showDocumentMinimap` flag in `config.json` to disable the zoom minimap if desired.

### Fonts & Reading Styles

Add any `.ttf` or `.otf` files to the project's `fonts/` directory (or the one bundled alongside your build). The in-app Options → Font & Reading Style menu will pick them up automatically so you can preview and select custom typography at runtime.

## Applied Patches

Every Linux build invocations (`make linux` from the root or `make` in this directory) apply the shared `webp-upstream-697749.patch` to MuPDF. This KOReader-derived patch backports upstream fixes so MuPDF can decode WebP images consistently inside comic archives and EPUB resources.

## Supported Formats

- **PDF**: Portable Document Format files
- **CBZ**: Comic Book ZIP archives
- **CBR**: Comic Book RAR archives
- **ZIP**: ZIP archives containing images
- **EPUB**: Electronic book format
- **MOBI**: Kindle book format
- **WebP Images**: WebP format images within documents and archives

## Troubleshooting

### Build Dependencies
The build system now automatically handles MuPDF compilation with WebP support. If you encounter issues, ensure you have:

- **git**: Required to clone MuPDF repository
- **libarchive-dev**: Required for CBR support
- **libwebp-dev**: Required for WebP image support
- **Standard build tools**: gcc, g++, make, pkg-config

### Distribution-Specific Issues
This build configuration has only been tested on **Ubuntu 24.04**. If you're using other Linux distributions:

1. Package names may be different
2. Some dependencies may not be available in default repositories
3. Additional system configuration may be required

The self-built MuPDF approach with WebP support reduces dependency issues compared to using system packages.

### SDL2 Support Issues
Make sure you have SDL2 and SDL2_ttf development packages installed:
- **SDL2**: `libsdl2-dev` (Debian/Ubuntu), `SDL2-devel` (Fedora), `sdl2` (Arch)
- **SDL2_ttf**: `libsdl2-ttf-dev` (Debian/Ubuntu), `SDL2_ttf-devel` (Fedora), `sdl2_ttf` (Arch)

### Comic Book Support
- **CBZ/ZIP**: Supported natively by MuPDF
- **CBR**: Now supported via built-in libarchive integration (no additional setup required)

### pkg-config Issues
Ensure `pkg-config` is installed on your system. It's required to locate the SDL2 libraries and headers.

### MuPDF Build Issues
If MuPDF fails to build:
1. Ensure you have internet access (required to clone the repository)
2. Check that `git` is installed
3. Verify `libarchive-dev` and `libwebp-dev` are installed for CBR and WebP support
4. Try cleaning and rebuilding: `make clean && make`

### Contributing
If you successfully build on other Linux distributions, please consider contributing the working dependency lists and any required modifications!
