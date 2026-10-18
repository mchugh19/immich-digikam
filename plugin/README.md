# Immich plugin for digiKam

A digiKam generic plugin (DPluginGeneric) that syncs with a
self-hosted Immich server: upload the current selection with XMP
sidecars, download albums with their sidecars. Modelled on
digiKam's own web-service plugins (Piwigo in particular).

## Features

- Server URL and API key stored in digiKam's configuration
  (KConfig group "Immich Settings").
- Album list read from the server, sorted by name, with a
  filter box; the selected album is the upload target, shown in
  an "Upload target" label.
- Import source switch (albums or tags): the import tool can
  pull an album or every asset carrying a tag. Selecting a
  source previews its contents as thumbnails (sortable by date
  or name, filterable with the same filter box) before anything
  is downloaded.
- Upload: each selected item is sent with an XMP sidecar taken
  from, in order, an on-disk sidecar next to the file, a sidecar
  generated from the item's metadata in digiKam's database (tags,
  rating, caption, GPS), or the file's embedded XMP packet.
- Download: the selected album's or tag's originals and
  sidecars are written into a chosen folder; files already on
  disk are skipped, but their sidecars are refreshed (server
  sidecar, or one generated from the Immich asset record:
  tags, rating, caption, GPS), so metadata changes made in
  Immich still reach digiKam on a re-download.
- Both transfers run item by item with a progress bar and a
  summary (done / skipped / failed counts).

## Server and API key

Tested against Immich 3.3.3. The API key needs these permissions:
`asset.upload`, `asset.read`, `asset.view`, `asset.download`,
`album.read`, `album.create`, `albumAsset.create`, `tag.read`.

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
