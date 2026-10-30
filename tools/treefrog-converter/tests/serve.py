#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Serve only a supplied public directory under the Pages project prefix."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Handler(SimpleHTTPRequestHandler):
    def send_head(self):
        name = unquote(urlsplit(self.path).path)
        if not name.startswith(self.server.prefix) or '..' in name.split('/') or '\\' in name:
            self.send_error(404)
            return None
        self.path = '/' + name[len(self.server.prefix):]
        return super().send_head()

    def list_directory(self, path):
        self.send_error(403)
        return None

    def log_message(self, format, *args):
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8777)
    parser.add_argument('--prefix', default='/LilSmoke-Port-Converter/')
    args = parser.parse_args()
    if not args.prefix.startswith('/') or not args.prefix.endswith('/'):
        parser.error('Prefix must start and end with /')
    directory = args.directory.resolve(strict=True)
    server = ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(directory)))
    server.prefix = args.prefix
    print('http://127.0.0.1:' + str(server.server_port) + server.prefix, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
