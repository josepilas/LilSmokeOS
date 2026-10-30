#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only validation of an owner's real ROM; no public game-data output."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from cook import Rom, US_SHA1, digest_file


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    before=digest_file(args.rom)
    tested=[]
    rom=Rom(args.rom)
    try:
        with tempfile.TemporaryDirectory(prefix='sm64-byo-validation-') as temporary:
            output=Path(temporary)/'normalized'
            rom.write_normalized(output,lambda *values: None)
            raw=output.read_bytes()
            assert hashlib.sha1(raw).hexdigest()==US_SHA1
            for order in (1,2,4):
                candidate=Path(temporary)/('arbitrary name '+str(order))
                data=raw
                if order!=1:
                    data=bytearray(len(raw))
                    for at in range(order): data[at::order]=raw[order-at-1::order]
                candidate.write_bytes(data)
                trial=Rom(candidate)
                try:
                    trial.write_normalized(Path(temporary)/('result-'+str(order)),lambda *values: None)
                    tested.append(order)
                finally:
                    trial.close()
    finally:
        rom.close()
    after=digest_file(args.rom)
    assert before==after
    result=dict(passed=True,original_rom_unchanged=True,original_sha256=after,
        normalized_sha1=US_SHA1,byte_orders_tested=tested,arbitrary_names_tested=True,
        extracted_assets_written=False,gameplay_tested=False)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
