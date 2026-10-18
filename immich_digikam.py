#!/usr/bin/env python3
"""immich-digikam bridge prototype.

Pull an Immich album's originals into a local working folder for digiKam,
then push finished files back to Immich. Metadata (tags, ratings, captions)
round-trips through XMP sidecar files, which both digiKam and Immich read
and write natively.

Usage:
    immich-digikam pull "Album Name" [--dest DIR]
    immich-digikam push [--dir DIR] [--album "Album Name"] [--create-album]

Config: ~/.config/immich-digikam/config.json  (mode 0600)
    {"server_url": "https://photos.example.com", "api_key": "..."}
"""

import argparse
import hashlib
import json
import os
import sys
import urllib.request
import urllib.error
from datetime import datetime, timezone
from xml.sax.saxutils import escape

CONFIG_PATH = os.path.expanduser("~/.config/immich-digikam/config.json")
MANIFEST_NAME = ".immich-manifest.json"
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".tif", ".tiff", ".heic", ".heif",
              ".webp", ".dng", ".cr2", ".cr3", ".nef", ".arw", ".orf", ".rw2"}


def load_config():
    if not os.path.exists(CONFIG_PATH):
        sys.exit(f"config not found: {CONFIG_PATH}\n"
                 "create it as {\"server_url\": \"...\", \"api_key\": \"...\"} "
                 "with mode 0600")
    with open(CONFIG_PATH) as f:
        cfg = json.load(f)
    if not cfg.get("server_url") or not cfg.get("api_key"):
        sys.exit("config needs server_url and api_key")
    return cfg["server_url"].rstrip("/"), cfg["api_key"]


class Immich:
    def __init__(self, server_url, api_key):
        self.base = server_url + "/api"
        self.key = api_key

    def _req(self, method, path, data=None, headers=None, timeout=120):
        req = urllib.request.Request(self.base + path, data=data, method=method)
        req.add_header("x-api-key", self.key)
        req.add_header("Accept", "application/json")
        for k, v in (headers or {}).items():
            req.add_header(k, v)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                body = r.read()
                ctype = r.headers.get("Content-Type", "")
                if "application/json" in ctype and body:
                    return json.loads(body)
                return body
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")[:500]
            sys.exit(f"API {method} {path} -> HTTP {e.code}: {detail}")

    def get(self, path):
        return self._req("GET", path)

    def post_json(self, path, payload):
        return self._req("POST", path, data=json.dumps(payload).encode(),
                         headers={"Content-Type": "application/json"})

    def put_json(self, path, payload):
        return self._req("PUT", path, data=json.dumps(payload).encode(),
                         headers={"Content-Type": "application/json"})

    def download(self, path, dest):
        data = self._req("GET", path)
        with open(dest, "wb") as f:
            f.write(data)

    def upload(self, file_path, sidecar_path=None):
        """Multipart upload. Returns the asset dict from the server."""
        boundary = "----immichdigikam%d" % os.getpid()
        fields = {
            "deviceAssetId": "digikam-" + hashlib.sha1(
                os.path.abspath(file_path).encode()).hexdigest()[:32],
            "deviceId": "digikam-bridge",
            "fileCreatedAt": datetime.fromtimestamp(
                os.path.getmtime(file_path), tz=timezone.utc).isoformat(),
            "fileModifiedAt": datetime.fromtimestamp(
                os.path.getmtime(file_path), tz=timezone.utc).isoformat(),
        }
        body = b""
        for k, v in fields.items():
            body += (f"--{boundary}\r\nContent-Disposition: form-data; "
                     f"name=\"{k}\"\r\n\r\n{v}\r\n").encode()
        with open(file_path, "rb") as f:
            fdata = f.read()
        body += (f"--{boundary}\r\nContent-Disposition: form-data; "
                 f"name=\"assetData\"; filename=\"{os.path.basename(file_path)}\"\r\n"
                 f"Content-Type: application/octet-stream\r\n\r\n").encode()
        body += fdata + b"\r\n"
        if sidecar_path and os.path.exists(sidecar_path):
            with open(sidecar_path, "rb") as f:
                sdata = f.read()
            body += (f"--{boundary}\r\nContent-Disposition: form-data; "
                     f"name=\"sidecarData\"; filename=\"{os.path.basename(sidecar_path)}\"\r\n"
                     f"Content-Type: application/xml\r\n\r\n").encode()
            body += sdata + b"\r\n"
        body += f"--{boundary}--\r\n".encode()
        return self._req("POST", "/assets", data=body, headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}"},
            timeout=600)


