#!/usr/bin/env python3
"""Reproduce embedded service assets. No network, Pillow, or device access."""
import argparse
import json
import hashlib
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
COLORS=('#5B5B5B','#008F39','#0072CE','#E6B800','#A0522D','#008F39')

def waste_svg(mask):
    source=ROOT/'vendor/heroicons'
    provenance=json.loads((source/'VERSION.json').read_text())
    original=(source/'trash-solid.svg').read_bytes()
    if hashlib.sha256(original).hexdigest()!=provenance['files_sha256']['trash-solid.svg']:
        raise ValueError('Heroicons source hash mismatch')
    path=ET.fromstring(original).find('{http://www.w3.org/2000/svg}path')
    if path is None:raise ValueError('Missing Heroicons silhouette')
    mixed=ROOT/'apps/waste/assets/mixed-bin.svg'
    metadata=json.loads(mixed.with_suffix('.json').read_text())
    mixed_bytes=mixed.read_bytes()
    if metadata['source_sha256']!=hashlib.sha256(original).hexdigest() or hashlib.sha256(mixed_bytes).hexdigest()!=metadata['prepared_sha256']:
        raise ValueError('Prepared mixed-bin artwork hash mismatch')
    mixed_paths=ET.fromstring(mixed_bytes).findall('{http://www.w3.org/2000/svg}path')
    indices=[i for i in range(6) if mask & (1<<i)]
    # Visible source bounds: x=3..21, y=1.5..22.5; preserve proportions.
    scale=64/21
    width=18*scale
    total=len(indices)*width+max(0,len(indices)-1)*10
    fit=min(1,212/total) if total else 1
    group=''
    for column,index in enumerate(indices):
        x=column*(width+10)*fit
        group+=f'<g transform="translate({x:g} 0) scale({scale*fit:g}) translate(-3 -1.5)" fill="{COLORS[index]}">'
        if index==0:
            for part in mixed_paths:
                group+=f'<path d="{part.attrib["d"]}" fill="{part.attrib["fill"]}" fill-rule="nonzero"/>'
        else:
            group+=f'<path d="{path.attrib["d"]}" fill-rule="evenodd"/>'
        group+='</g>'
    return '<svg xmlns="http://www.w3.org/2000/svg" width="212" height="64" viewBox="0 0 212 64">'+group+'</svg>'

def bundle(app):
    template=(ROOT/'apps'/app/'assets/template.json').read_text()
    if app=='weather':
        directory=ROOT/'apps/weather/assets/icons'
        names=json.loads((directory/'catalog.json').read_text())['assets']
        svg={name:(directory/(name+'.svg')).read_text() for name in names}
    else:
        svg={str(mask):waste_svg(mask) for mask in range(64)}
    return json.dumps({'template':template,'svg':svg},ensure_ascii=False,separators=(',',':'))+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',choices=('weather','waste'))
    parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for app in (args.app,) if args.app else ('weather','waste'):
        path=ROOT/'apps'/app/'assets/bundle.json';expected=bundle(app)
        if args.check:
            if path.read_text()!=expected:raise SystemExit(f'Stale {app} asset bundle: python3 scripts/prepare_service_assets.py --app {app}')
        else:path.write_text(expected)
if __name__=='__main__':main()
