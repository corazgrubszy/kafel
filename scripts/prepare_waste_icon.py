#!/usr/bin/env python3
"""Offline vector split of the pinned Heroicons silhouette; never runs on CM4.
Uses the same optional fontTools/pathops tools as Meteocons preparation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent.parent
OUTPUT=ROOT/'apps/waste/assets'
BODY='#5B5B5B'
LID='#E6B800'
SPLIT_Y=6.75
NS='http://www.w3.org/2000/svg'
ET.register_namespace('',NS)


def original_path():
    import pathops
    from fontTools.svgLib.path import parse_path
    vendor=ROOT/'vendor/heroicons'
    source=(vendor/'trash-solid.svg').read_bytes()
    expected=json.loads((vendor/'VERSION.json').read_text())['files_sha256']['trash-solid.svg']
    if hashlib.sha256(source).hexdigest()!=expected:raise ValueError('Heroicons source hash mismatch')
    path=pathops.Path()
    path.fillType=pathops.FillType.EVEN_ODD
    element=ET.fromstring(source).find('{'+NS+'}path')
    parse_path(element.attrib['d'],path.getPen())
    return path,expected


def split_paths():
    import pathops
    from fontTools.svgLib.path import parse_path
    path,_=original_path()
    def part(y0,y1):
        box=pathops.Path()
        parse_path(f'M0 {y0}H24V{y1}H0Z',box.getPen())
        return pathops.op(path,box,pathops.PathOp.INTERSECTION)
    return part(SPLIT_Y,24),part(0,SPLIT_Y)


def prepared():
    from fontTools.pens.svgPathPen import SVGPathPen
    root=ET.Element('{'+NS+'}svg',{'width':'24','height':'24','viewBox':'0 0 24 24'})
    for path,color in zip(split_paths(),(BODY,LID)):
        pen=SVGPathPen(None);path.draw(pen)
        ET.SubElement(root,'{'+NS+'}path',{'d':pen.getCommands(),'fill':color,'fill-rule':'nonzero'})
    return ET.tostring(root,encoding='utf-8',xml_declaration=True)+b'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true')
    args=parser.parse_args();data=prepared()
    _,source_hash=original_path()
    metadata={'source_sha256':source_hash,'split_y':SPLIT_Y,'body':BODY,'lid':LID,
              'prepared_sha256':hashlib.sha256(data).hexdigest()}
    outputs={'mixed-bin.svg':data,'mixed-bin.json':(json.dumps(metadata,indent=2)+'\n').encode()}
    for name,content in outputs.items():
        path=OUTPUT/name
        if args.check:
            if path.read_bytes()!=content:raise SystemExit('Stale waste artwork: '+str(path))
        else:path.write_bytes(content)


if __name__=='__main__':main()
