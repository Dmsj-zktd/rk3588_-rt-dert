#!/usr/bin/env python3
"""Dump VisDrone detections from the original RT-DETR checkpoint.

This mirrors ``onnx_dump.py``: images are stretched to 640x640, RGB values
are divided by 255, raw box/class outputs are decoded with argmax, and boxes
are mapped back to the original image size.
"""

import argparse
import os
import time

import numpy as np
from PIL import Image


def preprocess_image(path):
    """Return a stretched RGB image as float32 CHW in the range [0, 1]."""
    with Image.open(path) as source:
        image = source.convert("RGB")
        width, height = image.size
        array = np.asarray(
            image.resize((640, 640), Image.BILINEAR),
            dtype=np.float32,
        )
    return np.ascontiguousarray(array.transpose(2, 0, 1) / 255.0), (width, height)


def decode_predictions(predictions, sizes, stems=None):
    """Convert normalized ``[cx, cy, w, h, logits...]`` rows to detection rows."""
    predictions = np.asarray(predictions)
    if predictions.ndim == 2:
        predictions = predictions[None, ...]
    if predictions.ndim != 3 or predictions.shape[-1] < 5:
        raise ValueError("predictions must have shape [batch, queries, 4+classes]")
    if len(sizes) != predictions.shape[0]:
        raise ValueError("one image size is required per prediction batch item")
    if stems is None:
        stems = ["image"] * predictions.shape[0]
    if len(stems) != predictions.shape[0]:
        raise ValueError("one image stem is required per prediction batch item")

    rows = []
    for prediction, (width, height), stem in zip(predictions, sizes, stems):
        logits = prediction[:, 4:]
        classes = logits.argmax(axis=1)
        scores = logits.max(axis=1)
        center_x = prediction[:, 0] * width
        center_y = prediction[:, 1] * height
        box_width = prediction[:, 2] * width
        box_height = prediction[:, 3] * height
        x1 = np.clip(center_x - box_width / 2, 0, width)
        y1 = np.clip(center_y - box_height / 2, 0, height)
        x2 = np.clip(center_x + box_width / 2, 0, width)
        y2 = np.clip(center_y + box_height / 2, 0, height)

        for index in range(prediction.shape[0]):
            score = float(scores[index])
            if score <= -1.0:
                rows.append((stem, -1, -1.0, 0, 0, 0, 0))
                continue
            left = int(round(float(x1[index])))
            top = int(round(float(y1[index])))
            right = int(round(float(x2[index])))
            bottom = int(round(float(y2[index])))
            if right <= left or bottom <= top:
                continue
            rows.append(
                (
                    stem,
                    int(classes[index]),
                    score,
                    left,
                    top,
                    right,
                    bottom,
                )
            )
    return rows


def select_combined_output(outputs):
    """Return the ``[1, queries, 4+classes]`` output from an RT-DETR ONNX graph."""
    candidates = []
    for output in outputs:
        array = np.asarray(output)
        if array.ndim == 3 and array.shape[0] == 1 and array.shape[-1] >= 5:
            candidates.append(array)
    if not candidates:
        raise ValueError("no combined box/class prediction tensor found")
    return max(candidates, key=lambda array: array.shape[-1])


def find_images(image_dir):
    """Return sorted supported image paths in ``image_dir``."""
    suffixes = (".jpg", ".jpeg", ".png", ".bmp", ".webp")
    return sorted(
        os.path.join(image_dir, name)
        for name in os.listdir(image_dir)
        if name.lower().endswith(suffixes)
    )


def write_rows(file_handle, rows):
    """Write detection rows using the shared evaluator's text format."""
    for stem, cls, score, x1, y1, x2, y2 in rows:
        file_handle.write(
            f"{stem} {cls} {score:.5f} {x1} {y1} {x2} {y2}\n"
        )


def run(args):
    """Run PT or ONNX inference and return the number of written rows."""
    paths = find_images(args.images)
    if args.limit:
        paths = paths[: args.limit]
    if not paths:
        raise FileNotFoundError(f"no images found in {args.images}")

    started = time.time()
    written = 0
    with open(args.out, "w", encoding="utf-8") as output:
        if args.onnx:
            import onnxruntime as ort

            options = ort.SessionOptions()
            if args.threads:
                options.intra_op_num_threads = args.threads
            session = ort.InferenceSession(
                str(args.onnx),
                options,
                providers=["CPUExecutionProvider"],
            )
            input_name = session.get_inputs()[0].name
            for index, path in enumerate(paths, start=1):
                tensor, size = preprocess_image(path)
                stem = os.path.splitext(os.path.basename(path))[0]
                raw = session.run(None, {input_name: tensor[None]})
                rows = decode_predictions(select_combined_output(raw), [size], [stem])
                write_rows(output, rows)
                written += len(rows)
                if index % 100 == 0 or index == len(paths):
                    elapsed = time.time() - started
                    print(
                        f"  {index}/{len(paths)}  {elapsed:.1f}s"
                        f"  ({index / elapsed:.2f} img/s)",
                        flush=True,
                    )
        else:
            import torch
            from ultralytics import RTDETR

            if args.threads:
                torch.set_num_threads(args.threads)
            model = RTDETR(str(args.pt)).model.cpu().eval()
            for start in range(0, len(paths), args.batch):
                batch_paths = paths[start : start + args.batch]
                tensors, sizes, stems = [], [], []
                for path in batch_paths:
                    tensor, size = preprocess_image(path)
                    tensors.append(tensor)
                    sizes.append(size)
                    stems.append(os.path.splitext(os.path.basename(path))[0])

                batch = torch.from_numpy(np.stack(tensors))
                with torch.inference_mode():
                    result = model(batch)
                predictions = (
                    result[0] if isinstance(result, (tuple, list)) else result
                )
                rows = decode_predictions(
                    predictions.detach().cpu().numpy(),
                    sizes,
                    stems,
                )
                write_rows(output, rows)
                written += len(rows)
                completed = min(start + args.batch, len(paths))
                if completed % 100 == 0 or completed == len(paths):
                    elapsed = time.time() - started
                    print(
                        f"  {completed}/{len(paths)}  {elapsed:.1f}s"
                        f"  ({completed / elapsed:.2f} img/s)",
                        flush=True,
                    )
    return written


def main():
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--pt")
    source.add_argument("--onnx")
    parser.add_argument("--images", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--batch", type=int, default=4)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--threads", type=int, default=0)
    args = parser.parse_args()

    started = time.time()
    written = run(args)
    elapsed = time.time() - started
    print(f"done: rows={written} elapsed={elapsed:.1f}s", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
