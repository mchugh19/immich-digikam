# immich-digikam

Sync digiKam albums with a self-hosted Immich server: pull an
album's originals into digiKam, work on them there, push finished
files back. Primary use case: building family collages and
calendars from the Immich library without giving up digiKam's
editing and organisation.

The repository has two parts:

- **Prototype** (this directory): a Python CLI that validates the
  API contract and the XMP round-trip against a live server. It is
  the reference implementation for the plugin's behaviour.
- **Plugin** (`plugin/`): the digiKam DPlugin (C++/Qt) port, for an
  in-GUI flow with a settings page and an album browser. Scaffold
  stage: plugin/session/talker/window classes exist and the talker
  implements the verified endpoints; it has not been compiled yet
  (first build is in a Fedora container with digikam-devel).

## Prototype

Pull an Immich album's originals into a local folder for digiKam, then push
finished files back. Metadata round-trips through XMP sidecar files, which
both digiKam and Immich read and write natively. Verified live against
Immich 3.3.3 (2026-10-18): on push, tags, captions, GPS and the rating
from the sidecar are all applied to the asset, the rating in every
encoding tried (element form, attribute form, and embedded in the
photo's own XMP packet). The rating is stored as the asset's EXIF
rating: read it back as `exifInfo.rating`, because the 3.3.3 asset
record has no top-level `rating` field.

Pixel edits do NOT travel in XMP. Only rendered exports (JPEG/TIFF) carry
pixel changes back to Immich.

## Setup

1. In Immich: Account Settings -> API Keys -> New API Key.
   Permissions needed: `asset.create asset.read asset.download asset.view
   album.read album.create albumAsset.create`
2. `mkdir -p ~/.config/immich-digikam`
3. Create `~/.config/immich-digikam/config.json` (mode 0600):
   `{"server_url": "https://photos.example.com", "api_key": "..."}`

## Usage

```
# download album "Summer 2026" into ~/Pictures/immich-Summer-2026
immich-digikam pull "Summer 2026"

# after editing/exporting in digiKam, upload new/changed files
# and add them to the album (create it if missing)
immich-digikam push --dir ~/Pictures/immich-Summer-2026 \
    --album "Summer 2026" --create-album
```

`pull` writes a `.immich-manifest.json` mapping files to Immich asset IDs.
`push` uploads only files that are new or changed since the manifest, and
includes a same-named `.xmp` sidecar when one sits next to the file.

## The plugin port

`plugin/` ports this logic to a digiKam DPlugin (DPluginGeneric),
modeled on digiKam's own web-service exporters: a settings page
for the server URL and API key, an album browser, and pull/push
driven from the digiKam selection. Class layout mirrors the
Piwigo plugin (plugin / session / talker / window); the talker's
endpoints and upload fields are the ones the prototype verified.

## Roadmap

Future milestones, following the shapes digiKam's own web-service
plugins use:

- **A separate import plugin** for pull, the way the Google
  services pair an export tool with an import tool. Today pull
  lives inside the export dialog and writes originals plus
  sidecars into a folder; a DPluginImport would plug into
  digiKam's import flow, so downloaded albums land in a digiKam
  album directly rather than in a folder the user then scans.
- **A remote album browser**: navigate the server's album tree
  inside the plugin (search, create, rename, asset counts) as the
  target picker for both directions, instead of the current flat
  album list. The Piwigo plugin's album browser is the model.
- **Sidecars generated from digiKam's item metadata**, so tags,
  rating and captions that exist only in digiKam's database (not
  yet written to the file or an on-disk sidecar) also round-trip
  on upload, matching what the Python prototype does from the
  digiKam database.
- **An icon set** for the plugin's actions (the `dk-immich`
  theme icons are currently a placeholder).
