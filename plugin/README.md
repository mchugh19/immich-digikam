# Immich plugin for digiKam

A digiKam generic plugin (DPluginGeneric) that syncs with a
self-hosted Immich server: upload the current selection with XMP
sidecars, download albums with their sidecars. Modelled on
digiKam's own web-service plugins (Piwigo in particular).

## Features

- Server URL and API key stored in digiKam's configuration
  (KConfig group "Immich Settings").
- Sources are shown as tiles in the shape of Immich's own
  views: an album tile is a large cover with the name under it
  in a larger font, then the date range and the item count in
  smaller type; a tag tile is the tag icon and its name. Tiles
  are sorted by name and have a filter box; the selected album
  is the upload target, shown in an "Upload target" label.
- Import source switch (albums or tags): the import tool can
  pull an album or every asset carrying a tag. Selecting a
  source previews its contents as thumbnails (sortable by date
  or name) before anything is downloaded. The filter box
  narrows the source tiles only, never the preview.
- Upload: each selected item is sent with an XMP sidecar
  generated from the item's metadata in digiKam's database
  (tags, rating, caption, GPS: digiKam's current state wins,
  so a stale sidecar left next to a file cannot override fresh
  edits), merged over whatever XMP the file already carries
  (its own sidecar, else its embedded packet) so properties
  the generator does not set, digiKam's label properties
  included, are preserved verbatim. When digiKam's database
  holds nothing for an item, the file's own sidecar or
  embedded packet travels as it stands.
- Download: the selected album's or tag's originals and
  sidecars are written into a chosen folder; files already on
  disk are skipped, but their sidecars are refreshed (server
  sidecar, or one generated from the Immich asset record:
  tags, rating, caption, GPS), so metadata changes made in
  Immich still reach digiKam on a re-download.
- No duplicates for edited files: Immich identifies a
  photo by content checksum, so a file whose bytes changed
  (digiKam writes metadata into files, or the photo was
  edited) would normally upload as a second copy. Before
  uploading such a file, the plugin looks for the same photo
  on the server: within the album being exported to, the
  file name is the primary key, verified against capture
  time and dimensions, with a library-wide search as a
  second pass, and the pixels are compared with the server's
  thumbnail. A metadata-only change uploads nothing: the
  metadata sync below updates the existing asset. A real
  edit (changed pixels, same capture time) uploads the new
  version, puts it in every album the old copy belonged to,
  syncs its metadata, and stacks it as the new primary over
  the old copy, which stays in the library inside the stack
  (repeated edits accumulate as a version stack, newest on
  top). A crop changes the dimensions, so it matches on file
  name and exact capture time alone, and only replaces
  after a containment check confirms the local image really
  is a crop of the server's photo. When the evidence is
  ambiguous (no matching capture time, several candidates),
  nothing is overwritten: the file uploads as new, or syncs
  to the newest matching copy. See "Re-exporting: what to
  expect" below for the full set of cases.