def write_xmp_sidecar(path, tags=(), rating=0, description=""):
    """Write a minimal XMP sidecar digiKam and Immich both understand."""
    tag_items = "\n".join(
        f"      <rdf:li>{escape(t)}</rdf:li>" for t in tags)
    xmp = f"""<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:dc="http://purl.org/dc/elements/1.1/"
    xmlns:xmp="http://ns.adobe.com/xap/1.0/"
    xmlns:digiKam="http://www.digikam.org/ns/1.0/">
   <dc:description><rdf:Alt><rdf:li xml:lang="x-default">{escape(description)}</rdf:li></rdf:Alt></dc:description>
   <xmp:Rating>{int(rating)}</xmp:Rating>
   <digiKam:TagsList><rdf:Bag>
{tag_items}
   </rdf:Bag></digiKam:TagsList>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(xmp)


def find_album(imm, name):
    albums = imm.get("/albums")
    matches = [a for a in albums if a.get("albumName") == name]
    if not matches:
        sys.exit(f"no album named {name!r} "
                 f"({len(albums)} albums on server)")
    if len(matches) > 1:
        sys.exit(f"{len(matches)} albums named {name!r}; be more specific")
    return matches[0]


def album_assets(imm, album_id):
    """All assets in an album via the metadata search (Immich 2.x/3.x)."""
    assets = []
    page = 1
    while True:
        res = imm.post_json("/search/metadata",
                            {"albumIds": [album_id], "size": 100,
                             "page": page})
        items = res.get("assets", {}).get("items", [])
        if not items:
            break
        assets.extend(items)
        if res.get("assets", {}).get("nextPage") is None:
            break
        page = res["assets"]["nextPage"]
    # full DTOs carry tags/rating/exif; the search DTO is truncated
    return [imm.get(f"/assets/{a['id']}") for a in assets]


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def cmd_pull(imm, album_name, dest):
    album = find_album(imm, album_name)
    assets = album_assets(imm, album["id"])
    print(f"album {album_name!r}: {len(assets)} assets -> {dest}")
    os.makedirs(dest, exist_ok=True)
    manifest = {}
    for i, a in enumerate(assets, 1):
        fname = a.get("originalFileName") or f"{a['id']}.bin"
        # avoid collisions on duplicate filenames
        target = os.path.join(dest, fname)
        n = 1
        while os.path.exists(target):
            stem, ext = os.path.splitext(fname)
            target = os.path.join(dest, f"{stem}-{n}{ext}")
            n += 1
        print(f"  [{i}/{len(assets)}] {os.path.basename(target)}")
        imm.download(f"/assets/{a['id']}/original", target)
        # sidecar from Immich metadata so digiKam sees tags/ratings
        tags = [t.get("value") or t.get("name", "") for t in a.get("tags", [])]
        write_xmp_sidecar(target + ".xmp", tags=[t for t in tags if t],
                          rating=a.get("rating") or 0,
                          description=a.get("exifInfo", {}).get(
                              "description") or "")
        manifest[os.path.basename(target)] = {
            "asset_id": a["id"], "sha1": sha1_of(target)}
    with open(os.path.join(dest, MANIFEST_NAME), "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"done. manifest: {MANIFEST_NAME}")


def cmd_push(imm, workdir, album_name, create_album):
    manifest_path = os.path.join(workdir, MANIFEST_NAME)
    manifest = {}
    if os.path.exists(manifest_path):
        with open(manifest_path) as f:
            manifest = json.load(f)
    if album_name:
        try:
            album = find_album(imm, album_name)
        except SystemExit:
            if not create_album:
                raise
            album = imm.post_json("/albums", {"albumName": album_name})
            print(f"created album {album_name!r}")
    else:
        album = None

    new_ids = []
    for fname in sorted(os.listdir(workdir)):
        if fname.startswith(".") or fname == MANIFEST_NAME:
            continue
        if os.path.splitext(fname)[1].lower() not in IMAGE_EXTS:
            continue
        path = os.path.join(workdir, fname)
        entry = manifest.get(fname)
        if entry and entry.get("sha1") == sha1_of(path):
            continue  # unchanged since pull
        sidecar = path + ".xmp"
        print(f"  uploading {fname}"
              + (" (+ sidecar)" if os.path.exists(sidecar) else ""))
        res = imm.upload(path, sidecar if os.path.exists(sidecar) else None)
        asset_id = res.get("id")
        status = res.get("status", "?")
        print(f"    -> {asset_id} ({status})")
        if asset_id:
            new_ids.append(asset_id)
            manifest[fname] = {"asset_id": asset_id, "sha1": sha1_of(path)}
    if album and new_ids:
        imm.put_json(f"/albums/{album['id']}/assets", {"ids": new_ids})
        print(f"added {len(new_ids)} assets to {album['albumName']!r}")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"done. {len(new_ids)} uploaded.")


def main():
    ap = argparse.ArgumentParser(description="immich-digikam bridge prototype")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pull", help="download an Immich album for digiKam")
    p.add_argument("album")
    p.add_argument("--dest", default=None)
    p = sub.add_parser("push", help="upload finished files back to Immich")
    p.add_argument("--dir", default=".")
    p.add_argument("--album", default=None)
    p.add_argument("--create-album", action="store_true")
    args = ap.parse_args()

    server_url, api_key = load_config()
    imm = Immich(server_url, api_key)
    if args.cmd == "pull":
        dest = args.dest or os.path.join(
            os.path.expanduser("~/Pictures"),
            "immich-" + args.album.replace(" ", "-"))
        cmd_pull(imm, args.album, dest)
    else:
        cmd_push(imm, args.dir, args.album, args.create_album)


if __name__ == "__main__":
    main()
