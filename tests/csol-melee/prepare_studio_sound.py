"""Extract the production sound-filter entry for studio_sound.cpp."""
from pathlib import Path
import argparse


def extract(scene):
    text = Path(scene).read_text(encoding='utf-8-sig')
    signature = 'void StudioEvent(const mstudioevent_t* event,const cl_entity_t* entity)\n{\n'
    assert text.count(signature) == 1, 'StudioEvent entry is missing or ambiguous'
    start = text.index(signature) + len(signature)
    end = text.index('    if(CannonStudioEvent(event,entity))return;', start)
    entry = text[start:end]
    assert entry.count('SkipStudioSound(') == 1, 'Sound-filter entry changed'
    assert entry.count('strnlen(') == 2, 'Expected bounded SDK string access'
    return '#line %d "%s"\n%s' % (text.count('\n', 0, start) + 1,
        Path(scene).resolve().as_posix(), entry)


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, default=root/'native/csol/client/scene.cpp')
    parser.add_argument('--output', type=Path, required=True, help='Fresh generated include outside the source tree')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf8', newline='\n') as stream:
        stream.write(extract(args.scene))
