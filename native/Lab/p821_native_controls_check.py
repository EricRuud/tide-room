"""Export our trained controls and compare C++ against uninterrupted Python."""
import argparse,hashlib,importlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
import torch
from p821_identification_analysis import ROOT, metrics
from p821_select_continuous import MODULES
from p821_width_model import Model as WidthModel,features
from p821_width_separated_model import WIDTH_CHECKPOINT


def export(path,state,width):
    assert tuple(state['controller.weight_ih_l0'].shape)==(144,61),'This native export format supports the 61-input, 48-state controller only'
    with path.open('wb') as file:
        def write(value,dtype='<f4'):
            np.asarray(value.detach().numpy() if torch.is_tensor(value) else value,dtype=dtype).tofile(file)
        def gru(s,prefix):
            for key in ['weight_ih_l0','weight_hh_l0','bias_ih_l0','bias_hh_l0']:write(s[prefix+key])
        def dense(s,prefix):
            write(s[prefix+'weight']);write(s[prefix+'bias'])
        gru(state,'controller.');dense(state,'output.');dense(state,'memory_output.')
        if 'crest_output.weight' in state:dense(state,'crest_output.')
        else:write(np.zeros((5,48)));write(np.zeros(5))
        gru(width,'controller.');dense(width,'output.')
        for key in ['tau','growth','ceiling_tau']:write(torch.exp(state[key]),'<f8')
        write(torch.exp(width['tau']),'<f8')


def main(version,step,audio=False):
    torch.set_num_threads(1)
    extension=importlib.import_module(MODULES[version]);extension.activate()
    checkpoint_path=ROOT/f'surrogate-{version}-{step}.pt';checkpoint=torch.load(checkpoint_path,weights_only=True)
    model=extension.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    width_checkpoint=torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True);width=WidthModel();width.load_state_dict(width_checkpoint['state_dict']);width.eval()
    destination=ROOT/f'native-controls-{version}-{step}.bin';export(destination,checkpoint['state_dict'],width_checkpoint['state_dict'])
    rows=[]
    for name in ['music-levels','history-plus']:
        d=extension.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        wf=features(d['x'].astype(np.float64))
        with torch.no_grad():
            values,cap=model.control_parts(d['control'].transpose(0,1));delta=width(wf[None])[0]
        expected=np.column_stack([values.transpose(0,1).numpy().reshape(-1,10),cap.T.numpy(),delta.numpy()])
        source=np.column_stack([d['control'].numpy().reshape(-1,122),wf.numpy()]).astype('<f4')
        with tempfile.TemporaryDirectory(prefix='p821-native-controls-',dir='/private/tmp') as folder:
            folder=Path(folder);source.tofile(folder/'input.bin')
            cmd=['/private/tmp/tide-p821-control-bench',str(destination),str(folder/'input.bin'),str(folder/'output.bin')]
            if audio:
                from p821_width_identify import FOCUS_FULL_DB,BASE_DIFFERENCE_DB
                parameters=np.r_[model.frequency.exp().detach().numpy(),model.q.exp().detach().numpy(),
                                 float(model.release.exp().detach()),d['w'].reshape(-1),FOCUS_FULL_DB/BASE_DIFFERENCE_DB]
                parameters.astype('<f8').tofile(folder/'audio-parameters.bin');d['base'].numpy().astype('<f4').tofile(folder/'base.bin')
                cmd += [str(folder/'audio-parameters.bin'),str(folder/'base.bin'),str(folder/'audio-output.bin')]
            benchmark=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,13)
            error=np.max(abs(actual-expected),axis=0)
            assert np.isfinite(actual).all() and max(error)<2e-4,error
            subprocess.check_output(cmd,text=True);assert np.array_equal(actual,np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,13))
            audio_metrics=None
            if audio:
                expected_audio=extension.core.predict(model,d);native_audio=np.fromfile(folder/'audio-output.bin',dtype='<f8').reshape(-1,2)[:d['length']]
                audio_metrics=metrics(expected_audio,native_audio)
                assert np.isfinite(native_audio).all() and audio_metrics['peak_error']<2e-5,audio_metrics
        row=dict(dataset=name,maximum_control_error_db=float(max(error[:10])),maximum_ceiling_error=float(max(error[10:12])),
                 maximum_width_delta_error_db=float(error[12]),benchmark=benchmark,reset_repeat_exact=True,audio_port_comparison=audio_metrics)
        rows.append(row);print(row,flush=True)
    report=dict(version=version,step=checkpoint['step'],checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),
                binary_weights_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                header_sha256=hashlib.sha256(Path(__file__).with_name('P821ControlModel.h').read_bytes()).hexdigest(),
                benchmark_scope=('controllers and native-rate channel path; excludes feature extraction, quiet baseline/gentle gain and HQ resampling' if audio else 'controllers only, excludes feature extraction and audio path'),final_holdouts_used=False,rows=rows)
    suffix='-audio' if audio else ''
    (ROOT/f'native-controls-{version}-{step}{suffix}-check.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));parser.add_argument('--step',default='continuous-selected')
    parser.add_argument('--audio',action='store_true');args=parser.parse_args();main(args.version,args.step,args.audio)
