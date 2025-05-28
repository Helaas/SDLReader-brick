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

| Input                  | Action                                  |
| :--------------------- | :-------------------------------------- |
| **Keyboard** |                                         |
| `Q` or `Esc`           | Quit application                        |
| `Right Arrow`          | Scroll right                            |
| `Left Arrow`           | Scroll left                             |
| `Up Arrow`             | Scroll up                               |
| `Down Arrow`           | Scroll down                             |
| `Page Down`            | Go to next page                         |
| `Page Up`              | Go to previous page                     |
| `=` (Equals)           | Zoom in                                 |
| `-` (Minus)            | Zoom out                                |
| `F`                    | Toggle Fullscreen                       |
| `G`                    | Jump to Page (Not Implemented)          |
| **Mouse** |                                         |
| `Mouse Wheel Up`       | Scroll up                               |
| `Mouse Wheel Down`     | Scroll down                             |
| `Ctrl + Mouse Wheel Up`| Zoom in                                 |
| `Ctrl + Mouse Wheel Down`| Zoom out                              |
| `Left Click + Drag`    | Pan/Scroll                              |
