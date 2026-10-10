from pathlib import Path
import sys, re
output=Path(sys.argv[1]); root=Path(sys.argv[2])
paths=sys.argv[3:]
def expand(path, stack=()):
    path=path.resolve()
    if path in stack: raise RuntimeError("Recursive shader include")
    text=path.read_text(encoding="utf-8")
    return re.sub(r'^#include "([^"]+)"$', lambda m: expand(path.parent/m[1],stack+(path,)),text,flags=re.M)
lines=["#pragma once", "namespace Jade::Detail::Shaders {"]
for name in paths:
    symbol=name.replace("-","_").replace(".","_")
    text=expand(root/name)
    lines.append('inline constexpr char '+symbol+'[] = R"JADE_SHADER('+text+')JADE_SHADER";')
lines.append("}")
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text("\n".join(lines),encoding="utf-8")
