# Application icons

The application icon is our procedural crown from `src/util/appicon.cpp`, not a
retail asset. Regenerate the checked-in Windows and macOS icon containers with:

```sh
cmake --build build --target makeicons
python3 tools/make-platform-icons.py build/makeicons
```

The generator uses Python's standard library and the existing C++ renderer. It
adds no runtime dependency or normal-build requirement. Windows embeds the ICO
in `takclient.exe` through a compiled resource, and NSIS uses it for the installer
and uninstaller. macOS copies the ICNS into `Contents/Resources` and declares it
in `Info.plist`.

The macOS workflow retains `.app` on disk and marks its extension hidden in Finder.
User preferences can force extensions to remain visible. The DMG preserves this
metadata; the ZIP is created with `ditto` and distributed as an intact archive
so its extended attributes survive downloads. Native CI validates the ICNS with
`iconutil` and verifies the bundle signature after setting its display metadata.
