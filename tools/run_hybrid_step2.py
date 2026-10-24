#!/usr/bin/env python3
"""Run RKNN hybrid quantization step 2 from an existing cfg/data pair."""

import argparse
from pathlib import Path

from rknn.api import RKNN


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnx", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--cfg", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rknn = RKNN(verbose=True)
    ret = rknn.config(
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
        target_platform="rk3588",
        optimization_level=0,
        disable_rules=["fuse_mul_into_gemm"],
    )
    print(f"config_ret={ret}", flush=True)
    ret = rknn.hybrid_quantization_step2(
        model_input=str(args.onnx),
        data_input=str(args.data),
        model_quantization_cfg=str(args.cfg),
    )
    print(f"step2_ret={ret}", flush=True)
    if ret != 0:
        rknn.release()
        return ret

    ret = rknn.export_rknn(str(args.output))
    print(f"export_ret={ret} output={args.output}", flush=True)
    rknn.release()
    return ret


if __name__ == "__main__":
    raise SystemExit(main())
