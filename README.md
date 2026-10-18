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
See `plugin/README.md` for building, installing, the API key
permissions and the full feature list.

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
