# Crash Forge Character Tool

A small companion utility for **Crash Forge Racing** that builds ready-to-drop custom racer folders without requiring manual `character.ini` editing.

## What it does

- Imports a Wavefront OBJ model.
- Reads its MTL file and referenced textures.
- Copies OBJ, MTL and textures into a racer mod folder.
- Rewrites material texture paths to clean relative filenames.
- Generates `character.ini` automatically.
- Supports retail fallback characters and engine classes.
- Supports retail roster icons or custom PNG/BMP icon files.
- Supports model scale and XYZ offsets.
- Detects the optional wheel groups:
  - `wheel_fl`
  - `wheel_fr`
  - `wheel_rl`
  - `wheel_rr`
- Validates model limits, materials and missing textures.

## Running

On Windows, double-click:

```text
Open Crash Forge Character Tool.bat
```

Or run directly with Python 3:

```bash
python crash_forge_character_tool.py
```

The UI uses Python's built-in Tkinter.

## Output

The tool creates:

```text
assets/mods/racers/<asset_name>/
```

with files such as:

```text
character.ini
<asset_name>.obj
<asset_name>.mtl
textures...
<asset_name>_icon.png
```

If this tool is placed anywhere inside the Crash Forge Racing source tree, it automatically detects the repository root and defaults the output to:

```text
<source>/assets/mods/racers/
```

If it is run standalone, it defaults to a local `output/` folder. The output location can always be changed from the UI.

## Character configuration

The generated `character.ini` includes the values selected in the tool:

```ini
[character]
name = Example Racer
enabled = true
fallback_retail = crash
engine = balanced
has_wheels = false

[model]
scale = 1.0
offset_x = 0.0
offset_y = 0.0
offset_z = 0.0

[assets]
asset_name = example_racer
icon = retail:crash
```

## Model notes

- Asset names use lowercase letters, numbers and underscores.
- The current native OBJ path supports PNG and BMP textures.
- A custom icon is recommended at **128x128**.
- If the OBJ includes independently animated wheel groups, enable the matching wheel option in the tool.
- Custom icon paths are already stored by Crash Forge Racing; some roster rendering paths may still use the selected retail fallback until custom icon rendering is completed.

## Project

This utility is part of **Crash Forge Racing** and is intended to live under the source repository's development tools.
