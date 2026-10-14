"""Evaluate exported production-domain JPEG patches without claiming ground truth."""
import argparse
import json
from pathlib import Path
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory",type=Path)
parser.add_argument("--opencv-path",type=Path)
args=parser.parse_args()
if args.opencv_path:sys.path.insert(0,str(args.opencv_path.resolve()))
import cv2
import numpy as np
from alignment import align_patch,guarded_ecc_fit

cv2.setNumThreads(1)
data=json.loads((args.directory/"production.json").read_text())
for patch in data["patches"]:
    reference=cv2.imread(str(args.directory/(patch["id"]+"-reference.pgm")),cv2.IMREAD_GRAYSCALE)
    source=cv2.imread(str(args.directory/(patch["id"]+"-source.pgm")),cv2.IMREAD_GRAYSCALE)
    for name,fit in (("block_lk",align_patch),("guarded_ecc",guarded_ecc_fit)):
        result=fit(reference,source)
        patch[name]=result.record()
        if result.accepted and patch["production_offset_usable"]:
            patch[name]["production_disagreement"] = float(np.hypot(result.dx-patch["production_dx"],
                                                                    result.dy-patch["production_dy"]))
summary=dict(patch_count=len(data["patches"]),ground_truth=False,
             production_usable=sum(p["production_offset_usable"] for p in data["patches"]),
             production_detail_centres=sum(p["production_detail_at_centre"] for p in data["patches"]))
for method in ("block_lk","guarded_ecc"):
    accepted=[p[method] for p in data["patches"] if p[method]["accepted"]]
    disagreements=[p["production_disagreement"] for p in accepted if "production_disagreement" in p]
    reasons={}
    for p in data["patches"]:
        reason=p[method]["reason"];reasons[reason]=reasons.get(reason,0)+1
    summary[method]=dict(accepted=len(accepted),reasons=reasons,
        median_production_disagreement=float(np.median(disagreements)) if disagreements else None)
data["summary"]=summary
(args.directory/"comparison.json").write_text(json.dumps(data,indent=2,allow_nan=False)+"\n",encoding="utf-8")
print(json.dumps(summary,indent=2))
