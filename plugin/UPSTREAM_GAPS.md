# Remaining upstream work

Work still required before this plugin can be merged into digiKam
itself (graphics/digikam on KDE Invent, target location
`core/dplugins/generic/webservices/immich/`).

## Maintainer decision on the window framework

In-tree web-service windows derive from `WSToolDialog` and reuse the
shared framework in `core/libs/dplugins/webservices`. This plugin's
window is a plain `DPluginDialog` with its own session class, because
Immich authenticates with a pasted API key and has no OAuth or
login-dialog flow for the framework to run.

Still to do:

- Send the maintainer feeler and get a decision on whether this
  deviation is acceptable.
- Raise the same point explicitly in the eventual merge request
  description.
- If the maintainers require the framework dialog, rework the window
  and session handling to use it.

## In-tree integration against digiKam master

The merge target is digiKam master, not the 9.1 release used for the
standalone build.

Still to do:

- Copy the plugin into `core/dplugins/generic/webservices/immich/`
  using the prepared build file from `plugin/upstream/`.
- Register it with `add_subdirectory(immich)` in
  `core/dplugins/generic/webservices/CMakeLists.txt`.
- Append the in-tree moc includes to `immichplugin.cpp`,
  `immichwindow.cpp` and `immichtalker.cpp`.
- Change the source banners' project line to "digiKam project" when
  the code lands in-tree.
- Build the in-tree plugin and retest it against digiKam master.

## Handbook chapter

Write the `immich_export` chapter for the Export Tools section of
the digiKam manual and submit it to the separate documentation
repository as a follow-up contribution.
