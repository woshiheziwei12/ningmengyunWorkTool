"""Extract only data fixtures for optional local regression; never copy request headers/tokens.

Usage: python prepare_wecom_capture.py CAPTURE.txt OUTPUT_DIRECTORY
OUTPUT_DIRECTORY must be an ignored local build directory; fixtures contain private table data.
"""
import gzip
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit, parse_qs

source = Path(sys.argv[1]).read_bytes()
destination = Path(sys.argv[2]).resolve()
if not any(part.lower() in ("buildcheck", ".codex-work") for part in destination.parts):
    raise SystemExit("Use an ignored BuildCheck or .codex-work output directory")
destination.mkdir(parents=True, exist_ok=True)
count = 0
for match in re.finditer(rb'(?m)^GET ([^\r\n ]+/dop-api/(?:opendoc|get/sheet)\?[^\r\n ]+) HTTP', source):
    url = urlsplit(match.group(1).decode())
    query = parse_qs(url.query)
    start = source.index(b'HTTP/1.1 ', match.end())
    end = source.index(b'\r\n\r\n', start)
    headers = source[start:end].decode('latin1')
    position = end + 4
    if 'chunked' in headers.lower():
        chunks = []
        while True:
            end = source.index(b'\r\n', position)
            length = int(source[position:end].split(b';')[0], 16)
            position = end + 2
            if not length:
                break
            chunks.append(source[position:position+length])
            position += length + 2
        body = b''.join(chunks)
    else:
        length = int(re.search(r'Content-Length: (\d+)', headers, re.I)[1])
        body = source[position:position+length]
    if 'gzip' in headers.lower():
        body = gzip.decompress(body)
    response = json.loads(body)
    if url.path.endswith('/opendoc'):
        client = response['clientVars']
        collab = client['collab_client_vars']
        clean = {'clientVars': {'title': client['title'], 'collab_client_vars': {
            'padId': collab['padId'], 'rev': collab['rev'], 'header': [
                {'d': [{k: s[k] for k in ('id', 'name')} for s in group.get('d', [])]}
                for group in collab['header']]}}}
        (destination / 'metadata.json').write_text(json.dumps(clean, ensure_ascii=False), encoding='utf-8')
    elif query.get('endrow') == ['-1']:
        data = response['data']
        clean = {'retcode': response['retcode'], 'data': {k: data[k] for k in (
            'rev', 'padSubId', 'sheetId', 'startrow', 'endrow', 'async_open_done', 'initialAttributedText') if k in data}}
        count += 1
        (destination / f'sheet-{count}.json').write_text(json.dumps(clean, ensure_ascii=False), encoding='utf-8')
        print('Fixture', count, 'range', data.get('startrow'), data.get('endrow'), 'async_done', data.get('async_open_done'))
print('Prepared', count, 'sheet fixtures without request URLs, cookies or authentication fields.')
