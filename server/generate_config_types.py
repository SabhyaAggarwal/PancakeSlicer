#!/usr/bin/env python3
"""Extract the native string/point-group serializers from the UI's PrintConfigDef dictionary."""
import argparse
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("options", type=Path, help="web/src/options.generated.json")
args = parser.parse_args()
options = json.loads(args.options.read_text(encoding="utf-8"))
hidden = re.compile(r"print_host|printhost|printer_agent|host_type|api_key|bbl_use_printhost|upload|cloud|device|octoprint|_plugin_config_overrides")
kinds = {"string": "string", "strings": "strings", "coPointsGroups": "groups"}
groups = {kind: sorted(key for key, definition in options.items()
                       if definition["type"] == native and not hidden.search(key))
          for native, kind in kinds.items()}
groups["comma"] = sorted(key for key, definition in options.items()
                         if definition["type"] not in kinds and not hidden.search(key))
Path(__file__).with_name("config_types.json").write_text(json.dumps(groups, indent=2, sort_keys=True) + "\n", encoding="utf-8")
