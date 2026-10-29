"""Capture actual dedicated-server frame/cache diagnostics during human gameplay.

Only diagnostic commands are sent. This tool does not spawn, move, attack,
restart a map, or alter AI. A report is evidence, not automatic acceptance.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=30)
    args = parser.parse_args()
    if not 5 <= args.duration <= 120:
        parser.error('duration must be 5..120 seconds')
    output = args.output.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('output must be under workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('linked output directory refused')
    output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('rcon', ROOT / 'tools/GoldSrc-Command.py')
    rcon = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rcon)
    records, errors = [], []
    start = time.monotonic()
    active = False
    with (output / 'commands.jsonl').open('x', encoding='utf-8') as stream:
        def command(text):
            response = rcon.command(text)
            record = dict(elapsed=time.monotonic() - start, command=text, response=response)
            records.append(record)
            stream.write(json.dumps(record, ensure_ascii=False) + '\n')
            stream.flush()
            return response

        try:
            command('status')
            cache = command('gc_cache_stats')
            if 'GoldCraft cache:' not in cache or 'separate=' not in cache:
                raise RuntimeError('The running engine lacks the matched cache diagnostics')
            # End is guaranteed even if the acknowledgement gets lost.
            active = True
            if 'enabled=1' not in command('csol_scenario_perf begin'):
                raise RuntimeError('The running GameDLL did not enable frame diagnostics')
            deadline = time.monotonic() + args.duration
            while time.monotonic() < deadline:
                command('stats')
                time.sleep(min(2, max(0, deadline - time.monotonic())))
        except Exception as error:
            errors.append(str(error))
        finally:
            if active:
                try: command('csol_scenario_perf end')
                except Exception as error: errors.append(str(error))
            try: command('gc_cache_stats')
            except Exception as error: errors.append(str(error))

    report = dict(at=datetime.now(timezone.utc).isoformat(), durationRequested=args.duration,
                  records=len(records), errors=errors, gameplayCommands=[],
                  acceptance='unverified', boundary='Match active living monster counts, actual combat and observer position separately. Rejected, terminating and empty scenes are not battle FPS evidence. Percentile fields are histogram bucket upper bounds.')
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return int(bool(errors))


if __name__ == '__main__':
    raise SystemExit(main())
