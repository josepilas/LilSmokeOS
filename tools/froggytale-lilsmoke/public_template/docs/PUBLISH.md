# Maintainer Publication Checklist

The local staging task does not create a GitHub repository, push or publish.
The maintainer reviews the source inventory, license boundaries, exact engine
hash, build report and actual MIPS installer report before running commands.

The repository is `josepilas/Undertale-LilSmoke`. The **tag and title are both
exactly `0.0.1`**, without a `v` prefix. From the reviewed checkout:

```sh
python3 -B tools/audit.py
git status --short
git config core.autocrlf false
python3 -c 'import json,pathlib; m=json.loads(pathlib.Path("SOURCE-MANIFEST.json").read_text()); pathlib.Path("../public-pathspec.txt").write_bytes(b"\0".join(x.encode() for x in [*m["files"],"SOURCE-MANIFEST.json"])+b"\0")'
git --literal-pathspecs add -f --pathspec-from-file=../public-pathspec.txt --pathspec-file-nul
git commit -m "Public Undertale-LilSmoke 0.0.1 source"
gh repo create josepilas/Undertale-LilSmoke --public --source . --remote origin --push
git tag -a 0.0.1 -m 0.0.1
git push origin 0.0.1
gh release create 0.0.1 \
  ../public-release-0.0.1/Undertale-LilSmoke-0.0.1.lilpck \
  ../public-release-0.0.1/Undertale-LilSmoke-0.0.1-corresponding-source.zip \
  ../public-release-0.0.1/SHA256SUMS \
  ../public-release-0.0.1/manifest.json \
  ../public-release-0.0.1/package-qa.json \
  ../public-release-0.0.1/HANDOFF.json \
  --repo josepilas/Undertale-LilSmoke --title 0.0.1 --notes-file RELEASE-NOTES.md
```

Do not include local QA build directories, a compiler SDK, owner data, derived
ATLAS files, Flowey art, game music, saves, credentials or private logs. Do not
publish a binary without its matching source ZIP in the same release. Verify
the source-offer URL uses `/releases/download/0.0.1/` after upload. The release
engine's internal 0.1.0 diagnostic label is intentionally documented.
