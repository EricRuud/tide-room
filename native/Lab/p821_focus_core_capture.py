"""Additional training Focus pairs needed to separate channel dynamics/width."""
import json
import numpy as np
from pathlib import Path
from p821_identify import ROOT,save,submit
from p821_identification_analysis import read
from p821_bandlimited_dictionary import raw_input
from p821_width_identify import trajectory,BASE_DIFFERENCE_DB


def main():
    for name in ['envelope-plus','saturation-map','crest-history-plus','crest-history-minus']:
        source=Path(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        if not source.exists():source=save(name,raw_input(source))
        zero='focus-zero-'+name;submit(zero,source,{'Stage Focus':0})
        values,weights,row,_=trajectory(read(name),read(zero))
        np.savez_compressed(ROOT/f'width-labels-{name}.npz',focus_delta_db=values-BASE_DIFFERENCE_DB,side_energy=weights)
        (ROOT/f'width-factorization-{name}.json').write_text(json.dumps(row,indent=2));print(name,row,flush=True)


if __name__=='__main__':main()
