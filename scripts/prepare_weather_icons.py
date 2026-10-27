#!/usr/bin/env python3
"""Offline Flat SVG preparation. No extra dependency on the device or normal build.
Regenerate with requirements-weather-icons.txt; originals stay pinned in vendor/.
Only solid alpha masks are accepted, intersected as vectors, never discarded.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'vendor/meteocons-static'
OUTPUT=ROOT/'apps/weather/assets/icons'
NS='http://www.w3.org/2000/svg'
ET.register_namespace('',NS)
PALETTE={'#F8AF18':'#f6c177','#F6A823':'#f6c177','#E6EFFC':'#c4a7e7',
 '#F3F7FE':'#e0def4','#E2E8F0':'#e0def4','#0A5AD4':'#9ccfd8','#86C3DB':'#e0def4',
 '#72B9D5':'#9ccfd8','#202939':'#6e6a86','#7C8CA2':'#908caa','#B0BCCD':'#c4a7e7'}
def local(e):return e.tag.rsplit('}',1)[-1]
def translation(value):
    if not value:return (0.,0.)
    m=re.fullmatch(r'translate\(([-\d.]+)[ ,]+([-\d.]+)\)',value)
    if not m:raise ValueError('Unsupported transform: '+value)
    return tuple(map(float,m.groups()))
def geometry(e):
    import pathops
    from fontTools.svgLib.path import parse_path
    p=pathops.Path();tag=local(e)
    if tag=='path':data=e.attrib['d']
    elif tag=='rect':
        if any(k in e.attrib for k in ('rx','ry')):raise ValueError('Rounded mask rectangle')
        x,y,w,h=(float(e.get(k,'0')) for k in ('x','y','width','height'))
        data=f'M{x} {y}h{w}v{h}h{-w}Z'
    elif tag=='circle':
        x,y,r=(float(e.attrib[k]) for k in ('cx','cy','r'))
        data=f'M{x-r} {y}a{r} {r} 0 1 0 {2*r} 0a{r} {r} 0 1 0 {-2*r} 0Z'
    else:raise ValueError('Unsupported masked geometry: '+tag)
    parse_path(data,p.getPen())
    if e.get('fill-rule')=='evenodd':p.fillType=pathops.FillType.EVEN_ODD
    return p

def flatten(source):
    import pathops
    from fontTools.pens.svgPathPen import SVGPathPen
    root=ET.fromstring(source)
    if root.get('viewBox')!='0 0 128 128':raise ValueError('Unexpected viewport')
    ids={e.get('id'):e for e in root.iter() if e.get('id')}
    def ref(value,kind):
        m=re.fullmatch(r'url\(#([^()]+)\)',value)
        if not m or m[1] not in ids or local(ids[m[1]])!=kind:raise ValueError('Invalid '+kind+' reference')
        return ids[m[1]]
    # These package masks are alpha silhouettes, not luminance masks. Black is opaque.
    def mask_path(mask):
        if mask.get('style')!='mask-type:alpha' or mask.get('maskUnits')!='userSpaceOnUse':raise ValueError('Unsupported mask semantics')
        result=pathops.Path()
        for e in mask.iter():
            if e is mask:continue
            if local(e)=='g':
                if set(e.attrib)-{'id'}:raise ValueError('Unsupported mask group attributes')
                continue
            if any(k in e.attrib for k in ('opacity','fill-opacity','stroke','transform','mask','filter')) or e.get('fill') not in ('black','white'):
                raise ValueError('Mask is not an opaque silhouette')
            result=pathops.op(result,geometry(e),pathops.PathOp.UNION)
        bounds=ET.Element('rect',{'x':mask.get('x','0'),'y':mask.get('y','0'),'width':mask.attrib['width'],'height':mask.attrib['height']})
        return pathops.op(result,geometry(bounds),pathops.PathOp.INTERSECTION)
    result=ET.Element('{'+NS+'}svg',{'width':'128','height':'128','viewBox':'0 0 128 128','fill':'none'})
    def walk(e,offset=(0.,0.),masks=(),fill='none'):
        tag=local(e)
        if tag in ('defs','mask','clipPath'):return
        if tag not in ('svg','g','path','circle','rect'):raise ValueError('Unsupported element '+tag)
        if any(k in e.attrib for k in ('filter','opacity','fill-opacity','style','href','xlink:href')):raise ValueError('Unsupported artwork semantics')
        dx,dy=translation(e.get('transform'));offset=(offset[0]+dx,offset[1]+dy)
        fill=e.get('fill',fill)
        if e.get('clip-path'):
            clip=ref(e.get('clip-path'),'clipPath');children=list(clip)
            if len(children)!=1 or local(children[0])!='rect' or children[0].get('width')!='128' or children[0].get('height')!='128' or any(children[0].get(k,'0')!='0' for k in ('x','y')) or set(children[0].attrib)-{'id','x','y','width','height','fill'} or set(clip.attrib)-{'id'} or offset!=(0.,0.):
                raise ValueError('Non-viewport clip requires explicit conversion')
        if e.get('mask'):
            mask=mask_path(ref(e.get('mask'),'mask')).transform(1,0,0,1,*offset);masks=(*masks,mask)
        if tag in ('svg','g'):
            for c in e:walk(c,offset,masks,fill)
            return
        out=copy.deepcopy(e)
        for k in ('id','transform','mask','clip-path'):out.attrib.pop(k,None)
        if masks:
            if e.get('stroke','none')!='none' or fill=='none':raise ValueError('Masked stroke must be explicitly outlined')
            path=geometry(e).transform(1,0,0,1,*offset)
            for mask in masks:path=pathops.op(path,mask,pathops.PathOp.INTERSECTION)
            if not len(path):return
            pen=SVGPathPen(None);path.draw(pen)
            out=ET.Element('{'+NS+'}path',{'d':pen.getCommands(),'fill':fill,'fill-rule':'nonzero'})
        elif offset!=(0.,0.):out.set('transform',f'translate({offset[0]:g} {offset[1]:g})')
        if not out.get('fill'):out.set('fill',fill)
        for attr in ('fill','stroke'):
            value=out.get(attr)
            if value and value!='none':
                if value.upper() not in PALETTE:raise ValueError('Unmapped color '+value)
                out.set(attr,PALETTE[value.upper()])
        result.append(out)
    walk(root)
    # Trim the invisible source viewport, not the path geometry. A 64px image
    # slot then produces a 64px visible silhouette, consistently with other art.
    extents=[]
    for child in result:
        path=geometry(child)
        x0,y0,x1,y1=path.bounds
        dx,dy=translation(child.get('transform'))
        stroke=float(child.get('stroke-width','0'))/2 if child.get('stroke','none')!='none' else 0
        extents.append((x0+dx-stroke,y0+dy-stroke,x1+dx+stroke,y1+dy+stroke))
    if not extents:raise ValueError('Empty weather silhouette')
    x0=min(b[0] for b in extents);y0=min(b[1] for b in extents)
    width=max(b[2] for b in extents)-x0;height=max(b[3] for b in extents)-y0
    result.set('width',f'{width:g}');result.set('height',f'{height:g}')
    result.set('viewBox',f'{x0:g} {y0:g} {width:g} {height:g}')
    result.insert(0,ET.Comment(' Meteocons Flat / Bas Milius / MIT. Solid alpha masks intersected offline; Rose Pine palette. '))
    return ET.tostring(result,encoding='utf-8',xml_declaration=True)+b'\n'

def parse_catalog(output):
    return [dict(zip(('code','day','night','description'),line.split('\t'))) for line in output.splitlines()]

def catalog():
    binary=ROOT/'zig-out/bin/kafel-weather-catalog'
    return parse_catalog(subprocess.check_output([str(binary)],text=True))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-catalog',type=Path,help='Compare a weather-owned TSV build output with the committed catalog')
    args=parser.parse_args()
    if args.check_catalog:
        expected=json.loads((OUTPUT/'catalog.json').read_text())['wmo']
        if parse_catalog(args.check_catalog.read_text())!=expected:
            raise SystemExit('Stale WMO catalog: regenerate weather icons from kafel-weather-catalog')
        return
    OUTPUT.mkdir(parents=True,exist_ok=True)
    entries=catalog();names=sorted({e[k] for e in entries for k in ('day','night')}|{'not-available'})
    provenance=json.loads((SOURCE/'VERSION.json').read_text());report={}
    for name in names:
        relative='flat/'+name+'.svg';source=(SOURCE/relative).read_bytes()
        if hashlib.sha256(source).hexdigest()!=provenance['files_sha256'][relative]:raise ValueError('Original hash mismatch '+name)
        adapted=flatten(source);(OUTPUT/(name+'.svg')).write_bytes(adapted)
        report[name]={'source_sha256':hashlib.sha256(source).hexdigest(),'prepared_sha256':hashlib.sha256(adapted).hexdigest()}
    (OUTPUT/'catalog.json').write_text(json.dumps({'wmo':entries,'assets':report},ensure_ascii=False,indent=2)+'\n')
    print(f'Prepared {len(names)} Flat SVG resources for {len(entries)} WMO codes, both day/night.')
if __name__=='__main__':main()
