# Remaining upstream work

Work still required before this plugin can be merged into digiKam
itself (graphics/digikam on KDE Invent, target location
`core/dplugins/generic/webservices/immich/`).

## Window framework and session handling

The windows derive from `WSToolDialog` and use the standard dialog
button row (Close and the transfer action), matching digiKam's other
web-service tools. The framework's Help button is dropped while the
`immich_export` handbook chapter below does not exist. The plugin keeps its own session class, because
Immich authenticates with a pasted API key and has no OAuth or
login-dialog flow for the shared framework in
`core/libs/dplugins/webservices` to run.

Still to do:

- Raise the separate session class explicitly in the eventual merge
  request description, since it is the one remaining deviation from
  the in-tree web-service pattern.

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
