#!/usr/bin/env python3
"""Local-only SVG benchmark; no SSH, services or deployment."""
import json
import platform
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parent.parent

def main():
    subprocess.run(['zig','build','weather-svg-bench','-Doptimize=ReleaseFast'],cwd=ROOT,check=True)
    base=ROOT/'libs/ui/examples/weather-card'
    executable=ROOT/'zig-out/bin/weather-svg-bench'
    cpu=platform.processor()
    if platform.system()=='Darwin':cpu=subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip()
    report={'platform':platform.platform(),'machine':platform.machine(),'cpu':cpu,
        'zig':subprocess.check_output(['zig','version'],text=True).strip(),'optimize':'ReleaseFast',
        'native_benchmark_bytes':executable.stat().st_size,
        'method':'Fresh process per SVG. 100 parse+destroy; first raster; 300 warm raster; 200 cached card; 100 uncached card. Same text data for every illustration. NanoSVG malloc/realloc instrumentation is benchmark-only; requested live/peak bytes exclude allocator headers and C++ storage. ru_maxrss is whole-process high-water mark, not live heap or SVG-only RAM. Fonts are loaded from files.',
        'cases':{}}
    for scene in ('sun','clouds','rain','snow'):
        result=subprocess.run([str(executable),str(ROOT/'libs/ui/assets/ibm-plex-sans/IBMPlexSans-Regular.ttf'),
            str(ROOT/'libs/ui/assets/ibm-plex-sans/IBMPlexSans-Bold.ttf'),str(base/'weather.json'),str(base/'svg'/(scene+'.svg'))],
            check=True,text=True,capture_output=True)
        sample=json.loads(result.stdout)
        if sample['nanosvg_live_after_cache_bytes']!=0:raise RuntimeError('SVG heap not released after caching')
        if sample['cached_paint_svg_allocation_calls']!=0:raise RuntimeError('Cached paint must not call SVG allocator')
        report['cases'][scene]=sample
        print(scene,json.dumps(sample),flush=True)
    output=base/'measurements-local.json';output.write_text(json.dumps(report,indent=2)+'\n')
    print('Saved local measurements:',output)

if __name__=='__main__':main()
