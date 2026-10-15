#!/usr/bin/env python3
"""Reproduce the C calibration table from the human-readable profile export."""
from pathlib import Path
import json,math
root=Path(__file__).resolve().parents[1]
j=json.loads((root/'profiles/pujjoteen5.json').read_text())
assert j['sample_rate']==48000 and j['channels']==2 and len(j['sections'])==14
lines=['/* Recovered numeric speaker calibration. No Waves executable code. */','static const double speaker_sos[14][5] = {']
for s in j['sections']:
 assert len(s['b'])==3 and len(s['a'])==3 and s['a'][0]==1
 v=s['b']+s['a'][1:];assert all(math.isfinite(x) for x in v)
 lines.append('    {'+', '.join(float(x).hex() for x in v)+'},')
lines+=['};',''];(root/'src/speaker_profile.h').write_text('\n'.join(lines))
