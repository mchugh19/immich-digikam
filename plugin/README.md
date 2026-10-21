# Immich plugin for digiKam

A digiKam generic plugin (DPluginGeneric) that syncs with a
self-hosted Immich server: upload the current selection with XMP
sidecars, download albums with their sidecars. Modelled on
digiKam's own web-service plugins (Piwigo in particular).

## Features

- Two tools sharing one window: "Export to Immich..." uploads
  digiKam's current selection; "Import from Immich..."
  downloads an album or every asset carrying a tag.
- Server URL and other settings are stored in digiKam's
  configuration; the API key is stored in KWallet, with the
  configuration as fallback when no wallet is available.
- Sources are album and tag tiles, sorted by name, with a
  filter box; selecting one previews its contents as sortable
  thumbnails. A spinner overlays the album area while the
  album list loads.
- Export target is a radio choice, remembered between runs:
  Library only (the default) or the selected album.
- Uploads carry an XMP sidecar generated from digiKam's
  database metadata (tags, rating, caption, GPS), merged
  over the file's existing XMP so properties it does not set,
  labels included, survive.
- Downloads write originals and sidecars into the folder
  shown in the window's "Download to" field, remembered
  between runs. Files already on disk are skipped, but
  their sidecars are refreshed, so metadata changed in
  Immich still reaches digiKam.
- Re-exporting an edited photo never creates a duplicate:
  the file is recognised on the server and its changes are
  synced or used to replace the old copy (see below).
- Files the server already has are not re-uploaded; their
  metadata is diffed and only the differences are written,
  with tags add-only.
- Transfers run with a progress bar and end with a one-line
  summary of outcome counts.
- Thumbnails are cached on disk (about 200 MB, oldest pruned
  first); album contents are cached for the session, and
  clicking Connect again reloads them.

## Re-exporting: what to expect

Re-exporting the same selection is safe: what happens to
each file depends on what changed since the last export.
Recognition uses the file name, capture time and dimensions,
confirmed by comparing pixels with the server's thumbnail;
per-asset snapshots of the last sync (in digiKam's app data)
decide which side changed.

- **Nothing changed.** Checksum match: nothing is uploaded,
  and the metadata sync normally finds nothing to write.
- **Only metadata changed.** The pixels compare identical,
  so nothing is uploaded; the changed tags, rating, caption
  and GPS are written to the existing asset through the API.
  When both sides changed since the last sync, tags merge
  and scalars go to the newer side by timestamp, reported
  as a conflict; when only Immich changed, the asset is
  left alone.
- **The picture itself was edited.** The new version is
  uploaded into every album the old copy belonged to. The
  "Edited photos:" choice decides the old copy: **Stack**
  (the default) puts the new version on top of it as a stack
  primary; **Overwrite** sends it to Immich's trash
  (recoverable for 30 days). The replacement is a new asset:
  shared links do not carry over, face assignments
  re-derive, and Immich-editor edits on the old copy are
  lost.
- **The photo was cropped or resized.** Dimensions no
  longer match, so recognition rests on file name and exact
  capture time, and it replaces only after a containment
  check confirms the local image is a crop of the server's
  photo.
- **The evidence is ambiguous.** Nothing is overwritten or
  stacked; the file uploads as a new photo. With several
  copies on the server, the newest is the match.

Good to know:

- Recognition needs the file name and EXIF capture time to
  survive the edit; a renamed file counts as new.
- Files the plugin cannot decode (RAW and similar) count
  as unchanged pixels: their metadata syncs and they are
  never replaced or stacked.
- Tag sync is add-only everywhere: removing a tag in
  digiKam does not remove it in Immich.
- Stacking needs `stack.create` and `stack.read`; if the
  server refuses, the old copy falls back to the trash
  (needs `asset.delete`). Overwrite needs `asset.delete`
  directly.

## Server and API key

Tested against Immich 3.3.3. The API key needs:

- Base: `asset.upload`, `asset.read`, `asset.view`,
  `asset.download`, `album.read`, `album.create`,
  `albumAsset.create`, `tag.read`.
- Metadata sync: `asset.update`, `tag.create`, `tag.asset`
  (without them the sync is refused and the summary says so;
  uploads and downloads are unaffected).
- Stacking replaced photos: `stack.create`, `stack.read`.
- Overwrite, and the trash fallback when stacking is
  refused: `asset.delete`.

## Build

In a Fedora toolbox (43 or 44) matching your digiKam:

```sh
sudo dnf install cmake gcc-c++ digikam-devel qt6-qtbase-devel \
    kf6-kconfig-devel kf6-ki18n-devel kf6-kwallet-devel kf6-kio-devel
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

This produces `build/libgeneric_immich.so`.

## Run and test

**Do not point `DK_PLUGIN_PATH` at the build directory on its
own.** digiKam treats that variable as a *replacement* for its
whole plugin directory, which also holds the `dimg/` image
loaders: with only the build dir scanned, digiKam starts without
JPEG/PNG/HEIF decoders and cannot open any picture.

Either copy the library into the real plugin directory and start
digiKam normally:

```sh
sudo cp build/libgeneric_immich.so /usr/lib64/qt6/plugins/digikam/generic/
digikam
```

(re-copy after each rebuild), or name both paths, separated by a
semicolon:

```sh
DK_PLUGIN_PATH="$PWD/build;/usr/lib64/qt6/plugins/digikam" digikam
```

Then: Plugins settings should list Immich, the Export menu gains
"Export to Immich..." and the Import menu gains "Import from
Immich...". Each opens the shared window in its own mode, and
Connect should list the server's albums.

## Unit tests

The pure logic that carries the behavioral contracts (the XMP
sidecar builder, the preservation merge, sync planning, field
hashing, snapshots and the snapshot gating rules) has a Qt Test
suite under `tests/`, with no digiKam or network dependency:

```sh
cmake -S tests -B build-tests && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```
