#!/usr/bin/env python3
"""Builds the static site that GitHub Pages publishes (see .github/workflows/pages.yml).

    python3 carrier/docs/build_site.py [out_dir]     (default: _site)

    out_dir/index.html                     the landing page (carrier/docs/index.html)
    out_dir/soldering-guide/index.html     the soldering checklist, wrapped in a full HTML document
    out_dir/soldering-guide/rev2_solder_order.png
"""
import os, shutil, sys

DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DIR)
from serve import HEAD, PAGE   # the same document wrapper the local server uses

out = sys.argv[1] if len(sys.argv) > 1 else "_site"
guide = os.path.join(out, "soldering-guide")
os.makedirs(guide, exist_ok=True)
shutil.copy(os.path.join(DIR, "index.html"), os.path.join(out, "index.html"))
with open(os.path.join(DIR, PAGE), encoding="utf-8") as f:
    page = f.read()
with open(os.path.join(guide, "index.html"), "w", encoding="utf-8") as f:
    f.write(HEAD + page + "</body></html>\n")
shutil.copy(os.path.join(DIR, "rev2_solder_order.png"), guide)
print("site written to", out)
