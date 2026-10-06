#!/usr/bin/env python3
"""Evaluate a threshold profile's operating point from its (already thresholded) dump.

All rows in the dump are counted (they already passed the class-wise thresholds),
so the pooled P/R/F1 and the macro (class-balanced) F1 are the deployed metrics.
"""
import os, sys, argparse
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import visdrone_map as M


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--det", required=True)
    ap.add_argument("--ann", required=True)
    ap.add_argument("--iou", type=float, default=0.5)
    a = ap.parse_args()

    dets = M.load_dets(a.det)
    stems = sorted(dets.keys())
    gt, _ = M.load_gt(a.ann, stems)
    npos = sum(len(gt[s][c]) for s in gt for c in gt[s])

    tp = fp = 0
    ndet = 0
    per = []
    for c in range(10):
        t = f = 0
        for stem, dl in dets.items():
            cand = sorted([(s, b) for (cc, s, b) in dl if cc == c], key=lambda r: -r[0])
            gts = gt.get(stem, {}).get(c, [])
            matched = set()
            for s, b in cand:
                best, bj = 0.0, -1
                for j, g in enumerate(gts):
                    if j in matched:
                        continue
                    v = M.iou(b, g)
                    if v > best:
                        best, bj = v, j
                if bj >= 0 and best >= a.iou:
                    matched.add(bj); t += 1
                else:
                    f += 1
        n = sum(len(gt[s].get(c, [])) for s in gt)
        p = t / (t + f) if t + f else 0.0
        r = t / n if n else 0.0
        f1 = 2 * p * r / (p + r) if (p + r) else 0.0
        per.append((n, t, f, p, r, f1)); tp += t; fp += f

    ndet = tp + fp
    P = tp / ndet if ndet else 0.0
    R = tp / npos if npos else 0.0
    F1 = 2 * P * R / (P + R) if (P + R) else 0.0
    macro = sum(x[5] for x in per) / 10.0
    print("dump=%s rows=%d images=%d" % (os.path.basename(a.det), ndet, len(dets)))
    print("%-16s %5s %6s %6s %7s %7s %7s" % ("class", "GT", "TP", "FP", "P", "R", "F1"))
    for c in range(10):
        n, t, f, p, r, f1 = per[c]
        print("%-16s %5d %6d %6d %7.4f %7.4f %7.4f" % (M.CLASSES[c], n, t, f, p, r, f1))
    print("POOLED  TP=%d FP=%d FN=%d  P=%.4f R=%.4f F1=%.4f  | MACRO F1=%.4f"
          % (tp, fp, npos - tp, P, R, F1, macro))


if __name__ == "__main__":
    main()
