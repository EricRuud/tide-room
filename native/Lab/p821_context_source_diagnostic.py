"""Attribute training-context mismatch to GRU state or downstream state."""
import importlib,json
import numpy as np
import torch
from p821_identification_analysis import ROOT,metrics
from p821_select_continuous import MODULES
from p821_stateful_render import mix

@torch.no_grad()
def main():
    torch.set_num_threads(1);rows=[]
    for version in ['v27','v31']:
        extension=importlib.import_module(MODULES[version]);extension.activate();model=extension.Model()
        model.load_state_dict(torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True)['state_dict']);model.eval()
        d=extension.prepare_source(json.loads((ROOT/'history-plus.json').read_text())['job']['input']);full=extension.core.predict(model,d)
        control=d['control'].transpose(0,1);encoded,_=model.controller(control);values,cap=model.control_parts(control)
        gru=model.controller.forward;control_parts=model.control_parts
        for position in [217344,888832,1560320,2231808]:
            a=position-24576;b=position+8192;ka=a//256;kb=b//256+1;c=control[:,ka:kb];x=d['base'][a:b].T
            subset=dict(w=d['w'],length=8192,extra_width_gain=d['extra_width_gain'][position:b]);row=dict(version=version,start=position,modes={})
            for mode in ['reset','seed-gru','seed-all-controls']:
                if mode=='seed-gru':model.controller.forward=lambda inputs:gru(inputs,encoded[:,ka-1].unsqueeze(0).contiguous())
                elif mode=='seed-all-controls':model.control_parts=lambda inputs:(values[:,ka:kb],cap[:,ka:kb])
                output=mix(model(x,c).T.numpy()[24576:],subset)
                row['modes'][mode]=metrics(full[position:b],output);model.controller.forward=gru;model.control_parts=control_parts
            rows.append(row);print(version,position,{mode:round(result['residual_dbr'],3) for mode,result in row['modes'].items()},flush=True)
        del d,full
    (ROOT/'context-source-diagnostic.json').write_text(json.dumps(dict(scope='Model self-consistency attribution; seeded states are diagnostic only',final_holdouts_used=False,rows=rows),indent=2))

if __name__=='__main__':main()
