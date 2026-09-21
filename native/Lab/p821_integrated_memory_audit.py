"""Standalone limiter anti-aliasing benchmark, not a P821 waveform comparison."""
import json,time
import numpy as np
from p821_memory_limiter import render as ordinary
from p821_integrated_memory import render as integrated
from p821_identification_analysis import ROOT,SR,db,metrics


def main():
    t=np.arange(SR)/SR;x=(1.4*np.sin(2*np.pi*17000*t))[:,None]*[1,1];cap=np.full_like(x,.795);a=SR//4;b=3*SR//4
    started=time.monotonic();reference=ordinary(x,cap,.0046,64);report=dict(standalone_limiter=True,P821_not_used=True,reference_factor=64,reference_seconds=time.monotonic()-started,rows=[])
    for method,factors in [('ordinary',[1,2,4,8,16,32]),('integrated',[2,4,8,16]),('integrated_compensated',[2,4,8,16])]:
        for factor in factors:
            started=time.monotonic();y=ordinary(x,cap,.0046,factor) if method=='ordinary' else integrated(x,cap,.0046,factor,compensated=method=='integrated_compensated');seconds=time.monotonic()-started;Y=np.fft.rfft(y[a:b,0]);bin=lambda f:round(f*(b-a)/SR);fund=abs(Y[bin(17000)]);frequencies=np.array([f for f in range(1000,24000,1000) if f!=17000]);amplitudes=np.array([abs(Y[bin(f)]) for f in frequencies]);k=np.argmax(amplitudes)
            row=dict(method=method,factor=factor,seconds=seconds,other_khz_bins_max_dbr=db(amplitudes[k]/fund),other_khz_bins_max_hz=int(frequencies[k]),against_64x=metrics(reference[a:b],y[a:b]),fundamental_dbfs=db(2*fund/(b-a)));report['rows'].append(row);print(method,factor,round(row['other_khz_bins_max_dbr'],2),'error vs 64x',round(row['against_64x']['residual_dbr'],2),'sec',round(seconds,3),flush=True)
    (ROOT/'integrated-memory-audit.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
