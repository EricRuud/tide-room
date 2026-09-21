"""Describe driven filter center/Q changes; target-informed, not an emulator.

Two peak sections and an overall gain fit each local operating response. The
carrier neighborhood is excluded. Weak near-zero sections have unidentified Q.
"""
import json
import numpy as np
from scipy import optimize
from p821_identification_analysis import ROOT,SR,db
from p821_operating_fit import response


def main():
    d=np.load(ROOT/'quiet-operating-responses.npz');r=(d['small']+d['large'])*.5;cases=json.loads((ROOT/'quiet-operating-cases.json').read_text());frequency=np.fft.rfftfreq(r.shape[1],1/SR);ratios=np.fft.rfft(r,axis=1)/np.fft.rfft(r[0]);hz=np.geomspace(800,22500,400);hz=hz[abs(hz-6000)>1100];rows=[]
    for i,c in enumerate(cases[1:],1):
        target=np.interp(hz,frequency,ratios[i].real)+1j*np.interp(hz,frequency,ratios[i].imag)
        def error(p):
            e=(response(p,hz)-target)/abs(target);return np.concatenate([e.real,e.imag])
        best=None
        for q in [.5,1.]:
            initial=[-.5,np.log(2600),np.log(q),-1,np.log(10000),np.log(q),-.5]
            lower=[-5,np.log(1800),np.log(.2),-24,np.log(6000),np.log(.2),-24]
            upper=[5,np.log(3500),np.log(4),2,np.log(16000),np.log(4),2]
            fit=optimize.least_squares(error,initial,bounds=(lower,upper),max_nfev=1500,ftol=1e-12,xtol=1e-12,gtol=1e-12)
            if best is None or fit.cost<best.cost:best=fit
        row=dict(level=c['level'],gain_db=float(best.x[0]),descriptor_relative_complex_error_dbr=db(np.sqrt(np.mean(error(best.x)**2)*2)),filters=[dict(frequency=float(np.exp(f)),q=float(np.exp(q)),gain_db=float(g)) for f,q,g in best.x[1:].reshape(-1,3)]);rows.append(row);print(row,flush=True)
    (ROOT/'operating-family-description.json').write_text(json.dumps(dict(target_informed=True,rows=rows),indent=2))


if __name__=='__main__':main()
