# Font & Reading Style Menu

This implementation adds a comprehensive font and reading-style menu to SDL Reader, allowing runtime customization of EPUB/MOBI typography, colors, and navigation aids across desktop and TG5040 builds.

## Features

- **Font Menu**: Press `M` (keyboard), `Start` (desktop controllers), or Menu button 10 (TG5040) to toggle the settings overlay
- **Font Discovery**: Automatically scans the `/fonts` directory for `.ttf` and `.otf` files (ships with Inter, JetBrains Mono, Noto Serif Condensed, Roboto) and exposes them in the Options → Font & Reading Style menu
- **Reading Styles**: Built-in color themes (Default, Sepia, Dark, High Contrast, Paper Texture, Soft Gray, Night) applied via CSS
- **Font Size Control**: Numeric input and slider for font size (8-72pt)
- **On-screen Number Pad**: Controller-friendly page jump widget triggered from the menu's "Number Pad" button
- **Persistent Settings**: Saves configuration to `config.json` (font, size, style, zoom step, last browse directory) and auto-applies on startup
- **CSS Generation**: Automatically generates and applies user CSS via MuPDF's `fz_set_user_css`, including `@font-face` declarations

## How to Use

1. **Add Fonts**: Place your `.ttf` or `.otf` font files in the `fonts/` directory (optional—curated fonts ship in-tree) and they’ll appear in the Options → Font & Reading Style picker
2. **Launch a Document**: Open a file directly or start the built-in browser with `./bin/sdl_reader_cli --browse`
3. **Open the Menu**: Press `M` on keyboard, `Start` on desktop controllers, or the TG5040 Menu button (joystick button 10)
4. **Select Font**: Choose from the dropdown list of discovered fonts; the preview updates instantly
5. **Pick a Reading Style**: Choose a theme for background/text colors; defaults to "Document Default"
6. **Adjust Size & Zoom Step**: Use the numeric input or slider to set font size (8–72 pt) and tweak the zoom step
7. **Jump with Number Pad**: Click "Number Pad" for a controller-friendly page jump pad; confirm with `Go`
8. **Apply Changes**: Click "Apply" to reload the document with the new CSS and persist the configuration
9. **Close or Reset**: Use "Close" to exit without saving or "Reset to Default" to revert to document defaults

## Keyboard Controls

- `M` - Toggle font menu
- All existing keyboard controls remain unchanged

## Technical Details

### Files Added/Modified

- `include/options_manager.h` / `src/options_manager.cpp` - Font discovery, CSS generation, reading-style metadata, config persistence
- `include/gui_manager.h` / `src/gui_manager.cpp` - Dear ImGui UI (font picker, reading style combo, number pad)
- `include/input_manager.h` / `src/input_manager.cpp` - New logical actions (`ToggleFontMenu`, number pad navigation)
- `include/mupdf_document.h` / `src/mupdf_document.cpp` - CSS application via `fz_set_user_css` and pre-open injection
- `include/app.h` / `src/app.cpp` - Event wiring, callbacks, document reload sequencing
- `src/render_manager.cpp` - Overlay updates for page/scale indicators after reloads
- `ports/*/Makefile` - Dear ImGui build integration per platform

### Dependencies

- **Dear ImGui**: Provides the GUI framework
- **MuPDF**: CSS support via `fz_set_user_css` function
- **SDL2**: Existing dependency for windowing

### Configuration

Settings are stored in `config.json` inside the reader state directory (`$SDL_READER_STATE_DIR`, defaulting to `$HOME`):

```json
{
  "fontPath": "./fonts/Inter-Regular.ttf",
  "fontName": "Inter Regular",
  "fontSize": 16,
  "zoomStep": 10,
  "readingStyle": 0,
  "lastBrowseDirectory": "/path/to/library"
}
```

### CSS Generation

The system generates CSS that:
1. Registers the font via `@font-face` with file path
2. Forces application to all text elements with `!important`
3. Sets both font family and size

Example generated CSS:
```css
@font-face {
  font-family: 'Inter Regular';
  src: url('./fonts/Inter-Regular.ttf');
}

body {
  background-color: #f4ecd8 !important; /* Sepia */
  color: #5c4a3a !important;
}

*, p, div, span, h1, h2, h3, h4, h5, h6 {
  font-family: 'Inter Regular' !important;
  font-size: 16pt !important;
  line-height: 1.4 !important;
  color: #5c4a3a !important;
}
```

## Supported Formats

- **Font Files**: TTF, OTF
- **Document Types**: EPUB, MOBI (any format supporting CSS via MuPDF)
- **Font Sources**: Local files in `/fonts` directory

## Limitations

- Only works with documents that support CSS styling (EPUB/MOBI)
- PDF documents do not support runtime font changes
- Font preview in GUI uses default system font (actual document uses selected font)

## Building

The build system automatically downloads and compiles Dear ImGui v1.90.9. No additional setup required beyond existing SDL2 dependencies.

```bash
make mac  # for macOS
```

## Troubleshooting

- **No fonts showing**: Check that `.ttf` or `.otf` files are in the `fonts/` directory
- **CSS not applying**: Ensure document is EPUB/MOBI format (PDF not supported)
- **Font not loading**: Verify font file is not corrupted and has correct permissions
- **Menu not appearing**: Check that ImGui initialized correctly (see console output)
