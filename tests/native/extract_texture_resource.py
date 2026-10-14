"""Extract actual form transition and chunk acceptance for lifecycle regression."""
import sys
from pathlib import Path
source=Path(sys.argv[1]).read_text(encoding="utf-8-sig")
parts=[]
for start,end in (("int FormMessage(","int AvatarMessage("),
                  ("void ReadParticleTextureChunk(","void ReadBlockFeedback(")):
    at=source.index(start);parts.append(source[at:source.index(end,at)])
Path(sys.argv[2]).write_text("\n".join(parts),encoding="utf-8")
