# Font Management Feature

This implementation adds a comprehensive font selection GUI to the SDLReader application that allows users to choose fonts and sizes for EPUB/MOBI documents at runtime.

## Features

- **Font Menu**: Press `M` key to toggle the font selection menu
- **Font Discovery**: Automatically scans the `/fonts` directory for `.ttf` and `.otf` files
- **Live Preview**: Shows "The quick brown fox..." preview text
- **Font Size Control**: Numeric input and slider for font size (8-72pt)
- **Persistent Settings**: Saves configuration to `config.json` and auto-applies on startup
- **CSS Generation**: Automatically generates and applies user CSS via MuPDF's `fz_set_user_css`

## How to Use

1. **Add Fonts**: Place your `.ttf` or `.otf` font files in the `fonts/` directory
2. **Open Font Menu**: Press the `M` key while reading a document
3. **Select Font**: Choose from the dropdown list of available fonts
4. **Adjust Size**: Use the input field or slider to set font size (8-72 points)
5. **Preview**: See a live preview of your selection
6. **Apply**: Click "Apply" to use the font in your document
7. **Save**: Settings are automatically saved and will be restored next time

## Keyboard Controls

- `M` - Toggle font menu
- All existing keyboard controls remain unchanged

## Technical Details

### Files Added/Modified

- `include/font_manager.h` - Font discovery and management
- `src/font_manager.cpp` - Font scanning and CSS generation
- `include/gui_manager.h` - Dear ImGui integration
- `src/gui_manager.cpp` - Font selection GUI
- `include/mupdf_document.h` - Added CSS support
- `src/mupdf_document.cpp` - CSS application via `fz_set_user_css`
- `include/app.h` - Font menu integration
- `src/app.cpp` - Event handling and font application
- `ports/mac/Makefile` - Dear ImGui build integration

### Dependencies

- **Dear ImGui**: Provides the GUI framework
- **MuPDF**: CSS support via `fz_set_user_css` function
- **SDL2**: Existing dependency for windowing

### Configuration

Settings are stored in `config.json` in the executable directory:

```json
{
  "fontPath": "/path/to/font.ttf",
  "fontName": "Font Display Name", 
  "fontSize": 14
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
  font-family: 'UserSelectedFont';
  src: url('file:///path/to/font.ttf');
}

body, *, p, div, span, h1, h2, h3, h4, h5, h6 {
  font-family: 'UserSelectedFont' !important;
  font-size: 14pt !important;
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