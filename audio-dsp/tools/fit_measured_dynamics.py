"""Fit portable engine parameters to measured steady tone levels (private inputs)."""
import ctypes as C,json,sys
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
R=Path(__file__).resolve().parents[1]
class Band(C.Structure):_fields_=[(n,C.c_double) for n in ['threshold_db','ratio','knee_db','attack_ms','release_ms','makeup_db']]
class Config(C.Structure):_fields_=[('sample_rate',C.c_uint),('equalizer_enabled',C.c_int),('compressor_enabled',C.c_int),('limiter_enabled',C.c_int),('output_gain_db',C.c_double),('crossover_low_hz',C.c_double),('crossover_high_hz',C.c_double),('bands',Band*3),('limiter_ceiling_db',C.c_double),('limiter_release_ms',C.c_double),('lookahead_frames',C.c_uint),('measured_bass_enabled',C.c_int),('input_gain_db',C.c_double),('harmonics_enabled',C.c_int)]
lib=C.CDLL(str(R/'build/libchromebook_dsp.so'));lib.cb_state_size.restype=C.c_size_t;lib.cb_default_config.argtypes=[C.POINTER(Config)];lib.cb_init.argtypes=[C.c_void_p,C.POINTER(Config)];lib.cb_process_f32.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p,C.c_size_t]
j=json.loads(Path(sys.argv[1]).read_text());qs=[q for q in j['measurements'] if q['frequency'] in [80,120,200,1000] and q['fundamental_peak_codes']>40 and q['input_dbfs']>=-48];vol={25:-42.8,50:-23.5,75:-13.5,100:-9}
# Fit volumes 50/100; reserve volume 75 as a held-out volume.
train=[q for q in qs if q['volume'] in [50,100]];hold=[q for q in qs if q['volume']==75]
n=24000;t=np.arange(n)/48000;waves={f:np.column_stack([np.sin(2*np.pi*f*t)]*2).astype('f4') for f in [80,120,200,1000]};phase={f:np.exp(-2j*np.pi*f*t[-9600:]) for f in waves}
def config(v,volume):
 c=Config();lib.cb_default_config(C.byref(c));c.compressor_enabled=1;c.measured_bass_enabled=1;c.limiter_enabled=0;c.input_gain_db=vol[volume]+23.5;c.crossover_low_hz=150;c.crossover_high_hz=500
 for b in range(3):c.bands[b].ratio=1;c.bands[b].makeup_db=v[8] if b==2 else v[b*4+3];c.bands[b].attack_ms=5;c.bands[b].release_ms=100
 for b in range(2):c.bands[b].threshold_db=v[b*4];c.bands[b].ratio=v[b*4+1];c.bands[b].knee_db=v[b*4+2]
 return c
def pred(v,q):
 c=config(v,q['volume']);s=C.create_string_buffer(lib.cb_state_size());assert lib.cb_init(s,C.byref(c))==0
 x=waves[q['frequency']]*10**(q['input_dbfs']/20);y=np.empty_like(x);lib.cb_process_f32(s,x.ctypes.data,y.ctypes.data,n);a=abs(np.sum(y[-9600:,0]*phase[q['frequency']]))*2/9600;return 20*np.log10(max(a,1e-20))
def residual(v):return np.array([pred(v,q)-q['output_fund_dbfs'] for q in train])
v=[-20,1.8,30,4,-20,2.4,35,4,-10]
fit=least_squares(residual,v,bounds=([-70,1,1,-24,-70,1,1,-24,-24],[0,5,60,24,0,5,60,24,24]),max_nfev=65,diff_step=.003,verbose=0)
out={'status':'Approximate steady-tone dynamics fit; not complete vendor dynamics','crossovers_hz':[150,500],'parameters':fit.x.tolist(),'attack_ms':5,'release_ms':100,'timing_status':'Initial timing values; measured transient fitting pending','board_volume_db':vol,'input_gain_reference_volume_db':-23.5,'metrics':{}}
for name,points in [('training',train),('held_out_volume75',hold)]:
 err=[pred(fit.x,q)-q['output_fund_dbfs'] for q in points];out['metrics'][name]={'rms_db':float(np.sqrt(np.mean(np.square(err)))),'max_abs_db':float(max(abs(np.array(err)))),'points':len(err)}
(R/'profiles/pujjoteen5-dynamics.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
