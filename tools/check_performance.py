"""Report native frame rates, allowing for NTSC's nominal 29.97 Hz half-rate."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('log', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('elf_snapshot', type=Path)
parser.add_argument('--require-target', action='store_true')
args = parser.parse_args()
pattern = r'STPS BENCH scene=(\S+) frames=(\d+) seconds=([\d.]+) fps=([\d.]+)'
scenes = [dict(scene=name, frames=int(frames), seconds=float(seconds), fps=float(fps))
          for name, frames, seconds, fps in re.findall(pattern, args.log.read_text(encoding='utf-8', errors='replace'))]
assert scenes and all(0 < row['fps'] <= 61 for row in scenes), 'Invalid native benchmark'
minimum = min(row['fps'] for row in scenes)
report = dict(target_fps=30, target_met=minimum >= 29.85, minimum_fps=minimum,
              elf_sha256=json.loads(args.elf_snapshot.read_text(encoding='utf-8-sig'))['Hash'],
              scenes=scenes)
walk_pattern=r'STPS WALK route=(\S+) distance=([\d.]+) slow=(\d+) total=(\d+) worst_ms=([\d.]+) state=(\d+)'
walk=[dict(route=name,distance_m=float(distance),slow_frames=int(slow),frames=int(frames),worst_frame_ms=float(worst),state=int(state))
      for name,distance,slow,frames,worst,state in re.findall(walk_pattern,args.log.read_text(encoding='utf-8',errors='replace'))]
if walk:
    report['walk_routes']=walk
    report['walk_warmup_seconds']=1
    report['slow_frame_threshold_ms']=36
    report['steady_target_met']=all(row['slow_frames']==0 and row['distance_m']>=8 and row['state']==1 for row in walk)
    report['target_met']=report['target_met'] and report['steady_target_met']
args.output.write_text(json.dumps(report, indent=2))
print(f"Native benchmark: minimum {minimum:.2f} FPS; 30 FPS target {'MET' if report['target_met'] else 'NOT MET'}")
if args.require_target and not report['target_met']:
    raise SystemExit(1)
