"""Check actual Qt widget and backend text exported by --verify-ui (MSVC 2017 regression)."""
import json
import sys
from pathlib import Path

data=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
assert data['title']=='柠檬云 · 远端文件传输助手', repr(data['title'])
assert data['buttons']==['选择','启动助手','停止助手','退出'], repr(data['buttons'])
assert data['backendError']=='请选择已存在的本地专用共享目录，例如 C:\\hzw。', repr(data['backendError'])
print('PASS: runtime Chinese title, all buttons and backend error text match expected Unicode.')
