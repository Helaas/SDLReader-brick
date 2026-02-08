# TrimUI PAK Template

This directory contains shared template files used by both TG5040 and TG5050 builds.

## Contents

- **launch.sh**: Universal launcher script for TrimUI devices
- **res/docs.pdf**: Documentation PDF shown on first launch

## Usage

These files are automatically copied during the bundle export process:
- `make export-tg5040` → copies to `ports/tg5040/pak/`
- `make export-tg5050` → copies to `ports/tg5050/pak/`
- `make export-trimui` → exports both platforms

## Modifying Template Files

To update the launcher or docs for all TrimUI devices, edit the files in this directory. Changes will be picked up by the next `make export-*` run.
