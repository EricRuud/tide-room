"""Measure reference components that an odd-symmetric model cannot reproduce.

Global polarity inversion preserves the model's level descriptors. Matched
reference renders separate its odd response and polarity-even contribution.
This is a structural diagnostic, not permission to use target audio at inference.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,metrics,db


def main():
    x=read('input-music-levels');submit('music-levels-inverted',save('music-levels-inverted',-x));plus=read('music-levels');minus=read('music-levels-inverted');odd=(plus-minus)*.5;even=(plus+minus)*.5;rows=[]
    for c in json.loads((ROOT/'input-music-levels.json').read_text()):
        a,b=c['start'],c['end'];row=c|dict(odd_only=metrics(plus[a:b],odd[a:b]),even_rms_dbfs=db(np.sqrt(np.mean(even[a:b]**2))),even_peak=float(np.max(abs(even[a:b]))));rows.append(row);print(row,flush=True)
    (ROOT/'polarity-music-audit.json').write_text(json.dumps(dict(target_decomposition=True,rows=rows),indent=2))


if __name__=='__main__':main()
