"""Pinned generated vectors and exact mask conversion (offline optional tools)."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import prepare_weather_icons as icons
TOOLS=importlib.util.find_spec('pathops') is not None and importlib.util.find_spec('fontTools') is not None
class PreparedIconsTests(unittest.TestCase):
    def test_pinned_sources_and_prepared_hashes(self):
        provenance=json.loads((icons.SOURCE/'VERSION.json').read_text())
        catalog=json.loads((icons.OUTPUT/'catalog.json').read_text())
        self.assertEqual(len(catalog['wmo']),29)
        for name,entry in catalog['assets'].items():
            source=(icons.SOURCE/'flat'/(name+'.svg')).read_bytes()
            self.assertEqual(hashlib.sha256(source).hexdigest(),provenance['files_sha256']['flat/'+name+'.svg'])
            self.assertEqual(hashlib.sha256(source).hexdigest(),entry['source_sha256'])
            data=(icons.OUTPUT/(name+'.svg')).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(),entry['prepared_sha256'])
            root=ET.fromstring(data)
            for e in root.iter():
                self.assertIn(icons.local(e),('svg','path','rect','circle'))
                self.assertFalse(set(e.attrib)&{'mask','clip-path','filter','style','href'})
                for attr in ('fill','stroke'):
                    value=e.get(attr)
                    if value and value!='none':self.assertIn(value,set(icons.PALETTE.values()))
        names=set(catalog['assets'])
        for entry in catalog['wmo']:
            self.assertIn(entry['day'],names);self.assertIn(entry['night'],names)
    @unittest.skipUnless(TOOLS,'offline asset-generation tools only')
    def test_mask_hole_is_intersected_not_discarded(self):
        svg=b'''<svg viewBox="0 0 128 128" fill="none"><mask id="m" style="mask-type:alpha" maskUnits="userSpaceOnUse" x="0" y="0" width="128" height="128"><path fill="black" fill-rule="evenodd" d="M0 0H128V128H0Z M32 32H96V96H32Z"/></mask><rect width="128" height="128" fill="#F8AF18" mask="url(#m)"/></svg>'''
        root=ET.fromstring(icons.flatten(svg));p=icons.geometry(next(e for e in root if icons.local(e)=='path'))
        self.assertTrue(p.contains((10,10)));self.assertFalse(p.contains((64,64)))
        for bad in [svg.replace(b'fill="black"',b'fill="black" opacity="0.5"'),svg.replace(b'mask-type:alpha',b'mask-type:luminance')]:
            with self.assertRaises(ValueError):icons.flatten(bad)
    @unittest.skipUnless(TOOLS,'offline asset-generation tools only')
    def test_regeneration_is_byte_identical(self):
        catalog=json.loads((icons.OUTPUT/'catalog.json').read_text())
        for name in catalog['assets']:
            with self.subTest(name=name):
                self.assertEqual(icons.flatten((icons.SOURCE/'flat'/(name+'.svg')).read_bytes()),(icons.OUTPUT/(name+'.svg')).read_bytes())
