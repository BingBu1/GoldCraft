"""Extract matched gravity functions; assert production PM entry wiring."""
from pathlib import Path
import sys
source=Path(sys.argv[1]);output=Path(sys.argv[2]);s=source.read_text(encoding='utf-8-sig')
parts=[]
for marker in ['void PM_AddCorrectGravity()','void PM_FixupGravityVelocity()']:
    start=s.index(marker+'\n{');end=s.index('\n}',start)+2;parts.append(s[start:end])
for call in ['GoldCraft_StatusCategorize(PM_CategorizePositionOriginal)','GoldCraft_StatusWater(PM_WaterMove)','GoldCraft_StatusJump(PM_Jump)']:
    assert call in s,call
output.write_text('using real_t=double;\n'+'\n'.join(parts),encoding='utf-8')
