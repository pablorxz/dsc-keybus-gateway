"""Independently validate the completed, opt-in bench soak log; no device I/O."""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / '.local/bench/soak.log'


def analyze(text):
    limits = ('limits: 2700 s; sample gap <= 90 s; warmup=5 samples; '
              'free and largest drop <= 12288 bytes; largest >= 16384 bytes; '
              'HTTP stack minimum >= 512 bytes, >= 20 samples')
    lines = text.splitlines()
    if not lines or lines[0] != limits:
        raise ValueError('Missing expected limits declared before the run')
    samples, stacks, polls = [], [], []
    terminal = None
    for line in lines[1:]:
        if match := re.fullmatch(r'sample (\d+) elapsed=(\d+) uptime=(\d+) free=(\d+) largest=(\d+)', line):
            samples.append(tuple(map(int, match.groups())))
        elif match := re.fullmatch(r'http_stack elapsed=(\d+) min_free=(\d+)', line):
            stacks.append(tuple(map(int, match.groups())))
        elif match := re.fullmatch(r'http ok elapsed=(\d+)', line):
            polls.append(int(match.group(1)))
        elif match := re.fullmatch(r'PASS samples=(\d+) stack_samples=(\d+) min_http_stack=(\d+) elapsed=(\d+)', line):
            if terminal is not None: raise ValueError('Multiple terminal records')
            terminal = tuple(map(int, match.groups()))
        else:
            raise ValueError('Unrecognized or incomplete record')
    if terminal is None or not lines[-1].startswith('PASS '):
        raise ValueError('No completed PASS record')
    count, stack_count, min_stack, duration = terminal
    if duration < 2700 or count != len(samples) or stack_count != len(stacks):
        raise ValueError('Duration or recorded sample counts are inconsistent')
    if len(samples)<44 or len(stacks)<20 or len(polls)<44:
        raise ValueError('Insufficient heap, stack or HTTP samples')
    if min(value for _,value in stacks) != min_stack or min_stack < 512:
        raise ValueError('HTTP task stack headroom below declared limit')
    baseline_free,baseline_largest=samples[4][3:5]
    for index,(sequence,elapsed,uptime,free,largest) in enumerate(samples):
        if sequence!=index+1 or elapsed>duration or largest<16384:
            raise ValueError('Invalid heap sample sequence or largest-block threshold')
        # A reset shortly after the run starts could otherwise precede the first
        # pair of samples. Allow two seconds for clock drift and rounding.
        if uptime+2000 < elapsed*1000:
            raise ValueError('Device uptime is shorter than the observed run')
        if index and (elapsed <= samples[index-1][1] or elapsed-samples[index-1][1]>90 or uptime<=samples[index-1][2]):
            raise ValueError('Sample gap, time order or device uptime failed')
        if index>=5 and (baseline_free-free>12288 or baseline_largest-largest>12288):
            raise ValueError('Heap fell beyond declared tolerance')
    if (duration-samples[-1][1]>90 or polls[0]>10 or polls[-1]>duration or
        any(b<=a or b-a>90 for a,b in zip(polls,polls[1:]))):
        raise ValueError('HTTP polling or final sample gap failed')
    if (duration-polls[-1]>90 or duration-stacks[-1][0]>150 or
        any(elapsed>duration for elapsed,_ in stacks) or
        any(b[0]<=a[0] for a,b in zip(stacks,stacks[1:]))):
        raise ValueError('HTTP polling or stack sample is outside the run')
    return {'duration_s':duration, 'samples':len(samples), 'http_polls':len(polls),
            'stack_samples':len(stacks), 'min_stack_bytes':min_stack,
            'warmup_free_bytes':baseline_free, 'final_free_bytes':samples[-1][3],
            'warmup_largest_bytes':baseline_largest, 'final_largest_bytes':samples[-1][4]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--path', type=Path, default=DEFAULT)
    args = parser.parse_args()
    try:
        print('PASS', analyze(args.path.read_text(encoding='utf-8')))
    except (OSError, ValueError) as error:
        print(f'Soak evidence not accepted: {error}', file=sys.stderr)
        sys.exit(1)
