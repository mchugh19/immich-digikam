# immich-digikam

Sync digiKam albums with a self-hosted Immich server: pull an
album's originals into digiKam, work on them there, push finished
files back. Primary use case: building family collages and
calendars from the Immich library without giving up digiKam's
editing and organisation.

The plugin (`plugin/`) is a digiKam DPlugin (C++/Qt) modeled on
digiKam's own web-service tools: it registers "Export to
Immich..." in the Export menu and "Import from Immich..." in
the Import menu, each opening a shared window in its own mode.
See `plugin/README.md` for the API key permissions and the
full feature list.

## Building

The plugin is an out-of-tree digiKam generic plugin (C++17,
CMake 3.16 or later). The build declares Qt6 (Widgets and
Network), the KF6 Config, I18n, Wallet and KIO frameworks, and
digiKam 9's DigikamCore and DigikamPlugin CMake packages.

On Fedora, those are:

```sh
sudo dnf install cmake gcc-c++ digikam-devel qt6-qtbase-devel \
    kf6-kconfig-devel kf6-ki18n-devel kf6-kwallet-devel kf6-kio-devel
```

Other distributions provide the same Qt6, KF6 and digiKam
development packages under their own names.

Configure and build from the repository root:

```sh
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

This produces `build/libgeneric_immich.so`. The unit tests are
a separate, self-contained CMake project:

```sh
cmake -S plugin/tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Install the plugin into digiKam's generic plugin directory
(`/usr/lib64/qt6/plugins/digikam/generic` on Fedora), with its
icon in the system icon theme:

```sh
sudo cmake --install build
```

digiKam picks the plugin up on its next start.

Development note: to run from the build tree without
installing, `DK_PLUGIN_PATH` must append to digiKam's own
plugin path, not replace it. The variable replaces the whole
plugin directory, image loaders included, so naming the build
directory alone leaves digiKam unable to open any picture:

```sh
DK_PLUGIN_PATH="$PWD/build;/usr/lib64/qt6/plugins/digikam" digikam
```

## How metadata travels

Metadata round-trips through XMP sidecar files, which both
digiKam and Immich read and write natively. Verified live
against Immich 3.3.3: tags, captions, GPS and the rating from a
sidecar are all applied to the asset, the rating in every
encoding (element form, attribute form, and embedded in the
photo's own XMP packet). The rating is stored as the asset's
EXIF rating: read it back as `exifInfo.rating`, because the
3.3.3 asset record has no top-level `rating` field.

Pixel edits do NOT travel in XMP. Only rendered exports
(JPEG/TIFF) carry pixel changes back to Immich.

## Roadmap

Future milestones, following the shapes digiKam's own web-service
plugins use:

- **Import into a digiKam album**: the import tool writes into
  a chosen folder today; plugging into digiKam's import flow so
  downloaded albums land in a digiKam album directly is the
  remaining step.
- **A richer album browser**: the source lists are sorted and
  filterable today, and the import tool previews a source's
  contents; what remains from the full browser is album rename
  and a create dialog with more than a name (the Piwigo
  plugin's album browser is the model). One deliberate
  difference from Piwigo: there is no staged upload queue. The
  selection made in digiKam is the upload set; the window does
  not re-browse or re-stage it.
