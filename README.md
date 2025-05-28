# SDL Reader

A minimal document reader built using SDL2, MuPDF, and DjVuLibre, supporting PDF and DjVu file formats.

## Table of Contents
* [Features](#features)
* [Supported Document Types](#supported-document-types)
* [Build Instructions](#build-instructions)
* [Usage](#usage)
* [User Inputs](#user-inputs)
* [Project Structure](#project-structure)

## Features
* View PDF and DjVu documents.
* Page navigation (next/previous page).
* Zoom in/out.
* Scroll within pages (if zoomed in or page is larger than window).
* Toggle fullscreen mode.
* Basic UI overlay showing current page and zoom level.

## Supported Document Types
* **PDF** (`.pdf`)
* **DjVu** (`.djvu`)

## Build Instructions
To build this project, you will need:
* A C++17 compatible compiler (e.g., g++).
* `SDL2` development libraries.
* `SDL2_ttf` development libraries.
* `MuPDF` development libraries.
* `DjVuLibre` development libraries.
* `pkg-config` (to find library paths automatically).
* `make` (for the provided Makefile).

**On macOS (using Homebrew):**
1.  Install dependencies:
    ```bash
    brew install sdl2 sdl2_ttf mupdf-tools djvulibre pkg-config
    ```
2.  Navigate to the project root directory.
3.  Build the project:
    ```bash
    make
    ```
    This will create the executable `sdl_reader` in the `bin/` directory.

## Usage
After building, run the executable from your project root, providing the path to a PDF or DjVu file as an argument:

```bash
./bin/sdl_reader path/to/your_document.pdf
```
or
```bash
./bin/sdl_reader path/to/your_document.djvu
```

## User Inputs
The SDL Reader supports the following keyboard and mouse inputs:

| Input                  | Action                                  | Notes                                                              |
| :--------------------- | :-------------------------------------- | :----------------------------------------------------------------- |
| **Keyboard** |                                         |                                                                    |
| `Q` or `Esc`           | Quit application                        | Closes the reader window.                                          |
| `Right Arrow`          | Scroll right                            | Moves the view 32 pixels to the right.                             |
| `Left Arrow`           | Scroll left                             | Moves the view 32 pixels to the left.                              |
| `Up Arrow`             | Scroll up                               | Moves the view 32 pixels up.                                       |
| `Down Arrow`           | Scroll down                             | Moves the view 32 pixels down.                                     |
| `Page Down`            | Go to next page                         | Displays the subsequent page; resets scroll position to top-left.  |
| `Page Up`              | Go to previous page                     | Displays the preceding page; resets scroll position to top-left.   |
| `=` (Equals)           | Zoom in                                 | Increases zoom by 10%.                                             |
| `-` (Minus)            | Zoom out                                | Decreases zoom by 10%.                                             |
| `F`                    | Toggle Fullscreen                       | Switches between windowed and fullscreen desktop modes.            |
| `G`                    | Jump to Page (Not Implemented)          | Currently a placeholder; does not perform any action.              |
| **Mouse** |                                         |                                                                    |
| `Mouse Wheel Up`       | Scroll up                               | Scrolls vertically up by 32 pixels.                                |
| `Mouse Wheel Down`     | Scroll down                             | Scrolls vertically down by 32 pixels.                              |
| `Ctrl + Mouse Wheel Up`| Zoom in                                 | Increases zoom by 5% per scroll tick.                              |
| `Ctrl + Mouse Wheel Down`| Zoom out                              | Decreases zoom by 5% per scroll tick.                              |
| `Left Click + Drag`    | Pan/Scroll                              | Drags the page view around (useful when zoomed in).                |