- Metadata sync on export: a selected file the server
  already has (checksum match) is not re-uploaded, but its
  digiKam tags, rating, caption and GPS are diffed against
  the server's asset record and only the differences are
  written through the API (PUT /assets, the tags endpoints).
  Tag sync is add-only: a tag removed in digiKam is not
  removed in Immich, and a tag set in Immich is never deleted
  by an export. Per-asset snapshots of the last sync are
  kept (in digiKam's app data, immich-sync-state.json): when
  both sides changed a photo's scalars since then, the newer
  change wins by timestamp and the run reports it as a
  conflict; when only Immich changed, the export leaves the
  asset alone. Import refreshes write a local sidecar only
  when its content actually differs.
- Both transfers run item by item with a progress bar and a
  summary (done / skipped / failed counts).
- Caching: thumbnails (album covers and contents previews)
  are kept on disk in digiKam's cache directory and reused
  across sessions, pruned oldest-first past roughly 200 MB.
  Contents listings are cached for the session; Refresh is
  the invalidation, and an upload drops its target album's
  cached listing only when the album actually gained assets.

## Re-exporting: what to expect

Exporting the same selection twice is safe, and editing a
photo in digiKam between exports does not create duplicates
in Immich. What happens to each file depends on what changed:

- **Nothing changed.** The file's checksum matches the
  server's copy. Nothing is uploaded; the metadata sync runs
  and normally finds nothing to write.
- **Only metadata changed** (tags, rating, caption, GPS),
  whether digiKam kept it in its database or wrote it into
  the file. The photo is recognised by file name (within the
  album being exported to), capture time and dimensions, and
  its pixels compare identical to the server's copy. Nothing
  is uploaded; the changed metadata is written to the
  existing Immich asset through the API.
- **The picture itself was edited** (colour, retouching, any
  change that alters pixels), with the same capture time.
  The new version is uploaded and added to every album the
  old copy belonged to. What happens to the old copy is the
  "Edited photos:" choice in the export window: **Stack**
  (the default) makes the new version the primary of a stack
  over the old copy, which is not deleted: it stays in the
  stack, visible in Immich's stack view, and repeated edits
  keep adding versions, newest on top. **Overwrite** sends
  the old copy to Immich's trash instead (recoverable for 30
  days). Metadata-only changes never reach this choice: they
  always sync onto the existing asset, as above. Either way,
  the replaced asset is a new asset, so the old copy's
  shared links do not carry over, its face assignments
  re-derive from recognition, and any edits made with
  Immich's own editor on the old copy do not transfer.
- **The photo was cropped or resized.** Dimensions no longer
  match, so recognition rests on the file name and the exact
  capture time, and the new version replaces only after a
  containment check confirms the local image is really a
  crop of the server's photo. It is then stacked like any
  other edit.
- **The evidence is ambiguous.** A different photo that
  merely shares a file name, a missing or unmatched capture
  time, or a failed containment check means nothing is
  overwritten and nothing is stacked: the file uploads as a
  new photo. When a photo already has several copies on the
  server, the newest is the match and the summary says so.

The run summary at the end of an export reports each of
these outcomes with its own count: uploaded, already on the
server, matched (metadata synced instead of re-uploading),
replaced and stacked, and conflicts settled by timestamp.

Expectations to know before relying on this:

- Recognition needs the file name and the EXIF capture time
  to survive the edit. Both do in digiKam's normal editing
  flow; a file that was renamed, or whose capture time was
  rewritten, is treated as a new photo.
- RAW files and other formats the plugin cannot decode get
  the benefit of the doubt: their pixels count as unchanged,
  so their metadata syncs and they are never replaced or
  stacked by an export. A developed version of a RAW photo is
  a different file and uploads as its own photo.
- Tag sync is add-only in both the metadata sync and the
  sidecars: removing a tag in digiKam does not remove it in
  Immich.
- Stacking needs the `stack.create` and `stack.read` API key
  permissions. If the server refuses to stack, the replace
  falls back to moving the old copy to Immich's trash
  (recoverable for 30 days, needs `asset.delete`) rather
  than leaving two loose copies. The Overwrite choice needs
  `asset.delete` directly.

## Server and API key

Tested against Immich 3.3.3. The API key needs these permissions:
`asset.upload`, `asset.read`, `asset.view`, `asset.download`,
`album.read`, `album.create`, `albumAsset.create`, `tag.read`.
The export metadata sync additionally needs `asset.update`,
`tag.create` and `tag.asset`; without them the sync is refused
by the server and the run's summary says so (uploads and
downloads are unaffected). Stacking a replaced photo over its
previous version additionally needs `stack.create` and
`stack.read`; if stacking is refused, the replace falls back
to moving the old copy to Immich's trash, which needs
`asset.delete`.

## Build

In a Fedora toolbox (43 or 44) matching your digiKam:

```sh
sudo dnf install digikam-devel kf6-kconfig-devel kf6-ki18n-devel
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
Refresh albums should list the server's albums.

## Unit tests

The pure logic that carries the behavioral contracts (the XMP
sidecar builder, the preservation merge, sync planning, field
hashing, snapshots and the snapshot gating rules) has a Qt Test
suite under `tests/`, with no digiKam or network dependency:

```sh
cmake -S tests -B build-tests && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```
