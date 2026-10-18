#!/usr/bin/env python3
"""Contract experiments 2-4 for the immich-digikam bridge (2026-10-18).

Experiment 1 (sidecar element-form rating + tag + description) already
ran as sidecar_ingest_test.py: tag and description landed, rating did
not. These three close the remaining contract questions:

  E2 gps        sidecar with exif:GPSLatitude/GPSLongitude + marker tag
  E3 ratingattr sidecar with xmp:Rating as an rdf:Description ATTRIBUTE
  E4 embedded   rating/tag/description in the JPEG's own XMP packet,
                uploaded with NO sidecar

Each asset goes to the digikam-bridge-test album; verdicts are read off
the asset records once their pipelines have run.
"""

import json
import os
import sys
import time
import urllib.request
import urllib.error
from xml.sax.saxutils import escape

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
RATING = 4
# Bray/Wicklow coast: 53.1054, -6.0506 -> XMP "deg,min.decH" form
GPS_LAT_XMP = "53,6.3240N"
GPS_LON_XMP = "6,3.0360W"
GPS_LAT = 53.1054
GPS_LON = -6.0506
TIMEOUT_S = 600


class SurrogateImmich(bridge.Immich):
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


def sidecar_xml(marker_tag, rating=None, rating_as_attr=False,
                gps=False, description=""):
    attrs = ""
    elems = ""
    if rating is not None:
        if rating_as_attr:
            attrs = f' xmp:Rating="{int(rating)}"'
        else:
            elems += f"\n   <xmp:Rating>{int(rating)}</xmp:Rating>"
    if gps:
        elems += (f"\n   <exif:GPSLatitude>{GPS_LAT_XMP}</exif:GPSLatitude>"
                  f"\n   <exif:GPSLongitude>{GPS_LON_XMP}</exif:GPSLongitude>")
    if description:
        elems += (f"\n   <dc:description><rdf:Alt>"
                  f"<rdf:li xml:lang=\"x-default\">{escape(description)}"
                  f"</rdf:li></rdf:Alt></dc:description>")
    elems += (f"\n   <digiKam:TagsList><rdf:Bag>"
              f"\n      <rdf:li>{escape(marker_tag)}</rdf:li>"
              f"\n   </rdf:Bag></digiKam:TagsList>")
    return f"""<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:dc="http://purl.org/dc/elements/1.1/"
    xmlns:xmp="http://ns.adobe.com/xap/1.0/"
    xmlns:exif="http://ns.adobe.com/exif/1.0/"
    xmlns:digiKam="http://www.digikam.org/ns/1.0/"{attrs}>{elems}
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
"""


def make_jpeg(path, color, embedded_xmp=None):
    from PIL import Image
    Image.new("RGB", (640, 480), color).save(path, "JPEG", quality=90)
    if embedded_xmp:
        packet = embedded_xmp.encode("utf-8")
        header = b"http://ns.adobe.com/xap/1.0/\x00"
        seg_len = 2 + len(header) + len(packet)
        app1 = b"\xff\xe1" + seg_len.to_bytes(2, "big") + header + packet
        with open(path, "rb") as f:
            data = f.read()
        assert data[:2] == b"\xff\xd8"
        with open(path, "wb") as f:
            f.write(data[:2] + app1 + data[2:])


def main():
    workdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "test-run")
    os.makedirs(workdir, exist_ok=True)
    imm = SurrogateImmich(SERVER, "surrogate")
    album = bridge.find_album(imm, ALBUM_NAME)
    print(f"album: {album['id']}")

    experiments = []

    # E2: GPS via sidecar
    img = os.path.join(workdir, "contract-gps.jpg")
    make_jpeg(img, (40, 140, 60))
    with open(img + ".xmp", "w", encoding="utf-8") as f:
        f.write(sidecar_xml("bridge-gps-test-20261018", gps=True))
    experiments.append(("gps", img, img + ".xmp"))

    # E3: rating as attribute in sidecar
    img = os.path.join(workdir, "contract-ratingattr.jpg")
    make_jpeg(img, (160, 90, 30))
    with open(img + ".xmp", "w", encoding="utf-8") as f:
        f.write(sidecar_xml("bridge-ratingattr-test-20261018",
                            rating=RATING, rating_as_attr=True))
    experiments.append(("ratingattr", img, img + ".xmp"))

    # E4: embedded XMP, no sidecar
    img = os.path.join(workdir, "contract-embedded.jpg")
    make_jpeg(img, (70, 60, 150),
              embedded_xmp=sidecar_xml(
                  "bridge-embedded-test-20261018", rating=RATING,
                  description="embedded xmp test 2026-10-18"))
    experiments.append(("embedded", img, None))

    state = {}
    for name, img, sidecar in experiments:
        res = imm.upload(img, sidecar)
        aid = res.get("id")
        print(f"{name}: upload -> {aid} ({res.get('status')})")
        imm.put_json(f"/albums/{album['id']}/assets", {"ids": [aid]})
        state[name] = {"id": aid, "done": False, "moved": False,
                       "stable": 0, "last_updated": None}

    deadline = time.time() + TIMEOUT_S
    while time.time() < deadline and not all(
            s["done"] for s in state.values()):
        time.sleep(20)
        for name, s in state.items():
            if s["done"]:
                continue
            a = imm.get(f"/assets/{s['id']}")
            if a["updatedAt"] != a["createdAt"]:
                s["moved"] = True
            if s["moved"] and a["updatedAt"] == s["last_updated"]:
                s["stable"] += 1
            else:
                s["stable"] = 0
            s["last_updated"] = a["updatedAt"]
            s["asset"] = a
            if a.get("thumbhash") and s["stable"] >= 1:
                s["done"] = True
        print("  poll:", {n: ("done" if s["done"] else
                              "moved" if s["moved"] else "waiting")
                          for n, s in state.items()})

    print()
    for name, s in state.items():
        a = s.get("asset") or imm.get(f"/assets/{s['id']}")
        tags = [t.get("value") or t.get("name", "")
                for t in a.get("tags", [])]
        exif = a.get("exifInfo") or {}
        print(f"== {name} (asset {s['id']}, pipeline done: {s['done']})")
        print(f"   tags={tags}")
        print(f"   rating={exif.get('rating')}")
        print(f"   description={exif.get('description')!r}")
        print(f"   lat={exif.get('latitude')} lon={exif.get('longitude')}")
        if name == "gps":
            ok = (exif.get("latitude") is not None
                  and abs(exif["latitude"] - GPS_LAT) < 0.001
                  and abs(exif["longitude"] - GPS_LON) < 0.001)
            print(f"   VERDICT gps: {'LANDED' if ok else 'NOT LANDED'}")
        if name == "ratingattr":
            print(f"   VERDICT rating-as-attribute: "
                  f"{'LANDED' if exif.get('rating') == RATING else 'NOT LANDED'}")
        if name == "embedded":
            print(f"   VERDICT embedded rating: "
                  f"{'LANDED' if exif.get('rating') == RATING else 'NOT LANDED'}")


if __name__ == "__main__":
    main()
