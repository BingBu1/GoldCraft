import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('observer', ROOT/'tools/Observe-SteamStall.py')
observer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(observer)


def event(stamp, elapsed=100):
    return dict(stage='frameGap', completedSteadySeconds=stamp, elapsedMs=elapsed)


def row(count, history, menu=False):
    return dict(utc=str(count), slowCalls=count, longCalls=history, hostGameMenu=menu)


class ObserverTests(unittest.TestCase):
    def test_excludes_baseline_and_repeated_history(self):
        result = observer.summarize([row(8, [event(1, 9000)]), row(9, [event(1, 9000), event(2, 123)]),
                                     row(9, [event(1, 9000), event(2, 123)], True)])
        self.assertEqual((1, 1, 123), (result['slowCallDelta'], result['observedNewFrameGaps'],
                                      result['maxObservedNewFrameGapMs']))
        self.assertEqual(1, len(result['menuEdges']))

    def test_reports_events_lost_from_rolling_buffer(self):
        result = observer.summarize([row(4, [event(1)]), row(9, [event(8), event(9)])])
        self.assertEqual(3, result['unobservedHistoryEvents'])

    def test_rejects_cross_session_counter_reset(self):
        with self.assertRaises(ValueError):
            observer.summarize([row(4, [event(1)]), row(0, [])])

    def test_refuses_output_outside_build(self):
        for path in (ROOT/'sandbox/out', ROOT/'build/../sandbox/out', ROOT.parent/'outside'):
            with self.assertRaises(ValueError):
                observer.workspace_path(path, 'build')

    def test_debugger_commands_never_suspend_or_execute_target(self):
        args = observer.debugger_args(Path('debuggers/x86/cdb.exe'), 123, Path('build/symbols'))
        self.assertEqual(['-pvr', '-p', '123', '-sins', '-y', 'build\\symbols', '-c', '~* kn 35;lm;qd'], args[1:])


if __name__ == '__main__':
    unittest.main()
