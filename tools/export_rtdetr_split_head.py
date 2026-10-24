#!/usr/bin/env python3
"""Export the existing RT-DETR checkpoint with separate box/score outputs."""

import argparse
from pathlib import Path
from types import MethodType

import torch
import torch.nn as nn
import torch.nn.functional as F
from ultralytics import RTDETR


def custom_grid_sample(
    input,
    grid,
    mode="bilinear",
    padding_mode="zeros",
    align_corners=False,
):
    """Static GridSample replacement required by the existing RKNN pipeline."""
    n, channels, height_in, width_in = input.shape
    _, height_out, width_out, _ = grid.shape

    ix = grid[..., 0]
    iy = grid[..., 1]
    if align_corners:
        ix = ((ix + 1) / 2) * (width_in - 1)
        iy = ((iy + 1) / 2) * (height_in - 1)
    else:
        ix = ((ix + 1) * width_in - 1) / 2
        iy = ((iy + 1) * height_in - 1) / 2

    ix_nw = torch.floor(ix)
    iy_nw = torch.floor(iy)
    ix_ne = ix_nw + 1
    iy_sw = iy_nw + 1
    ix_se = ix_ne
    iy_se = iy_sw

    nw_weight = (ix_ne - ix) * (iy_sw - iy)
    ne_weight = (ix - ix_nw) * (iy_sw - iy)
    sw_weight = (ix_ne - ix) * (iy - iy_nw)
    se_weight = (ix - ix_nw) * (iy - iy_nw)

    ix_nw = torch.clamp(ix_nw, 0, width_in - 1)
    iy_nw = torch.clamp(iy_nw, 0, height_in - 1)
    ix_ne = torch.clamp(ix_ne, 0, width_in - 1)
    iy_sw = torch.clamp(iy_sw, 0, height_in - 1)
    ix_se = torch.clamp(ix_se, 0, width_in - 1)
    iy_se = torch.clamp(iy_se, 0, height_in - 1)

    idx_nw = (iy_nw * width_in + ix_nw).reshape(n, -1)
    idx_ne = (iy_nw * width_in + ix_ne).reshape(n, -1)
    idx_sw = (iy_sw * width_in + ix_nw).reshape(n, -1)
    idx_se = (iy_sw * width_in + ix_se).reshape(n, -1)

    flat_input = input.reshape(n, channels, -1)
    idx_nw = idx_nw.unsqueeze(1).expand(-1, channels, -1).long()
    idx_ne = idx_ne.unsqueeze(1).expand(-1, channels, -1).long()
    idx_sw = idx_sw.unsqueeze(1).expand(-1, channels, -1).long()
    idx_se = idx_se.unsqueeze(1).expand(-1, channels, -1).long()

    nw_val = torch.gather(flat_input, 2, idx_nw).view(n, channels, height_out, width_out)
    ne_val = torch.gather(flat_input, 2, idx_ne).view(n, channels, height_out, width_out)
    sw_val = torch.gather(flat_input, 2, idx_sw).view(n, channels, height_out, width_out)
    se_val = torch.gather(flat_input, 2, idx_se).view(n, channels, height_out, width_out)

    nw_weight = nw_weight.unsqueeze(1)
    ne_weight = ne_weight.unsqueeze(1)
    sw_weight = sw_weight.unsqueeze(1)
    se_weight = se_weight.unsqueeze(1)
    return (
        nw_val * nw_weight
        + ne_val * ne_weight
        + sw_val * sw_weight
        + se_val * se_weight
    )


def patch_linear_forward(module: nn.Linear, inputs):
    """Flatten non-2D Linear inputs so the ONNX graph has static dimensions."""
    if inputs.dim() <= 2:
        return F.linear(inputs, module.weight, module.bias)
    original_shape = inputs.shape
    flatten_dim = 1
    for size in original_shape[:-1]:
        flatten_dim *= size
    flattened = inputs.reshape(flatten_dim, original_shape[-1])
    output = F.linear(flattened, module.weight, module.bias)
    return output.reshape(*original_shape[:-1], module.out_features)


def split_head_forward(self, features, batch=None):
    """Run the decoder and return box/score tensors directly, without final concat."""
    from ultralytics.models.utils.ops import get_cdn_group

    feats, shapes = self._get_encoder_input(features)
    dn_embed, dn_bbox, attn_mask, _ = get_cdn_group(
        batch,
        self.nc,
        self.num_queries,
        self.denoising_class_embed.weight,
        self.num_denoising,
        self.label_noise_ratio,
        self.box_noise_scale,
        self.training,
    )
    embed, refer_bbox, _, _ = self._get_decoder_input(
        feats, shapes, dn_embed, dn_bbox
    )
    dec_bboxes, dec_scores = self.decoder(
        embed,
        refer_bbox,
        feats,
        shapes,
        self.dec_bbox_head,
        self.dec_score_head,
        self.query_pos_head,
        attn_mask=attn_mask,
    )
    return dec_bboxes, dec_scores.sigmoid()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--opset", type=int, default=12)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    F.grid_sample = custom_grid_sample
    nn.Linear.forward = patch_linear_forward

    model = RTDETR(str(args.pt))
    torch_model = model.model.cpu().eval()
    head = torch_model.model[-1]
    head.forward = MethodType(split_head_forward, head)
    head.export = True
    head.format = "onnx"

    dummy_input = torch.randn(1, 3, 640, 640)
    print(f"exporting {args.pt} -> {args.output}", flush=True)
    torch.onnx.export(
        torch_model,
        dummy_input,
        str(args.output),
        opset_version=args.opset,
        do_constant_folding=True,
        input_names=["images"],
        output_names=["pred_boxes", "pred_logits"],
    )
    print(f"exported {args.output}", flush=True)
    if args.verify:
        import numpy as np
        import onnxruntime as ort

        with torch.no_grad():
            reference = torch_model(dummy_input)
        session = ort.InferenceSession(
            str(args.output), providers=["CPUExecutionProvider"]
        )
        actual = session.run(
            None,
            {"images": dummy_input.numpy().astype(np.float32)},
        )
        expected_names = ["pred_boxes", "pred_logits"]
        expected_shapes = [(1, 300, 4), (1, 300, 10)]
        if [item.name for item in session.get_outputs()] != expected_names:
            raise RuntimeError("unexpected ONNX output names or order")
        for name, expected, observed, shape in zip(
            expected_names, reference, actual, expected_shapes
        ):
            expected_array = expected.detach().cpu().numpy()
            if tuple(observed.shape) != shape:
                raise RuntimeError(f"{name}: shape {observed.shape} != {shape}")
            max_abs = float(np.max(np.abs(expected_array - observed)))
            print(
                f"verify {name}: shape={observed.shape} max_abs={max_abs:.7g}",
                flush=True,
            )
            if not np.allclose(expected_array, observed, rtol=1e-4, atol=1e-4):
                raise RuntimeError(f"{name}: ONNX output mismatch")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
