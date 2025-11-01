# SDL Reader - macOS Port

This directory contains the macOS-specific build configuration for SDL Reader.

**Note**: This build has been tested on macOS with Homebrew package manager.

## Key Features

- **Built-in CBR Support**: Now includes support for CBR (Comic Book RAR) files alongside CBZ/ZIP comic books
- **Self-Contained MuPDF**: Automatically downloads and builds MuPDF 1.26.7 with libarchive support
- **Homebrew Integration**: Uses Homebrew libraries for SDL2 and libarchive dependencies
- **ImGui Interface**: Desktop build includes the ImGui file browser, font & reading-style menu, and on-screen number pad

## Dependencies

### Required via Homebrew
```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required dependencies
brew install sdl2 sdl2_ttf libarchive git
```

### Development Tools
Ensure you have Xcode Command Line Tools installed:
```bash
xcode-select --install
```

## Building

The build process now automatically downloads and compiles MuPDF 1.26.7 with CBR support if not already present.

From the project root directory:
```bash
make mac
```

Or from this directory:
```bash
make
```

### First Build Process

1. **Automatic MuPDF Setup**: On first build, the system will:
   - Clone MuPDF 1.26.7 from GitHub
   - Configure it with libarchive support for CBR files
   - Build the required MuPDF libraries with macOS-specific settings

2. **Subsequent Builds**: The MuPDF directory is preserved between builds for faster compilation.

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

Using `--browse` opens the ImGui file picker, restores your last-read page via `reading_history.json`, and lets you tweak fonts/themes from a controller-friendly UI. Settings live in the reader state directory (`$SDL_READER_STATE_DIR`, defaulting to `$HOME/config.json`). Set `SDL_READER_DEFAULT_DIR` to control which folder the browser opens by default. While browsing, press **X** to toggle the thumbnail grid; the viewer also supports a minimap overlay that can be disabled via `showDocumentMinimap` in `config.json`.

### Fonts & Reading Styles

Place custom `.ttf` or `.otf` fonts in the root `fonts/` directory (or alongside the packaged app). The Options → Font & Reading Style menu will automatically discover them so you can preview and apply new typography without rebuilding.

## Applied Patches

`make mac` (or `make` from this directory) applies the shared `webp-upstream-697749.patch` to the MuPDF source tree before compilation. This KOReader-sourced patch enables reliable WebP decoding inside PDFs and comic archives on macOS builds.

## Supported Formats

- **PDF**: Portable Document Format files
- **CBZ**: Comic Book ZIP archives
- **CBR**: Comic Book RAR archives
- **ZIP**: ZIP archives containing images
- **EPUB**: Electronic book format
- **MOBI**: Kindle book format

## Troubleshooting

### Homebrew Issues
If you encounter issues with Homebrew dependencies:
```bash
# Update Homebrew
brew update

# Check for issues
brew doctor

# Reinstall dependencies if needed
brew reinstall sdl2 sdl2_ttf libarchive
```

### Build Dependencies
The build system now automatically handles MuPDF compilation. If you encounter issues, ensure you have:

- **git**: Required to clone MuPDF repository
- **libarchive**: Required for CBR support (via Homebrew)
- **Xcode Command Line Tools**: Required for compilation

### MuPDF Build Issues
If MuPDF fails to build:
1. Ensure you have internet access (required to clone the repository)
2. Check that Homebrew is properly installed and updated
3. Verify `libarchive` is installed: `brew list | grep libarchive`
4. Try cleaning and rebuilding: `make clean && make`

### Path Issues
If you get "command not found" errors, ensure Homebrew's paths are in your shell configuration:
```bash
# For Apple Silicon Macs (M1/M2/M3)
echo 'export PATH="/opt/homebrew/bin:$PATH"' >> ~/.zshrc

# For Intel Macs
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.zshrc

# Reload your shell
source ~/.zshrc
```

### libarchive Detection
The build system automatically detects libarchive via Homebrew. If detection fails:
1. Verify installation: `brew list libarchive`
2. Check the path: `brew --prefix libarchive`
3. Try reinstalling: `brew reinstall libarchive`

### Comic Book Support
- **CBZ/ZIP**: Supported natively by MuPDF
- **CBR**: Now supported via built-in libarchive integration

## Contributing
If you successfully build on different macOS versions or encounter issues, please consider contributing improvements or reporting bugs!
