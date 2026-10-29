"""Tests for the original RT-DETR checkpoint detection dump."""

import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from tools.rtdetr_pt_dump import (
    decode_predictions,
    preprocess_image,
    select_combined_output,
)


class RtdetrPtDumpTest(unittest.TestCase):
    def test_decode_maps_normalized_boxes_to_original_pixels(self):
        predictions = np.zeros((1, 300, 14), dtype=np.float32)
        predictions[0, 0, :4] = (0.5, 0.5, 0.2, 0.4)
        predictions[0, 0, 7] = 2.0
        predictions[0, 0, 4:7] = 1.0
        predictions[0, 0, 8:] = -2.0

        rows = decode_predictions(predictions, [(1000, 500)])

        self.assertEqual(rows, [("image", 3, 2.0, 400, 150, 600, 350)])

    def test_decode_marks_low_scores_and_skips_zero_area_boxes(self):
        predictions = np.zeros((1, 2, 14), dtype=np.float32)
        predictions[0, :, 4:] = -2.0
        predictions[0, 1, :4] = (0.5, 0.5, 0.0, 0.2)
        predictions[0, 1, 4] = 2.0

        rows = decode_predictions(predictions, [(100, 100)])

        self.assertEqual(rows, [("image", -1, -1.0, 0, 0, 0, 0)])

    def test_preprocess_returns_rgb_chw_float_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "red.png"
            Image.new("RGB", (1280, 720), (255, 0, 0)).save(path)

            image, size = preprocess_image(path)

        self.assertEqual(size, (1280, 720))
        self.assertEqual(image.shape, (3, 640, 640))
        self.assertEqual(image.dtype, np.float32)
        np.testing.assert_allclose(image[0], 1.0)
        np.testing.assert_allclose(image[1:], 0.0)

    def test_select_combined_output_uses_the_14_value_prediction_tensor(self):
        outputs = [
            np.zeros((1, 300, 14), dtype=np.float32),
            np.zeros((1, 1, 300, 4), dtype=np.float32),
            np.zeros((1, 300, 10), dtype=np.float32),
        ]

        selected = select_combined_output(outputs)

        self.assertEqual(selected.shape, (1, 300, 14))


if __name__ == "__main__":
    unittest.main()
