#!/usr/bin/env python3
"""Lays out the static site that GitHub Pages publishes (see .github/workflows/pages.yml).

    python3 carrier/docs/build_site.py [out_dir]     (default: _site)

    out_dir/index.html                          <- index.html (landing page)
    out_dir/soldering-guide/index.html          <- rev2_soldering_guide.html
    out_dir/soldering-guide/rev2_solder_order.png
"""
import os, shutil, sys

DIR = os.path.dirname(os.path.abspath(__file__))
ROUTES = {
    "index.html": "index.html",
    "soldering-guide/index.html": "rev2_soldering_guide.html",
    "soldering-guide/rev2_solder_order.png": "rev2_solder_order.png",
}

out = sys.argv[1] if len(sys.argv) > 1 else "_site"
for dest, src in ROUTES.items():
    os.makedirs(os.path.dirname(os.path.join(out, dest)) or out, exist_ok=True)
    shutil.copy(os.path.join(DIR, src), os.path.join(out, dest))
print("site written to", out)
