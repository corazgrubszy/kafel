#!/usr/bin/env python3
"""Offline, pinned Meteocons comparison. Uses the real NanoSVG/C++ card renderer."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
import zlib

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'vendor/meteocons-static'
OUTPUT=ROOT/'libs/ui/examples/weather-card/meteocons'
NS='http://www.w3.org/2000/svg'
ET.register_namespace('',NS)
CASES={'sun':'clear-day','clouds':'cloudy','rain':'rain','snow':'snow'}
PALETTE={'#E6EFFC':'#c4a7e7','#F3F7FE':'#e0def4','#0A5AD4':'#9ccfd8','#86C3DB':'#e0def4',
         '#FBBF24':'#f6c177','#F8AF18':'#f6c177'}

def local(tag):return tag.rsplit('}',1)[-1]

def adapt(source,scene="clouds"):
    root=ET.fromstring(source)
    if root.get('viewBox')!='0 0 128 128':raise ValueError('Unexpected source viewport')
    clips={}
    for element in root.iter():
        tag=local(element.tag)
        if tag in ('mask','filter','use','animate','animateTransform','style'):
            raise ValueError('Unsupported SVG feature must not be silently removed: '+tag)
        if tag=='clipPath':
            children=list(element)
            if len(children)!=1 or local(children[0].tag)!='rect':raise ValueError('Nontrivial clip')
            rect=children[0]
            if rect.get('x','0')!='0' or rect.get('y','0')!='0' or rect.get('width')!='128' or rect.get('height')!='128' or any(k in rect.attrib for k in ('transform','rx','ry')):
                raise ValueError('Clip is not the complete source viewport')
            clips['url(#'+element.attrib['id']+')']=element
    for element in root.iter():
        if 'clip-path' in element.attrib:
            if element.attrib['clip-path'] not in clips:raise ValueError('Unknown or nontrivial clip reference')
            del element.attrib['clip-path']
        for attribute in ('fill','stroke','stop-color'):
            if attribute not in element.attrib:continue
            value=element.attrib[attribute]
            if value=='none' or value.startswith('url(#'):continue
            if local(element.tag)=='rect' and value=='white':continue # removed viewport clip
            mapped=PALETTE.get(value.upper())
            if mapped is None:raise ValueError('Unmapped source color: '+value)
            if value.upper()=='#F8AF18' and local(element.tag)=='stop':mapped='#ebbcba'
            element.attrib[attribute]=mapped
    for parent in root.iter():
        for child in list(parent):
            if local(child.tag)=='clipPath':parent.remove(child)
    result=ET.Element('{'+NS+'}svg',{'width':'240','height':'240','viewBox':'0 0 240 240','fill':'none'})
    result.append(ET.Comment(' Adapted from Meteocons by Bas Milius, MIT. Rose Pine palette; viewport clips only removed; common layout transform. '))
    placement='translate(127 28) scale(0.95)' if scene=='sun' else 'translate(106 3) scale(1.25)'
    group=ET.SubElement(result,'{'+NS+'}g',{'transform':placement})
    for child in list(root):
        if local(child.tag)=='defs':result.append(copy.deepcopy(child))
        else:group.append(copy.deepcopy(child))
    return ET.tostring(result,encoding='utf-8',xml_declaration=True)+b'\n'

def ppm(path):
    magic,dimensions,maximum,data=path.read_bytes().split(b'\n',3)
    if (magic,dimensions,maximum)!=(b'P6',b'240 240',b'255') or len(data)!=240*240*3:raise ValueError('Unexpected renderer output')
    return data

def png(path,width,height,rgb):
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))

def main():
    subprocess.run(['zig','build','weather-card-bin','-Doptimize=ReleaseFast'],cwd=ROOT,check=True)
    binary=ROOT/'zig-out/bin/weather-card-example'
    font=ROOT/'libs/ui/assets/ibm-plex-sans'
    template=ROOT/'libs/ui/examples/weather-card/weather.json'
    provenance=json.loads((SOURCE/'VERSION.json').read_text())
    report={'package':'@meteocons/svg-static','version':provenance['version'],
        'notes':['Originals are preserved in vendor/meteocons-static with MIT license.',
                 'Only full 128x128 viewport clips are removed; masks, filters, use and animation are rejected.',
                 'The cloud example is cloudy. Flat overcast uses a nontrivial mask and was not substituted silently.',
                 'Adapted source geometry is unchanged; colors and one common layout transform differ.'], 'cases':{}}
    contact=bytearray(480*960*3)
    for column,style in enumerate(('fill','flat')):
        folder=OUTPUT/style;folder.mkdir(parents=True,exist_ok=True)
        for row,(scene,name) in enumerate(CASES.items()):
            original=SOURCE/style/(name+'.svg')
            if hashlib.sha256(original.read_bytes()).hexdigest()!=provenance['files_sha256'][style+'/'+name+'.svg']:
                raise RuntimeError('Original source differs from pinned package')
            adapted=folder/(scene+'.svg');adapted.write_bytes(adapt(original.read_bytes(),scene))
            def render(svg,out):return subprocess.run([str(binary),str(font/'IBMPlexSans-Regular.ttf'),str(font/'IBMPlexSans-Bold.ttf'),str(template),str(svg),str(out),scene],text=True,capture_output=True)
            raw=render(original,folder/(scene+'-raw.ppm'))
            out=folder/(scene+'.ppm');ready=render(adapted,out)
            if ready.returncode:raise RuntimeError(ready.stderr)
            pixels=ppm(out);png(out.with_suffix('.png'),240,240,pixels)
            for y in range(240):
                start=((row*240+y)*480+column*240)*3
                contact[start:start+240*3]=pixels[y*240*3:(y+1)*240*3]
            report['cases'][style+'/'+scene]={'original_supported':raw.returncode==0,'original_error':raw.stderr.strip() if raw.returncode else '',
                'adapted_supported':True,'source_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),
                'adapted_sha256':hashlib.sha256(adapted.read_bytes()).hexdigest(),'rgb_sha256':hashlib.sha256(pixels).hexdigest()}
            print(style,scene,'raw='+str(raw.returncode==0),'adapted=OK',flush=True)
    png(OUTPUT/'comparison.png',480,960,contact)
    (OUTPUT/'compatibility.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
