"""Adapt licensed artwork without silently dropping meaningful SVG features."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

spec=importlib.util.spec_from_file_location('compare_meteocons',Path(__file__).resolve().parents[1]/'compare_meteocons.py')
comparison=importlib.util.module_from_spec(spec);spec.loader.exec_module(comparison)

class MeteoconsTests(unittest.TestCase):
    def test_all_eight_keep_path_geometry_and_use_rose_pine(self):
        palette={'#c4a7e7','#e0def4','#9ccfd8','#f6c177','#ebbcba'}
        for style in ('fill','flat'):
            for scene,name in comparison.CASES.items():
                with self.subTest(style=style,scene=scene):
                    original=(comparison.SOURCE/style/(name+'.svg')).read_bytes()
                    before=ET.fromstring(original);after=ET.fromstring(comparison.adapt(original,scene))
                    paths=lambda root:[(e.get('d'),e.get('transform'),e.get('stroke-width'),e.get('stroke-linecap')) for e in root.iter() if comparison.local(e.tag)=='path']
                    self.assertEqual(paths(before),paths(after))
                    self.assertEqual(after.get('viewBox'),'0 0 240 240')
                    for element in after.iter():
                        self.assertNotEqual(comparison.local(element.tag),'clipPath')
                        self.assertNotIn('clip-path',element.attrib)
                        for attribute in ('fill','stroke','stop-color'):
                            value=element.get(attribute)
                            if value and value!='none' and not value.startswith('url(#'):self.assertIn(value,palette)
    def test_nontrivial_clip_is_rejected(self):
        svg=b'<svg viewBox="0 0 128 128"><defs><clipPath id="a"><rect width="64" height="128"/></clipPath></defs><g clip-path="url(#a)"/></svg>'
        with self.assertRaises(ValueError):comparison.adapt(svg)
    def test_masks_and_animation_are_not_silently_removed(self):
        for feature in ('mask','filter','use','animate','animateTransform','style'):
            with self.subTest(feature=feature),self.assertRaises(ValueError):
                comparison.adapt(('<svg viewBox="0 0 128 128"><'+feature+'/></svg>').encode())
    def test_unmapped_color_is_rejected(self):
        with self.assertRaises(ValueError):comparison.adapt(b'<svg viewBox="0 0 128 128"><rect fill="#123456"/></svg>')
