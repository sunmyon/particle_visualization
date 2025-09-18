#!/usr/bin/env python3
import os, urllib.request, hashlib

FILES = {
    "cloud_SF_Zsolar.dat": {
        "url": "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/cloud_SF_Zsolar.dat",
        "sha256": "d6758175dd0983bef5e47ef5483351bd5e28cb88637e49f0edada51e0340752f"
    },
    "star_cluster_metal_poor.dat": {
        "url": "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/star_cluster_metal_poor.dat",
        "sha256": "20f0f81e69fe8f54971a813748466c84f0768df15b87cd6e7aa41c64628ca7ab"
    },
    "supermassive_stars_extremely_metal_poor.dat": {
        "url": "https://github.com/sunmyon/particle_visualization/releases/download/test_data_ver1.0/supermassive_stars_extremely_metal_poor.dat",
        "sha256": "7386b2ccb0f19c43ce560be9a314c06b5cdfbb192a2bda79f915285809aaebf2"
    },
}

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(BASE_DIR, "data")
os.makedirs(DEST, exist_ok=True)

for fname, meta in FILES.items():
    out = os.path.join(DEST, fname)
    if not os.path.exists(out):
        print("Downloading", fname)
        urllib.request.urlretrieve(meta["url"], out)
    h = hashlib.sha256(open(out,"rb").read()).hexdigest()
    if h != meta["sha256"]:
        raise RuntimeError(f"Checksum mismatch for {fname}")
    print("OK:", fname)
