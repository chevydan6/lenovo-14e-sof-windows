#!/usr/bin/env python3
"""Independent signal/format tests. Requires numpy and scipy, not the decoder."""
import ctypes as C
import json, math, pathlib, struct, subprocess, sys, tempfile, wave
import numpy as np
from scipy.signal import sosfilt
ROOT=pathlib.Path(__file__).resolve().parents[1]
LIB=pathlib.Path(sys.argv[1]);CLI=pathlib.Path(sys.argv[2]);lib=C.CDLL(str(LIB))
class Band(C.Structure):
 _fields_=[(n,C.c_double) for n in ['threshold_db','ratio','knee_db','attack_ms','release_ms','makeup_db']]
class Config(C.Structure):
 _fields_=[('sample_rate',C.c_uint),('equalizer_enabled',C.c_int),('compressor_enabled',C.c_int),('limiter_enabled',C.c_int),('output_gain_db',C.c_double),('crossover_low_hz',C.c_double),('crossover_high_hz',C.c_double),('bands',Band*3),('limiter_ceiling_db',C.c_double),('limiter_release_ms',C.c_double),('lookahead_frames',C.c_uint),('measured_bass_enabled',C.c_int),('input_gain_db',C.c_double),('harmonics_enabled',C.c_int)]
class Meter(C.Structure):
 _fields_=[('gain_reduction_db',C.c_double*3),('limiter_gain_db',C.c_double),('peak_output',C.c_double),('input_frames',C.c_uint64),('invalid_samples',C.c_uint64)]
lib.cb_state_size.restype=C.c_size_t
lib.cb_default_config.argtypes=[C.POINTER(Config)]
lib.cb_init.argtypes=[C.c_void_p,C.POINTER(Config)];lib.cb_init.restype=C.c_int
lib.cb_process_f32.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p,C.c_size_t]
lib.cb_process_s32.argtypes=lib.cb_process_f32.argtypes
lib.cb_reset.argtypes=[C.c_void_p];lib.cb_get_meter.argtypes=[C.c_void_p,C.POINTER(Meter)]
lib.cb_latency_frames.argtypes=[C.c_void_p];lib.cb_latency_frames.restype=C.c_uint
lib.cb_compressor_curve.argtypes=[C.c_double,C.POINTER(Band)];lib.cb_compressor_curve.restype=C.c_double
rng=np.random.default_rng(6473)
def config(linear=True):
 c=Config();lib.cb_default_config(C.byref(c))
 if linear:c.limiter_enabled=0;c.output_gain_db=0
 return c
def state(c):
 s=C.create_string_buffer(lib.cb_state_size());assert lib.cb_init(s,C.byref(c))==0;return s
def process(s,x,blocks=None,inplace=False):
 x=np.ascontiguousarray(x,dtype=np.float32);y=x.copy() if inplace else np.empty_like(x);pos=0
 while pos<len(x):
  n=min(len(x)-pos,int(rng.integers(1,1000))) if blocks else len(x)-pos
  src=y[pos:] if inplace else x[pos:]
  lib.cb_process_f32(s,src.ctypes.data,y[pos:].ctypes.data,n);pos+=n
 return y
profile=json.loads((ROOT/'profiles/pujjoteen5.json').read_text());sos=np.array([s['b']+s['a'] for s in profile['sections']])
x=rng.uniform(-.5,.5,(150000,2)).astype('f4');x[:2048]=0;x[0,0]=1
expected=sosfilt(sos,x,axis=0).astype('f4');c=config();s=state(c);y=process(s,x)
np.testing.assert_allclose(y,expected,atol=1e-7,rtol=1e-6)
np.testing.assert_array_equal(process(state(c),x,blocks=True),y)
np.testing.assert_array_equal(process(state(c),x,inplace=True),y)
lib.cb_reset(s);np.testing.assert_array_equal(process(s,x),y)
mono=np.zeros((10000,2),dtype='f4');mono[0,0]=1
assert np.count_nonzero(process(state(c),mono)[:,1])==0
print('PASS: all recovered sections match independent SciPy SOS reference; block/in-place/reset/channel isolation')
# Configuration rejection is atomic; coefficients must never change on invalid input.
s=state(c);before=bytes(s)
for name,value in [('sample_rate',44100),('input_gain_db',float('nan')),('output_gain_db',float('nan')),('limiter_ceiling_db',1),('lookahead_frames',481),('crossover_high_hz',100)]:
 bad=Config.from_buffer_copy(bytes(c));setattr(bad,name,value);assert lib.cb_init(s,C.byref(bad))==-1 and bytes(s)==before
