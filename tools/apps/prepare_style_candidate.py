"""Prepare Candidate 17 from the verified prior payload, without physical media."""
from pathlib import Path
import shutil,subprocess
P=Path(__file__).resolve().parents[2]
source=P/'build/m8-payload-candidate16';target=P/'build/m8-payload-candidate17'
if target.exists():raise SystemExit('Candidate17 already exists; inspect before reusing')
shutil.copytree(source,target)
subprocess.run(['python3',str(P/'tools/m8/refresh_candidate_runtime.py'),
    '--image',str(P/'build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img'),
    '--payload',str(target),'--build','/root/lilsmoke-os/build/buildroot/output',
    '--runtime-path','usr/bin/lilsmoke-ui'],check=True)
