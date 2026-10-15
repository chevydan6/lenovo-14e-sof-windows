"""Fit independent RBJ filters to a local PCM16 EchoRef measurement.
Capture files are private research inputs and are not distributed.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy import signal,optimize
ROOT=Path(__file__).resolve().parents[1]
def peak(f,g,q):
 a=10**(g/40);w=2*np.pi*f/48000;alpha=np.sin(w)/(2*q);c=np.cos(w)
 b=np.array([1+alpha*a,-2*c,1-alpha*a]);d=np.array([1+alpha/a,-2*c,1-alpha/a]);return np.r_[b/d[0],d/d[0]]
def run(capture):
 x=np.fromfile(capture/'test.raw','<i2').reshape(-1,2)[:,0]/32768
 r,y=__import__('scipy.io.wavfile',fromlist=['read']).read(capture/'echo.wav');assert r==48000 and y.dtype==np.int16
 y=y[:,0]/32768;c=signal.correlate(y,x,method='fft');lag=int(signal.correlation_lags(len(y),len(x))[np.argmax(abs(c))])
 curves=[]
 for k in [2,3]:
  start=(1+3*k)*48000+24000;end=start+96000;a=x[start:end];b=y[start+lag:end+lag]
  f,xx=signal.welch(a,48000,nperseg=8192);_,xy=signal.csd(a,b,48000,nperseg=8192);_,coh=signal.coherence(a,b,48000,nperseg=8192)
  curves.append((20*np.log10(np.maximum(abs(xy/xx),1e-15)),coh))
 base=json.loads((ROOT/'profiles/pujjoteen5.json').read_text());sos=np.array([s['b']+s['a'] for s in base['sections']])
 grid=np.geomspace(90,1100,180);target=np.interp(grid,f,curves[1][0]);_,h=signal.sosfreqz(sos,worN=grid,fs=48000);baseline=20*np.log10(abs(h))-9
 def response(v,fg):
  ss=np.array([peak(*v[i:i+3]) for i in range(0,len(v),3)]);_,h=signal.sosfreqz(ss,worN=fg,fs=48000);return 20*np.log10(np.maximum(abs(h),1e-15))
 def residual(v):
  # Keep correction close to unity above the fitted bass region.
  return np.r_[response(v,grid)+baseline-target,.5*response(v,np.geomspace(2000,18000,35))]
 v=[170,25,.8,300,-8,1,650,-8,.8,100,5,1]
 lo=[70,-36,.2]*4;hi=[1400,36,6]*4
 fit=optimize.least_squares(residual,v,bounds=(lo,hi),max_nfev=3000,xtol=1e-12,ftol=1e-12,gtol=1e-12)
 coeff=[peak(*fit.x[i:i+3]).tolist() for i in range(0,12,3)]
 model=response(fit.x,grid)+baseline
 errors={}
 for name,(db,coh) in zip(['holdout_minus40','training_minus30'],curves):
  err=model-np.interp(grid,f,db);errors[name]={'rms_db':float(np.sqrt(np.mean(err**2))),'max_abs_db':float(max(abs(err))),'median_coherence':float(np.median(np.interp(grid,f,coh)))}
 out={'name':'Pujjoteen5 measured bass preview','sample_rate':48000,'capture_volume_percent':50,'fit_range_hz':[90,1100],'base_output_gain_db':-9,'status':'Linear magnitude approximation at measured levels; not a speaker protection model or complete MaxxBass clone','parameters':[dict(zip(['frequency_hz','gain_db','q'],map(float,fit.x[i:i+3]))) for i in range(0,12,3)],'sections':[{'b':s[:3],'a':s[3:]} for s in coeff],'validation':errors}
 (ROOT/'profiles/pujjoteen5-measured-bass.json').write_text(json.dumps(out,indent=2)+'\n')
 (ROOT/'src/measured_bass.h').write_text('/* Independently fitted numeric calibration. MIT. */\nstatic const double measured_bass_sos[4][5] = {\n'+''.join('    {'+', '.join(float(z).hex() for z in s[:3]+s[4:])+'},\n' for s in coeff)+'};\n')
 print(json.dumps(out,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('capture',type=Path);run(p.parse_args().capture)
