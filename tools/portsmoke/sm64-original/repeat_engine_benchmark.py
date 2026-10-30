"""Repeat a cached exact-output engine pair with child CPU-time measurements."""
import argparse
import json
from pathlib import Path
import resource
import statistics
from test_performance_engine import execute

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine',type=Path,required=True)
    p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--width',choices=('256','320'),default='320')
    p.add_argument('--pairs',type=int,default=3)
    a=p.parse_args();assert 1<=a.pairs<=5
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    products=json.loads((a.engine/'build.json').read_text())
    for version in ('before','after'):products[version]['binary']=str((a.engine/products[version]['binary']).resolve())
    result={'passed':False,'width':int(a.width),'physical_fps_measured':False,'runs':[],'pairs':[]}
    try:
        for pair in range(a.pairs):
            values={}
            for version in (('before','after') if pair%2==0 else ('after','before')):
                before=resource.getrusage(resource.RUSAGE_CHILDREN)
                run=execute(a.out,products,a.runtime,f'{pair}-{version}',version,{'SM64_SOFTWARE_WIDTH':a.width},2700)
                after=resource.getrusage(resource.RUSAGE_CHILDREN)
                run['host_child_cpu_seconds']=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime
                values[version]=run;result['runs'].append(run)
                (a.out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
            for key in ('images','state','audio'):assert values['before'][key]==values['after'][key],(pair,key)
            result['pairs'].append({'cpu_ratio':values['after']['host_child_cpu_seconds']/values['before']['host_child_cpu_seconds'],
                'raster_ratio':values['after']['raster_ms']/values['before']['raster_ms']})
        result.update(passed=True,pixel_state_audio_equal=True,
            median_cpu_ratio=statistics.median(x['cpu_ratio'] for x in result['pairs']),
            median_raster_ratio=statistics.median(x['raster_ratio'] for x in result['pairs']))
    finally:
        (a.out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS repeated exact-output engine pairs',result['median_cpu_ratio'],result['median_raster_ratio'])

if __name__=='__main__':main()