bad=Config.from_buffer_copy(bytes(c));bad.bands[0].ratio=0;assert lib.cb_init(s,C.byref(bad))==-1 and bytes(s)==before
# Signed extrema and no undefined negative shifts.
xi=np.array([[0,0],[2147483647,-2147483648],[-2147483648,2147483647]]*800,dtype='i4');yi=np.empty_like(xi)
lib.cb_process_s32(state(c),xi.ctypes.data,yi.ctypes.data,len(xi))
ref=np.clip(np.trunc(sosfilt(sos,xi.astype('f8')/2**31,axis=0)*2**31),-2**31,2**31-1).astype('i4')
assert np.max(np.abs(yi.astype('i8')-ref.astype('i8')))<=1
print('PASS: invalid configurations leave state unchanged; signed PCM extrema match reference')
# Inclusive-window sample limiter: all delays, transients, block edges and gain changes.
for delay in [0,1,144,480]:
 c=config(False);c.equalizer_enabled=0;c.output_gain_db=12;c.lookahead_frames=delay
 x=rng.uniform(-16,16,(60000,2)).astype('f4');x[::127]=0;x[::509]=[16,-16]
 y=process(state(c),x,blocks=True);assert np.max(np.abs(y))<=10**(-1/20)+1e-7
 np.testing.assert_array_equal(process(state(c),x),y)
c=config(False);c.equalizer_enabled=0;c.output_gain_db=0;s=state(c);delay=lib.cb_latency_frames(s)
x=np.zeros((delay+5,2),'f4');x[0]=[.1,.05];y=process(s,x)
assert np.all(y[:delay]==0);np.testing.assert_allclose(y[delay],x[0],atol=1e-8)
x=np.zeros((10000,2),'f4');x[0]=[float('nan'),float('inf')];x[500]=[float('-inf'),0]
s=state(c);assert np.all(np.isfinite(process(s,x)));meter=Meter();lib.cb_get_meter(s,C.byref(meter));assert meter.invalid_samples==3
print('PASS: limiter ceilings at 0/1/144/480-frame lookahead; exact delay; non-finite input containment')
# Hard/soft knee and stereo-linked multiband compressor.
b=Band(-18,4,0,10,150,0);assert abs(lib.cb_compressor_curve(-6,C.byref(b))+9)<1e-12
b.knee_db=6
for v in [-21,-15]:assert abs(lib.cb_compressor_curve(v-1e-7,C.byref(b))-lib.cb_compressor_curve(v+1e-7,C.byref(b)))<2e-7
for hz in [50,200,500,1000,2500,8000,16000]:
 c=config();c.equalizer_enabled=0;c.compressor_enabled=1
 for band in c.bands:band.ratio=1
 n=np.arange(96000);tone=.1*np.sin(2*np.pi*hz*n/48000);x=np.column_stack([tone,tone]).astype('f4');y=process(state(c),x)
 ratio=np.sqrt(np.mean(y[48000:,0]**2))/np.sqrt(np.mean(x[48000:,0]**2));assert abs(20*np.log10(ratio))<.005,(hz,ratio)
c=config();c.equalizer_enabled=0;c.compressor_enabled=1
n=np.arange(96000);tone=.9*np.sin(2*np.pi*1000*n/48000);x=np.column_stack([tone,.1*tone]).astype('f4');s=state(c);y=process(s,x)
np.testing.assert_allclose(y[:,1],.1*y[:,0],atol=2e-7);lib.cb_get_meter(s,C.byref(meter));assert max(meter.gain_reduction_db)>1
assert np.max(abs(y[48000:,0]))<np.max(abs(x[48000:,0]))
print('PASS: compressor curve, soft-knee continuity, unity LR4 reconstruction magnitude, stereo linkage and reduction')
# Offline WAV parser and actual command line: all formats and malformed files.
def wav_bytes(bits,tag=1,rate=48000,channels=2,extensible=False,odd_junk=False):
 x=rng.uniform(-.4,.4,(7000,channels))
 if tag==3:audio=x.astype('<f4').tobytes()
 elif bits==16:audio=(x*32768).astype('<i2').tobytes()
 elif bits==32:audio=(x*2**31).astype('<i4').tobytes()
 else:
  values=(x*2**23).astype('i4').ravel();audio=b''.join((int(v)&0xffffff).to_bytes(3,'little') for v in values)
 align=channels*bits//8;fmt=struct.pack('<HHIIHH',0xfffe if extensible else tag,channels,rate,rate*align,align,bits)
 if extensible:fmt+=struct.pack('<HHI',22,bits,3)+struct.pack('<I',tag)+bytes.fromhex('00001000800000aa00389b71')
 body=b'WAVE'+(b'JUNK'+struct.pack('<I',1)+b'x\0' if odd_junk else b'')+b'fmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',len(audio))+audio
 return b'RIFF'+struct.pack('<I',len(body))+body
