# SPDX-License-Identifier: MPL-2.0
"""Inspect/convert local archives; no per-game recipes or execution."""
import argparse,ctypes,hashlib,io,json
from pathlib import Path
import zipfile

HERE=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--input',type=Path,action='append',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--runtime',type=Path);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    c=ctypes.CDLL(str(HERE/'_build/libtfconvert.so'))
    c.tf_open.argtypes=c.tf_runtime.argtypes=[ctypes.c_void_p,ctypes.c_size_t];c.tf_report.restype=c.tf_error.restype=ctypes.c_char_p
    c.tf_convert.argtypes=[ctypes.c_uint,ctypes.c_char_p,ctypes.c_char_p];c.tf_output_size.restype=ctypes.c_size_t;c.tf_output_data.restype=ctypes.c_void_p
    results=[]
    for source in a.input:
        data=source.read_bytes();buffer=ctypes.create_string_buffer(data)
        if c.tf_open(buffer,len(data))!=1:raise RuntimeError(c.tf_error().decode())
        if a.runtime:
            runtime_data=a.runtime.read_bytes();runtime_buffer=ctypes.create_string_buffer(runtime_data)
            if c.tf_runtime(runtime_buffer,len(runtime_data))!=1:raise RuntimeError(c.tf_error().decode())
        report=json.loads(c.tf_report());(a.out/(source.stem+'-analysis.json')).write_text(json.dumps(report,indent=2)+'\n')
        for index,port in enumerate(report['ports']):
            if not port['ready']:raise RuntimeError(port['reason'])
            if c.tf_convert(index,port['name'].encode(),b'0.2.0')!=1:raise RuntimeError(c.tf_error().decode())
            output=ctypes.string_at(c.tf_output_data(),c.tf_output_size());target=a.out/(port['id']+'-0.2.0.lilpck');target.write_bytes(output)
            with zipfile.ZipFile(io.BytesIO(data))as original,zipfile.ZipFile(io.BytesIO(output))as package:
                assert package.testzip()is None
                config=next(n for n in original.namelist()if n.endswith('/config.json'));prefix=config[:-len('config.json')]
                preserved=all(package.read('runtime/port/'+n[len(prefix):])==original.read(n)for n in original.namelist()if n.startswith(prefix)and not n.endswith('/'))
                assert preserved
            results.append(dict(input=str(source),output=str(target),sha256=hashlib.sha256(output).hexdigest(),bytes=len(output),original_files_preserved=preserved,analysis=port))
        c.tf_close()
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,actual_execution=False,results=results),indent=2)+'\n')
    print(json.dumps([dict(name=r['analysis']['name'],ready=True,helpers=r['analysis']['helper_executables'],output=r['output'],bytes=r['bytes'])for r in results],indent=2))
if __name__=='__main__':main()
