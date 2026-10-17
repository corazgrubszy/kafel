#!/usr/bin/env python3
"""Render the README gallery without hardware; Pillow only encodes PNG/GIF."""
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT=Path(__file__).resolve().parent.parent


def main():
    output=ROOT/'docs/images'
    with tempfile.TemporaryDirectory(prefix='kafel-preview-') as directory:
        source=Path(directory)
        subprocess.run(['zig','build','preview-assets','-Doptimize=ReleaseFast',
                        '-Dpreview-dir='+directory],cwd=ROOT,check=True)
        output.mkdir(parents=True,exist_ok=True)
        for name in ('dashboard','weather','waste'):
            with Image.open(source/(name+'.ppm')) as original:
                assert original.size==(240,320)
                original.save(output/(name+'.png'),optimize=True)
        frames=[]
        for path in sorted(source.glob('frame-*.ppm')):
            with Image.open(path) as original:
                assert original.size==(240,320)
                frames.append(original.convert('RGB'))
        assert len(frames)==150
        # One palette for the entire loop avoids colour shifts between frames.
        samples=Image.new('RGB',(240,320*3))
        for row,index in enumerate((0,60,90)):samples.paste(frames[index],(0,row*320))
        palette=samples.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
        indexed=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
        indexed[0].save(output/'ribbon.gif',save_all=True,append_images=indexed[1:],
                        duration=40,loop=0,optimize=True,disposal=1)
    for path in sorted(output.glob('*')):
        if path.suffix in ('.png','.gif'):print(f'{path.relative_to(ROOT)}: {path.stat().st_size} bytes')


if __name__=='__main__':
    main()
