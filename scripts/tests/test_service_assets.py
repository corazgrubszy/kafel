"""Ensure binary-embedded graphics match their reviewable, public source files."""
import importlib.util
import json
import shutil
import tempfile
from unittest.mock import patch
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
spec=importlib.util.spec_from_file_location('service_assets',Path(__file__).resolve().parents[1]/'prepare_service_assets.py')
assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(assets)
class AssetTests(unittest.TestCase):
    def test_checked_in_bundles_are_reproducible(self):
        for app in ('weather','waste'):
            with self.subTest(app=app):
                path=assets.ROOT/'apps'/app/'assets/bundle.json'
                self.assertEqual(path.read_text(),assets.bundle(app))
                source=json.loads(path.read_text());template=json.loads(source['template'])
                self.assertEqual(template['schema'],'kafel.tile/1');self.assertEqual(template['canvas'],[240,240])
                for svg in source['svg'].values():ET.fromstring(svg)
    def test_weather_bundle_needs_only_weather_owned_assets(self):
        expected=(assets.ROOT/'apps/weather/assets/bundle.json').read_text()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            shutil.copytree(assets.ROOT/'apps/weather/assets',root/'apps/weather/assets')
            self.assertFalse((root/'libs/ui/examples').exists())
            with patch.object(assets,'ROOT',root):
                self.assertEqual(assets.bundle('weather'),expected)

    def test_shared_typographic_roles(self):
        for app in ('weather','waste'):
            template=json.loads((assets.ROOT/'apps'/app/'assets/template.json').read_text())
            faces={f['id']:f['face'] for f in template['fonts']}
            hero=template['styles']['hero' if app=='weather' else 'caption']
            self.assertEqual(faces[hero['font']],'ibm-plex-sans/bold')
            if app=='waste':self.assertEqual(faces[template['styles']['detail']['font']],'ibm-plex-sans/regular')
    def test_fraction_overflow_is_declarative(self):
        template=json.loads((assets.ROOT/'apps/waste/assets/template.json').read_text())
        fields=[t for t in template['texts'] if isinstance(t['content'],dict) and t['content'].get('field')=='fractions']
        self.assertEqual(len(fields),1)
        self.assertEqual(fields[0]['overflow'],'marquee')
    def test_heroicons_geometry_and_saturated_colors(self):
        original=ET.fromstring((assets.ROOT/'vendor/heroicons/trash-solid.svg').read_text())
        path=original.find('{http://www.w3.org/2000/svg}path').get('d')
        for mask in (0,1,12,48,63):
            root=ET.fromstring(assets.waste_svg(mask))
            paths=root.findall('.//{http://www.w3.org/2000/svg}path')
            self.assertEqual(len(paths),bin(mask).count('1')+bool(mask&1))
            self.assertTrue(all(p.get('d')==path and p.get('fill-rule')=='evenodd' for p in paths if p.get('fill') not in ('#5B5B5B','#E6B800')))
            if mask&1:self.assertEqual({p.get('fill') for p in paths if p.get('fill')},{'#5B5B5B','#E6B800'})
            self.assertEqual(root.get('viewBox'),'0 0 212 64')
        source=assets.waste_svg(48)
        self.assertIn('#A0522D',source);self.assertIn('#008F39',source)
    def test_large_tiles_share_typographic_and_alignment_roles(self):
        for app in ('weather','waste'):
            t=json.loads((assets.ROOT/'apps'/app/'assets/template.json').read_text())
            self.assertEqual(t['styles']['title']['size'],22)
            self.assertEqual(t['styles']['hero' if app=='weather' else 'caption']['size'],60)
            self.assertEqual(t['styles']['hero' if app=='weather' else 'caption']['color'],'#c4a7e7')
            self.assertEqual(t['backdrop']['glow_gain'],[0,0,0]);self.assertEqual(t['backdrop']['stripe_width'],0)
            self.assertEqual(t['backdrop']['corner_radius'],6)
            self.assertEqual(t['images'][0]['at'][1],52)
            self.assertEqual(t['images'][0]['size'][1],64)
            footer=t['texts'][-1]
            self.assertEqual(footer['at'],[14,210])
            self.assertEqual(t['styles'][footer['style']]['size'],18)
            if app=='weather':
                self.assertEqual(t['metrics'][0]['sizes'],['hero'])
                self.assertEqual(t['metrics'][0]['at'],[14,150])
                self.assertNotIn('TERAZ',json.dumps(t,ensure_ascii=False))
            else:
                self.assertEqual(t['texts'][1]['at'],[14,150])
                self.assertEqual(t['styles']['caption']['size'],60)

    def test_waste_date_uses_one_text_field_and_one_size(self):
        t=json.loads((assets.ROOT/'apps/waste/assets/template.json').read_text())
        self.assertNotIn('month', t['styles'])
        self.assertNotIn('month', {f['name'] for f in t['fields']})
        captions=[item for item in t['texts'] if isinstance(item['content'],dict) and item['content'].get('field')=='caption']
        self.assertEqual(len(captions),1)
        self.assertEqual(t['styles'][captions[0]['style']]['size'],60)
