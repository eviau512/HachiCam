"""Same 321-pixel inputs/support; proposed fits must survive native detail gates.

Offline feasibility assessment, not an application replacement or flow truth.
"""
import argparse, csv, json, sys, time
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory',type=Path)
parser.add_argument('--opencv-path',type=Path)
parser.add_argument('--summarize',action='store_true')
args=parser.parse_args()
if args.opencv_path:sys.path.insert(0,str(args.opencv_path.resolve()))
import cv2
import numpy as np
from alignment import Config, Result, _fit, _assess, _hessian
cv2.setNumThreads(1)
root=args.directory
if args.summarize:
    data=json.loads((root/'proposals.json').read_text())
    post={(r['id'],r['method']):r for r in csv.DictReader((root/'post-gates.csv').open())}
    summary={}
    for method in ('shared_production','guarded_production_ecc','block_lk'):
        entries=[r[method] for r in data['records']]
        complete=0
        for row in data['records']:
            values=post.get((row['id'],method))
            row[method]['native_post_gate']=values
            if row[method]['accepted'] and values and values['detail']=='1':complete+=1
        proposed=[r for r in entries if r['accepted']]
        timings=[r['milliseconds'] for r in entries if 'milliseconds' in r]
        summary[method]={'proposed':len(proposed),'post_detail_gate':complete,'total':len(entries),
                         'median_ms':float(np.median(timings)) if timings else None,
                         'p95_ms':float(np.percentile(timings,95)) if timings else None,
                         'rejections':{reason:sum(r['reason']==reason and not r['accepted'] for r in entries)
                                       for reason in sorted({r['reason'] for r in entries if not r['accepted']})}}
    data['summary']=summary
    data['original_production_detail']=sum(r['original_detail'] for r in data['records'])
    data['mean_shared_mask_fraction']=float(np.mean([r['common_fraction'] for r in data['records']]))
    (root/'assessment.json').write_text(json.dumps(data,indent=2,allow_nan=False),encoding='utf-8')
    print(json.dumps({k:v for k,v in data.items() if k!='records'},indent=2))
    raise SystemExit(0)

cfg=Config()
def guard(reference,source,mask,dx,dy):
    valid=mask.copy()
    margin=cfg.max_shift+4
    valid[:margin]=valid[-margin:]=False
    valid[:,:margin]=valid[:,-margin:]=False
    if valid.sum()<cfg.min_samples:return Result(reason='insufficient_support')
    r=cv2.GaussianBlur(reference.astype(np.float32),(0,0),cfg.sigma)
    s=cv2.GaussianBlur(source.astype(np.float32),(0,0),cfg.sigma)
    gy,gx=np.gradient(r)
    _,emin,eratio=_hessian(gx[valid],gy[valid])
    result=Result(dx=dx,dy=dy,eigen_min=emin,eigen_ratio=eratio)
    if emin<cfg.min_eigenvalue or eratio<cfg.min_eigen_ratio:
        result.reason='flat_or_aperture';return result
    return _assess(r,s,valid,mask,cfg,np.array([dx,dy]),result)

data=json.loads((root/'production.json').read_text())
records=[]
with (root/'fits.txt').open('w') as fits:
    for entry in data['patches']:
        key=entry['id']
        r=cv2.imread(str(root/f'{key}-reference.pgm'),cv2.IMREAD_GRAYSCALE)
        s=cv2.imread(str(root/f'{key}-source.pgm'),cv2.IMREAD_GRAYSCALE)
        common=cv2.imread(str(root/f'{key}-mask.pgm'),cv2.IMREAD_GRAYSCALE)>0
        assert r.shape==(321,321) and s.shape==r.shape
        record=dict(entry)
        record['shared_production']={'accepted':entry['shared_production_detail'],
            'reason':'native_detail' if entry['shared_production_detail'] else 'native_rejected',
            'dx':entry['shared_production_dx'],'dy':entry['shared_production_dy']}
        started=time.perf_counter()
        guarded=Result(reason='native_rejected')
        if entry['shared_production_detail']:
            guarded=guard(r,s,common,entry['shared_production_dx'],entry['shared_production_dy'])
            if guarded.accepted:
                backward=guard(s,r,common,entry['reverse_dx'],entry['reverse_dy']) if entry['reverse_usable'] else Result()
                guarded.fb_error=float(np.hypot(guarded.dx+backward.dx,guarded.dy+backward.dy))
                if not backward.accepted or guarded.fb_error>cfg.max_fb_error:
                    guarded.accepted=False;guarded.reason='forward_reverse'
        record['guarded_production_ecc']=guarded.record()
        record['guarded_production_ecc']['milliseconds']=1000*(time.perf_counter()-started)
        started=time.perf_counter()
        forward=_fit(r,s,common,common,cfg)
        if forward.accepted:
            backward=_fit(s,r,common,common,cfg)
            forward.fb_error=float(np.hypot(forward.dx+backward.dx,forward.dy+backward.dy))
            if not backward.accepted or forward.fb_error>cfg.max_fb_error:
                forward.accepted=False;forward.reason='forward_reverse'
        record['block_lk']=forward.record()
        record['block_lk']['milliseconds']=1000*(time.perf_counter()-started)
        for method in ('shared_production','guarded_production_ecc','block_lk'):
            if record[method]['accepted']:
                fit=record[method];fits.write(f"{key} {method} {fit['dx']:.10g} {fit['dy']:.10g}\n")
        records.append(record)
payload={'scope':'matched_node_proposals_and_native_post_gates_not_end_to_end',
         'production_source':'5e15ffb','patch_size':321,'config':cfg.__dict__,
         'runtime':{'python_opencv':cv2.__version__,'numpy':np.__version__,'native_opencv':'5.0'},
         'shared_mask':'intersection 8<gray<247; 9x9 erosion for sigma=1.2, before any method',
         'records':records}
(root/'proposals.json').write_text(json.dumps(payload,indent=2,allow_nan=False),encoding='utf-8')
print('Generated 27 matched proposals; native assessment still required.')