with tempfile.TemporaryDirectory() as directory:
 d=pathlib.Path(directory)
 for bits,tag,ext in [(16,1,False),(24,1,False),(32,1,False),(32,3,False),(24,1,True),(32,3,True)]:
  inp=d/f'{bits}-{tag}-{ext}.wav';out=d/(inp.stem+'-out.wav');inp.write_bytes(wav_bytes(bits,tag,extensible=ext,odd_junk=True))
  run=subprocess.run([str(CLI),str(inp),str(out),'--linear'],capture_output=True);assert run.returncode==0,run.stderr
  b=out.read_bytes();assert b[20:24]==b'\x03\x00\x02\x00' and len(b)==56+7000*8
  assert np.all(np.isfinite(np.frombuffer(b[56:],'<f4')))
  size=len(b);run=subprocess.run([str(CLI),str(inp),str(out)],capture_output=True);assert run.returncode!=0 and len(out.read_bytes())==size
 inp=d/'good.wav';inp.write_bytes(wav_bytes(16));out=d/'good-out.wav';run=subprocess.run([str(CLI),str(inp),str(out),'--compress'],capture_output=True);assert run.returncode==0 and len(out.read_bytes())==56+(7000+144)*8
 for i,b in enumerate([b'',b'RIFF',wav_bytes(16,rate=44100),wav_bytes(16,channels=1),wav_bytes(16)[:-12],b'RIFF'+struct.pack('<I',40)+b'WAVEdata'+struct.pack('<I',0xffffffff)]):
  inp=d/f'bad{i}.wav';out=d/f'bad{i}-out.wav';inp.write_bytes(b);run=subprocess.run([str(CLI),str(inp),str(out)],capture_output=True);assert run.returncode!=0 and not out.exists(),(i,run.stderr)
print('PASS: WAV PCM16/24/32, IEEE float, extensible headers, odd chunks, tails, malformed/truncated files and overwrite refusal')
print('All independent DSP checks passed.')

# Measured bass cascade: independent SOS reference, streaming and limiter stress.
bass=json.loads((ROOT/'profiles/pujjoteen5-measured-bass.json').read_text())
bsos=np.array([v['b']+v['a'] for v in bass['sections']])
assert all(np.max(np.abs(np.roots(v[3:])))<1 for v in bsos)
c=config();c.measured_bass_enabled=1
x=(rng.normal(size=(96000,2))*.0005).astype('f4')
expected=sosfilt(bsos,sosfilt(sos,x,axis=0),axis=0).astype('f4')
y=process(state(c),x);np.testing.assert_allclose(y,expected,atol=2e-7,rtol=1e-5)
np.testing.assert_array_equal(y,process(state(c),x,blocks=[1,113,2048]))
c=config(False);c.measured_bass_enabled=1
x=(rng.normal(size=(96000,2))*2).astype('f4')
y=process(state(c),x);assert np.isfinite(y).all() and np.max(np.abs(y))<=10**(-1/20)+1e-7
print('PASS: measured bass stability, independent cascade, streaming and full-scale limiter stress')

# Measured preset configuration and a held-out volume, using observed tone levels.
lib.cb_measured_config.argtypes=[C.POINTER(Config),C.c_double]
c=config(False);before=bytes(c)
for invalid in [24,101,float('nan')]:
 assert lib.cb_measured_config(C.byref(c),invalid)==-1 and bytes(c)==before
observations=json.loads((ROOT/'profiles/pujjoteen5-tone-holdout.json').read_text());errors=[]
for q in observations:
 assert lib.cb_measured_config(C.byref(c),q['volume'])==0
 c.limiter_enabled=0
 tt=np.arange(24000)/48000;wave=np.sin(2*np.pi*q['frequency']*tt)*10**(q['input_dbfs']/20)
 xx=np.column_stack([wave,wave]).astype('f4');yy=process(state(c),xx)
 a=abs(np.sum(yy[-9600:,0]*np.exp(-2j*np.pi*q['frequency']*tt[-9600:])))*2/9600
 errors.append(20*np.log10(a)-q['output_fund_dbfs'])
assert np.sqrt(np.mean(np.square(errors)))<.35 and max(abs(np.array(errors)))<.6
for volume in [25,50,75,100]:
 assert lib.cb_measured_config(C.byref(c),volume)==0
 xx=(rng.normal(size=(16000,2))*2).astype('f4');yy=process(state(c),xx)
 assert np.isfinite(yy).all() and np.max(abs(yy))<=10**(-1/20)+1e-7
print('PASS: measured volume presets, held-out tone levels and limiter stress')

# The independent harmonic branch reproduces measured low-bass H3 approximately.
observations=json.loads((ROOT/'profiles/pujjoteen5-harmonic-validation.json').read_text())['rows']
for q in observations:
 if q['volume']!=50 or q['frequency']!=80 or q['input_dbfs'] not in [-48,-36,-24]:continue
 assert lib.cb_measured_config(C.byref(c),50)==0
 c.limiter_enabled=0
 tt=np.arange(48000)/48000;wave=np.sin(2*np.pi*80*tt)*10**(q['input_dbfs']/20)
 yy=process(state(c),np.column_stack([wave,wave]).astype('f4'))[-24000:,0]
 aa=[abs(np.sum(yy*np.exp(-2j*np.pi*80*k*tt[-24000:]))) for k in [1,3]]
 assert abs(20*np.log10(aa[1]/aa[0])-q['observed_h3_db'])<3
print('PASS: independent bass harmonic branch agrees with measured 80 Hz H3 within 3 dB')
