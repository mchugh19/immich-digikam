#!/usr/bin/env python3
"""Sidecar ingestion test for the immich-digikam bridge (2026-10-18).

The one Phase 1 question left open when the prototype was parked:
when the bridge pushes a file with an XMP sidecar (the push sends it
as the multipart sidecarData field), does Immich actually ingest
the sidecar's tags, rating and description onto the asset? The
October attempt was unobservable because the server's extraction
queue was buried under the ML backlog.

This driver reuses the bridge module's own upload and sidecar
writer, but authenticates through the custom.immich connector
(surrogate credential; no key on disk) instead of the config file.

It uploads one generated JPEG plus a sidecar carrying a unique
marker tag, rating 4 and a marker description into the
digikam-bridge-test album, then polls the asset until the metadata
shows up or the timeout passes.
"""

import json
import os
import sys
import time
import urllib.request
import urllib.error

sys.path.insert(0, "/opt/muse/skills/custom-connector-setup/bin")
from dynamic_credentials import add_surrogate_to_request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import immich_digikam as bridge

# Point the test drivers at your own server: export
# IMMICH_TEST_HOST=<host> (no scheme). The placeholder default
# keeps published copies free of any real hostname.
HOST = os.environ.get("IMMICH_TEST_HOST", "photos.example.com")
SERVER = f"https://{HOST}"
ALBUM_NAME = "digikam-bridge-test"
MARKER_TAG = "bridge-sidecar-test-20261018"
MARKER_DESC = "sidecar ingestion test 2026-10-18"
RATING = 4
TIMEOUT_S = 480


class SurrogateImmich(bridge.Immich):
    """Bridge client whose requests carry the connector surrogate
    instead of an x-api-key header."""

    def _req(self, method, path, data=None, headers=None, timeout=120):
        req = urllib.request.Request(self.base + path, data=data,
                                     method=method)
        add_surrogate_to_request(req, "custom.immich",
                                 allowed_hosts=[HOST])
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


def main():
    workdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "test-run")
    os.makedirs(workdir, exist_ok=True)
    img = os.path.join(workdir, "sidecar-test-20261018.jpg")
    if not os.path.exists(img):
        from PIL import Image
        Image.new("RGB", (640, 480), (32, 96, 160)).save(
            img, "JPEG", quality=90)
        print(f"generated {img}")
    sidecar = img + ".xmp"
    bridge.write_xmp_sidecar(sidecar, tags=[MARKER_TAG], rating=RATING,
                             description=MARKER_DESC)
    print(f"sidecar: tag={MARKER_TAG!r} rating={RATING} "
          f"description={MARKER_DESC!r}")

    imm = SurrogateImmich(SERVER, "surrogate")
    album = bridge.find_album(imm, ALBUM_NAME)
    print(f"album {ALBUM_NAME!r}: {album['id']}")

    res = imm.upload(img, sidecar)
    asset_id = res.get("id")
    print(f"upload -> {asset_id} ({res.get('status')})")
    if not asset_id:
        sys.exit("upload returned no asset id")
    imm.put_json(f"/albums/{album['id']}/assets", {"ids": [asset_id]})
    print("added to album; polling for extraction...")

    deadline = time.time() + TIMEOUT_S
    attempt = 0
    while time.time() < deadline:
        attempt += 1
        a = imm.get(f"/assets/{asset_id}")
        tags = [t.get("value") or t.get("name", "")
                for t in a.get("tags", [])]
        # the rating lives in exifInfo; the 3.3.3 asset record has
        # no top-level "rating" field (reading one returns None
        # forever, which is how this test's verdict was once wrong)
        rating = (a.get("exifInfo") or {}).get("rating")
        desc = (a.get("exifInfo") or {}).get("description")
        print(f"  [{attempt}] tags={tags} rating={rating} "
              f"description={desc!r}")
        if (any(MARKER_TAG in t for t in tags) and rating == RATING
                and desc == MARKER_DESC):
            print("VERDICT: sidecar metadata fully ingested "
                  "(tag, rating and description all landed).")
            return
        time.sleep(15)
    print("VERDICT: TIMEOUT - metadata not fully ingested within "
          f"{TIMEOUT_S}s; last state above is the evidence.")
    sys.exit(2)


if __name__ == "__main__":
    main()
