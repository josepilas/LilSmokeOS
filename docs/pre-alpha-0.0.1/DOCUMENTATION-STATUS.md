<!-- SPDX-License-Identifier: MPL-2.0 -->
# Documentation Handoff

Prepared 2026-10-12 for the release owner. This is a documentation checkpoint,
not a declaration that Pre-Alpha-0.0.1 or the original SM64 port is released.

## Coverage

The release folder covers installation/update/storage, stock import, UI/shutdown,
emulators/archives, controllers/pointers, applications, themes, media, network,
troubleshooting, release notes, licensing scope and publication gates.
The [developer index](../developer/README.md) covers source/build, board/profile
porting, application packages and testing. Root credits/contribution guidance
and three structured issue forms are included.

The root README's expected `installation.md`, `user-manual.md` and
`troubleshooting.md` names use lowercase filenames, including on case-sensitive
hosts. Links to the parent-maintained LICENSE, LICENSE_POLICY.md and
THIRD_PARTY_NOTICES.md are present. The owner-provided GitHub repository, issues
and release-index URLs are used. No version-specific download, tag, checksum
or artifact inventory has been invented.

## Checks

- Local Markdown targets, filename case, code fences and original-document
  SPDX notices reviewed; no private PC paths or card identities in new guides.
- All four issue-template YAML files parsed with duplicate-key rejection;
  three forms checked for supported field types, unique IDs and attribute types.
  GitHub's live UI/rendering remains untested. Its
  [official form syntax](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/syntax-for-issue-forms)
  is the schema reference.
- Package examples and UI/control names compared with actual source and current
  development evidence. Older roadmap claims are not copied as certifications.
- No source, build configuration, component licenses, private data/images,
  release tooling or SD files were changed by this documentation task.

## Remaining Release-Owner Work

Reconcile final source/licensing/payload inventory, actual published source URL,
checksums, profile geometry and test results with these guides. A private
SM64 options-menu revision is still private development, not a distributable
BYO release. Vendor dependencies and the current SM64 renderer/assets remain
publication blockers until cleared or removed/replaced. Native macOS, media
hardware playback and new adapted-board acceptance retain explicit test gaps.
Follow the [publication checklist](PUBLICATION-CHECKLIST.md).
