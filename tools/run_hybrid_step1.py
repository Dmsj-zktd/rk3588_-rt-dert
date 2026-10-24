#!/usr/bin/env python3
"""Generate RKNN hybrid-quantization calibration data from a static ONNX model."""

import argparse
from pathlib import Path

from rknn.api import RKNN


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnx", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    rknn = RKNN(verbose=args.verbose)
    config = {
        "mean_values": [[0, 0, 0]],
        "std_values": [[255, 255, 255]],
        "target_platform": "rk3588",
        "optimization_level": 0,
        "disable_rules": ["fuse_mul_into_gemm"],
    }
    print(f"config={config}", flush=True)
    ret = rknn.config(**config)
    print(f"config_ret={ret}", flush=True)
    if ret != 0:
        rknn.release()
        return ret

    ret = rknn.load_onnx(model=str(args.onnx))
    print(f"load_ret={ret}", flush=True)
    if ret != 0:
        rknn.release()
        return ret

    ret = rknn.hybrid_quantization_step1(dataset=str(args.dataset))
    print(f"step1_ret={ret}", flush=True)
    rknn.release()
    return ret


if __name__ == "__main__":
    raise SystemExit(main())
