#!/usr/bin/env python3
"""Render the actual C++ WMO catalog and prepared SVGs; Pillow only lays out labels."""
import hashlib
import json
from pathlib import Path
import subprocess
import textwrap
import xml.etree.ElementTree as ET
from PIL import Image,ImageDraw,ImageFont
from prepare_weather_icons import ROOT,OUTPUT as ICONS,catalog,NS
OUTPUT=ROOT/'libs/ui/examples/weather-card/meteocons/catalog'
ET.register_namespace('',NS)

def prepare_example_cards():
    names=json.loads((ICONS/'catalog.json').read_text())['assets']
    OUTPUT.mkdir(parents=True,exist_ok=True)
    for name in names:
        card=ET.Element('{'+NS+'}svg',{'width':'240','height':'240','viewBox':'0 0 240 240','fill':'none'})
        group=ET.SubElement(card,'{'+NS+'}g',{'transform':'translate(106 3) scale(1.25)'})
        for child in ET.fromstring((ICONS/(name+'.svg')).read_bytes()):group.append(child)
        (OUTPUT/(name+'-card.svg')).write_bytes(ET.tostring(card,encoding='utf-8',xml_declaration=True))

def main():
    prepare_example_cards()
    binary=ROOT/'zig-out/bin/weather-card-example';entries=catalog()
    fontroot=ROOT/'libs/ui/assets/ibm-plex-sans'
    font=ImageFont.truetype(str(fontroot/'IBMPlexSans-Regular.ttf'),15)
    small=ImageFont.truetype(str(fontroot/'IBMPlexSans-Regular.ttf'),12)
    bold=ImageFont.truetype(str(fontroot/'IBMPlexSans-Bold.ttf'),16)
    report=[]
    for page in range(2):
        image=Image.new('RGB',(840,970),'#232136');draw=ImageDraw.Draw(image)
        draw.text((12,8),'METEOCONS FLAT  /  ROSÉ PINE MOON',font=bold,fill='#e0def4')
        for i,entry in enumerate(entries[page*15:(page+1)*15]):
            x=(i%3)*280;y=36+(i//3)*186
            draw.text((x+10,y),f"WMO {entry['code']}",font=bold,fill='#f6c177')
            desc=textwrap.wrap(entry['description'],width=31)
            for j,line in enumerate(desc):draw.text((x+10,y+21+j*17),line,font=font,fill='#e0def4')
            row={'code':int(entry['code']),'description':entry['description']}
            for column,key in enumerate(('day','night')):
                name=entry[key];out=OUTPUT/(name+'.ppm')
                subprocess.run([str(binary),'--icon',str(ICONS/(name+'.svg')),str(out)],check=True)
                with Image.open(out) as icon:image.paste(icon,(x+4+column*140,y+53))
                draw.text((x+12+column*140,y+166),'DZIEŃ' if key=='day' else 'NOC',font=small,fill='#908caa')
                row[key]={'resource':name,'rgb_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
            report.append(row)
        image.save(OUTPUT/f'overview-{page+1}.png')
    # Four real 240x240 frames, same composition as before, day/night data supplied explicitly.
    for name,code,day in [('clear-day',0,1),('clear-night',0,0),('rain-day',63,1),('storm-night',95,0)]:
        entry=next(e for e in entries if int(e['code'])==code);resource=entry['day' if day else 'night']
        out=OUTPUT/(name+'-preview.ppm')
        subprocess.run([str(binary),str(fontroot/'IBMPlexSans-Regular.ttf'),str(fontroot/'IBMPlexSans-Bold.ttf'),str(ROOT/'libs/ui/examples/weather-card/weather.json'),str(OUTPUT/(resource+'-card.svg')),str(out),str(code),str(day)],check=True)
        with Image.open(out) as card:card.save(out.with_suffix('.png'))
    (OUTPUT/'rendered.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print('Rendered all 58 WMO/day-night selections and four actual 240x240 card examples.')
if __name__=='__main__':main()
