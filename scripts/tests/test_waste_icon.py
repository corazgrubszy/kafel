"""Pinned two-colour waste art; optional offline tools verify vector geometry."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('waste_icon',Path(__file__).resolve().parents[1]/'prepare_waste_icon.py')
icon=importlib.util.module_from_spec(spec);spec.loader.exec_module(icon)
TOOLS=importlib.util.find_spec('pathops') is not None and importlib.util.find_spec('fontTools') is not None


class WasteIconTests(unittest.TestCase):
    def test_artwork_provenance(self):
        meta=json.loads((icon.OUTPUT/'mixed-bin.json').read_text())
        source=(icon.ROOT/'vendor/heroicons/trash-solid.svg').read_bytes()
        self.assertEqual(meta['source_sha256'],hashlib.sha256(source).hexdigest())
        self.assertEqual(meta['prepared_sha256'],hashlib.sha256((icon.OUTPUT/'mixed-bin.svg').read_bytes()).hexdigest())
        self.assertEqual((meta['body'],meta['lid']),('#5B5B5B','#E6B800'))

    @unittest.skipUnless(TOOLS,'offline artwork generation tools only')
    def test_body_and_lid_preserve_complete_silhouette_and_holes(self):
        import pathops
        original,_=icon.original_path()
        body,lid=icon.split_paths()
        combined=pathops.op(body,lid,pathops.PathOp.UNION)
        difference=pathops.op(original,combined,pathops.PathOp.XOR)
        self.assertLess(abs(difference.area),0.001)
        self.assertLess(abs(pathops.op(body,lid,pathops.PathOp.INTERSECTION).area),0.001)
        self.assertAlmostEqual(body.bounds[1],icon.SPLIT_Y)
        self.assertAlmostEqual(lid.bounds[3],icon.SPLIT_Y)
        for x in range(3,22):
            for y in range(2,23):
                point=(x+0.1,y+0.1)
                self.assertEqual(original.contains(point),body.contains(point) or lid.contains(point))

    @unittest.skipUnless(TOOLS,'offline artwork generation tools only')
    def test_split_is_reproducible(self):
        self.assertEqual(icon.prepared(),(icon.OUTPUT/'mixed-bin.svg').read_bytes())
