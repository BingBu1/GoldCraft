"""Execute the production form-menu adapter with observed engine callbacks."""
import sys
from pathlib import Path

source=Path(sys.argv[1]).read_text(encoding="utf-8-sig")
regions=(("bool ServerMenuInput(){","void ClearHud(){"),
         ("void SelectFormMenu(int index){","void OpenFormMenu(){"),
         ("bool DefaultFormMenuKey(","void ReloadMinecraftResources(){"))
parts=[]
for start,end in regions:
    at=source.index(start)
    parts.append(source[at:source.index(end,at)])
Path(sys.argv[2]).write_text("\n".join(parts),encoding="utf-8")
