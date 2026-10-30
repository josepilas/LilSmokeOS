#!/usr/bin/env python3
"""
LilSmokeOS — OTA Server (reference implementation)

A minimal HTTP server that serves OTA update bundles to LilSmokeOS devices.

Endpoints:
    GET /manifest.json         — current version metadata
    GET /lilsmoke-update-<v>.tar.gz — download a specific bundle

Directory layout expected:
    ./releases/
        0.1.0/
            manifest.json
            lilsmoke-update-0.1.0.tar.gz
        0.2.0/
            manifest.json
            lilsmoke-update-0.2.0.tar.gz
        latest -> 0.2.0       (symlink to the latest release)

The manifest.json file format:
    {
        "version": "0.2.0",
        "sha256": "abc123...",
        "size": 24567890,
        "min_version": "0.1.0",
        "release_notes": "Added N64 support (just kidding)",
        "released_at": "2026-06-26T12:00:00Z"
    }

Usage:
    python3 ota-server.py --port 8080 --releases ./releases

In production, put this behind nginx/apache with TLS. LilSmokeOS devices
will fetch from https://updates.lilsmoke.org/manifest.json
"""

import argparse
import hashlib
import http.server
import json
import os
import socketserver
import sys
import threading
from datetime import datetime, timezone
from pathlib import Path


class OTAHandler(http.server.BaseHTTPRequestHandler):
    releases_dir = Path("./releases")

    def log_message(self, fmt, *args):
        # Simple logging
        sys.stderr.write(f"{self.address_string()} - {fmt % args}\n")

    def do_GET(self):
        if self.path == "/manifest.json":
            return self.serve_manifest()
        if self.path.startswith("/lilsmoke-update-") and self.path.endswith(".tar.gz"):
            return self.serve_bundle(self.path[1:])  # strip leading /
        return self.send_error(404, "Not found")

    def serve_manifest(self):
        # Read the "latest" symlink
        latest = self.releases_dir / "latest"
        if not latest.exists():
            return self.send_error(404, "No releases published")
        latest_dir = latest.resolve()
        manifest_path = latest_dir / "manifest.json"
        if not manifest_path.exists():
            return self.send_error(500, "manifest.json missing in latest release")
        manifest = json.loads(manifest_path.read_text())
        # Add server-side timestamp if missing
        if "served_at" not in manifest:
            manifest["served_at"] = datetime.now(timezone.utc).isoformat()
        body = json.dumps(manifest, indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def serve_bundle(self, filename):
        bundle_path = self.releases_dir / "latest" / filename
        if not bundle_path.exists():
            return self.send_error(404, "Bundle not found")
        size = bundle_path.stat().st_size
        self.send_response(200)
        self.send_header("Content-Type", "application/gzip")
        self.send_header("Content-Length", str(size))
        self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        with open(bundle_path, "rb") as f:
            while chunk := f.read(65536):
                self.wfile.write(chunk)


def make_manifest(release_dir: Path, version: str, release_notes: str = ""):
    """Generate manifest.json + sha256 for a release directory."""
    bundle = release_dir / f"lilsmoke-update-{version}.tar.gz"
    if not bundle.exists():
        raise FileNotFoundError(f"Bundle not found: {bundle}")

    size = bundle.stat().st_size
    sha256 = hashlib.sha256(bundle.read_bytes()).hexdigest()

    manifest = {
        "version": version,
        "sha256": sha256,
        "size": size,
        "min_version": "0.1.0",  # we support upgrading from any 0.1.0+
        "release_notes": release_notes,
        "released_at": datetime.now(timezone.utc).isoformat(),
    }
    (release_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"Generated manifest for {version}: sha256={sha256[:16]}... size={size}")
    return manifest


def cmd_publish(args):
    """Publish a new release: create the directory, copy the bundle, generate manifest."""
    release_dir = Path(args.releases) / args.version
    release_dir.mkdir(parents=True, exist_ok=True)
    bundle_src = Path(args.bundle)
    if not bundle_src.exists():
        print(f"error: bundle not found: {bundle_src}", file=sys.stderr)
        return 1
    bundle_dst = release_dir / f"lilsmoke-update-{args.version}.tar.gz"
    if bundle_src.resolve() != bundle_dst.resolve():
        bundle_dst.write_bytes(bundle_src.read_bytes())
    make_manifest(release_dir, args.version, args.notes)
    # Update 'latest' symlink
    latest = Path(args.releases) / "latest"
    if latest.exists() or latest.is_symlink():
        latest.unlink()
    latest.symlink_to(args.version)
    print(f"Published {args.version} as latest")
    return 0


def cmd_serve(args):
    """Run the OTA HTTP server."""
    OTAHandler.releases_dir = Path(args.releases)
    if not OTAHandler.releases_dir.exists():
        print(f"error: releases dir not found: {args.releases}", file=sys.stderr)
        return 1
    with socketserver.TCPServer((args.host, args.port), OTAHandler) as httpd:
        print(f"LilSmokeOS OTA server listening on http://{args.host}:{args.port}")
        print(f"  releases dir: {args.releases}")
        print(f"  GET /manifest.json")
        print(f"  GET /lilsmoke-update-<v>.tar.gz")
        print(f"\nPress Ctrl-C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down.")
        return 0


def cmd_make_manifest(args):
    """Just generate manifest.json for an existing release dir."""
    release_dir = Path(args.releases) / args.version
    if not release_dir.exists():
        print(f"error: release dir not found: {release_dir}", file=sys.stderr)
        return 1
    make_manifest(release_dir, args.version, args.notes)
    return 0


def main():
    p = argparse.ArgumentParser(description="LilSmokeOS OTA server")
    p.add_argument("--releases", default="./releases", help="Releases directory")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_serve = sub.add_parser("serve", help="Run the HTTP server")
    p_serve.add_argument("--host", default="0.0.0.0")
    p_serve.add_argument("--port", type=int, default=8080)

    p_pub = sub.add_parser("publish", help="Publish a new release")
    p_pub.add_argument("version", help="Version string (e.g., 0.2.0)")
    p_pub.add_argument("--bundle", required=True, help="Path to the .tar.gz bundle")
    p_pub.add_argument("--notes", default="", help="Release notes")

    p_mm = sub.add_parser("manifest", help="Generate manifest.json only")
    p_mm.add_argument("version")
    p_mm.add_argument("--notes", default="")

    args = p.parse_args()
    if args.cmd == "serve":
        return cmd_serve(args)
    if args.cmd == "publish":
        return cmd_publish(args)
    if args.cmd == "manifest":
        return cmd_make_manifest(args)
    p.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
