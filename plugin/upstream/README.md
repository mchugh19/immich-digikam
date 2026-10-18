# Dropping this plugin into the digiKam tree

Target: `graphics/digikam`, directory
`core/dplugins/generic/webservices/immich/`, against master.

Steps:

1. Copy the plugin's sources (everything in `plugin/` except
   `tests/`, `upstream/` itself and the standalone
   `CMakeLists.txt`) into the new in-tree directory, including
   the `icons/` subdirectory and `immich_icons.qrc`.
2. Copy `upstream/CMakeLists.txt` over it as the directory's
   `CMakeLists.txt`.
3. Add one line to
   `core/dplugins/generic/webservices/CMakeLists.txt`:
   `add_subdirectory(immich)`.
4. In-tree plugin sources end with a moc include, because the
   tree's plugin builds do not scan these headers the way the
   standalone build's AUTOMOC does. Append to the `.cpp` of
   each `Q_OBJECT` class:
   - `immichplugin.cpp`: `#include "moc_immichplugin.cpp"`
   - `immichwindow.cpp`: `#include "moc_immichwindow.cpp"`
   - `immichtalker.cpp`: `#include "moc_immichtalker.cpp"`
5. The banner in each file says "a part of the immich-digikam
   project"; in-tree it becomes "a part of digiKam project"
   with the digiKam URL. The SPDX lines stay as they are.

Notes for the merge request discussion:

- The window is a plain `DPluginDialog`, not the web-services
  framework's `WSToolDialog` (gap 6 in `../UPSTREAM_GAPS.md`):
  Immich authenticates with a pasted API key, so the
  framework's login flow has nothing to run. This is the one
  deliberate architectural deviation to agree with the
  maintainers.
- `icon()` returns the icon from the embedded resource
  (`immich_icons.qrc`, kept by the in-tree build file). If the
  maintainers prefer the pure theme form, the size set under
  `icons/` is already installed by `icons/CMakeLists.txt` and
  `icon()` becomes `QIcon::fromTheme("dk-immich")`.
- The unit and contract tests under `plugin/tests/` stay in
  this repository (in-tree web-service plugins ship none), and
  are offered in the MR discussion.
