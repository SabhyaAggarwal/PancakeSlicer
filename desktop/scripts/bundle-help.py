#!/usr/bin/env python3
import argparse
import html
from html.parser import HTMLParser
from pathlib import Path
import shutil
import markdown


class LocalLinks(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.out = []

    def handle_starttag(self, tag, attrs):
        values = []
        for key, value in attrs:
            if value is None:
                values.append(key)
                continue
            if key == 'href' and not value.startswith(('https:', 'http:', 'mailto:', '#')):
                path, marker, fragment = value.partition('#')
                if path.endswith('.md'):
                    path = path[:-3] + '.html'
                value = path + (marker + fragment if marker else '')
            if key in {'src', 'href'} and value.startswith(('http:', 'https:', '//')) and tag in {'img', 'script', 'link', 'iframe'}:
                raise ValueError(f'External resource in bundled guide: {value}')
            values.append(f'{key}="{html.escape(value, quote=True)}"')
        self.out.append('<' + tag + (' ' + ' '.join(values) if values else '') + '>')

    def handle_endtag(self, tag):
        self.out.append('</' + tag + '>')

    def handle_data(self, data):
        self.out.append(data)

    def handle_entityref(self, name):
        self.out.append('&' + name + ';')

    def handle_charref(self, name):
        self.out.append('&#' + name + ';')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True, type=Path)
    ap.add_argument('--web', required=True, type=Path)
    args = ap.parse_args()
    source = args.root/'docs/user-guide'
    target = args.web/'help'
    target.mkdir(parents=True, exist_ok=True)
    style = 'body{max-width:960px;margin:3rem auto;padding:0 1.5rem;font:17px/1.6 system-ui,sans-serif;color:#242424;background:#fff}a{color:#63378f}img{max-width:100%;height:auto;border-radius:8px}pre{overflow:auto;padding:1rem;background:#f3f3f3}table{border-collapse:collapse;width:100%}td,th{padding:.5rem;border:1px solid #ddd}nav{margin-bottom:2rem}code{font-size:.9em}'
    for path in source.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        if path.suffix == '.md':
            destination = target/relative.with_suffix('.html')
            parser = LocalLinks()
            parser.feed(markdown.markdown(path.read_text(encoding='utf-8'), extensions=['tables', 'fenced_code', 'toc']))
            title = next((line.lstrip('# ').strip() for line in path.read_text(encoding='utf-8').splitlines() if line.startswith('# ')), path.stem)
            page = '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src \'self\';img-src \'self\' data:;style-src \'unsafe-inline\';script-src \'none\';connect-src \'none\'"><title>' + html.escape(title) + ' · PancakeSlicer</title><style>' + style + '</style><body><nav><a href="/help/README.html">PancakeSlicer user guide</a></nav><main>' + ''.join(parser.out) + '</main></body></html>'
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(page, encoding='utf-8')
        elif path.suffix.lower() in {'.webp', '.png', '.jpg', '.svg'}:
            destination = target/relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    shutil.copy2(target/'README.html', target/'index.html')
    shutil.copy2(args.root/'LICENSE', args.web/'licence.txt')
    print('Bundled help files:', sum(path.is_file() for path in target.rglob('*')))


if __name__ == '__main__':
    main()
