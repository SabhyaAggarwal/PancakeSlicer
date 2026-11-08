import json
from pathlib import Path

def read(path):
    path = Path(path)
    if not path.exists():
        return None
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("schema") != "orca.nonplanar_slice.v1":
        raise ValueError("unsupported slice JSON schema in %s" % path)
    return record

def summary(record):
    return record["text_summary"]
