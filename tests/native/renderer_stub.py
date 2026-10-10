"""Generate test-only aborting overrides from the exact, configured Renderer SDK.

No copied third-party header or guessed vtable layout is stored in the project.
The driver-backed test overrides the three API operations it actually uses.
"""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8-sig")
source = source.split("class IMetaRenderer : public IBaseInterface", 1)[1]
declarations = re.findall(r"^\s*virtual (.+?) = 0;\s*$", source, re.MULTILINE)
assert len(declarations) == source.count("virtual "), "Review changed SDK declarations"
assert len(declarations) > 80
output = Path(sys.argv[2])
output.write_text("// Generated from the configured SDK; unused calls abort.\n"
                  "struct UnusedRenderer : IMetaRenderer {\n" +
                  "\n".join(f"    {line} override {{ std::abort(); }}" for line in declarations) +
                  "\n};\n", encoding="utf-8")
