"""Inspect model residuals on validation, never on the final holdout."""
from p821_900_model import *
from p821_stateful_render import mix
torch.set_num_threads(1)
m=initial();m.load_state_dict(torch.load(ROOT/'refined-best.pt',weights_only=True)['state_dict']);wm=width_model();rows={}
for name in ['validation-0','validation-1','validation-2','validation-room','validation-history']:
 d=prepare(name,wm);prediction=inference(m,d);base=mix(d['base'].numpy(),d);y=d['y'];row={'model':metrics(y[SR:],prediction[SR:]),'base':metrics(y[SR:],base[SR:]),'input_rms':float(np.sqrt(np.mean(d['x']**2))),'input_peak':float(abs(d['x']).max()),'seconds':[]}
 for a in range(SR,len(y)-SR,SR):
  sl=slice(a,a+SR);power=np.mean(y[sl]**2)
  if power>1e-10:row['seconds'].append({'second':a/SR,'input_rms':float(np.sqrt(np.mean(d['x'][sl]**2))),'reference_peak':float(abs(y[sl]).max()),'model_peak':float(abs(prediction[sl]).max()),'residual_dbr':metrics(y[sl],prediction[sl])['residual_dbr']})
 row['bands']={}
 for lo,hi in [(20,150),(150,1000),(1000,5000),(5000,20000)]:
  sos=signal.butter(3,[lo,hi],btype='bandpass',fs=SR,output='sos');ref=signal.sosfilt(sos,y,axis=0);pred=signal.sosfilt(sos,prediction,axis=0);row['bands'][f'{lo}-{hi}']=metrics(ref[SR:],pred[SR:])
 rows[name]=row;print(name,{k:row[k] for k in ['model','base','input_rms','input_peak']},flush=True)
 if name=='validation-history':print(row['seconds'],flush=True)
store('diagnosis.json',rows)
