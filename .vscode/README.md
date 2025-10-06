# VS Code Configuration

This project includes baseline VS Code configuration files to help with C++ development:

- `c_cpp_properties.json`: IntelliSense configuration with proper include paths for SDL2, MuPDF, ImGui, and other dependencies
- `settings.json`: Editor settings and C++ formatter configuration matching the project's coding style

## Local Customization

These files are tracked in git but set to be ignored for local changes using `git update-index --skip-worktree`. This means:

- ✅ Everyone gets the baseline configuration when they clone the repo
- ✅ You can customize these files locally without affecting git
- ✅ Your local changes won't be committed accidentally

## Managing the Configuration

### To see which files are set to skip-worktree:
```bash
git ls-files -v | grep ^S
```

### To stop ignoring changes (if you want to update the baseline):
```bash
git update-index --no-skip-worktree .vscode/c_cpp_properties.json .vscode/settings.json
```

### To start ignoring changes again:
```bash
git update-index --skip-worktree .vscode/c_cpp_properties.json .vscode/settings.json
```

### If you need to pull updates to these files:
```bash
git update-index --no-skip-worktree .vscode/c_cpp_properties.json .vscode/settings.json
git pull
git update-index --skip-worktree .vscode/c_cpp_properties.json .vscode/settings.json
```