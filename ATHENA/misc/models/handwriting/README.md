# Hand TeX model assets

ATHENA uses the Hand TeX single-symbol classifier by VoxelCubes for the
Handwritten Symbol pane. The model and the metadata from which the runtime
tables were generated are released under GPL-3.0-compatible terms.

Upstream: https://github.com/VoxelCubes/Hand-TeX

Upstream revision used for metadata: `5613813717c393fb5bcb405a83e63facdab1c998`

Model release inputs:

- `handtex.safetensors` SHA-256:
  `f3fcb8004e67826590e2b7860ccf835efa56031bc4b3b236d6aeecfd7e6a2eef`
- `encodings.txt` SHA-256:
  `47adad1c942f3245b934d229cbb533d262bb4c3da4a81b57db15c68f072e9bd1`

The checked-in `handtex.ncnn.param` and `handtex.ncnn.bin` files are a lossless
FP32 conversion made with pnnx 20260526. ATHENA runs them with ncnn pinned at
revision `e54f7b1f88434e1d844ea0551b880a1cfb079ce1`. Regenerate all runtime
assets with `tools/handwriting/export_handtex_model.py`.

The complete Hand TeX GPLv3 license and ncnn license notices are included as
`LICENSE.Hand-TeX.txt` and `LICENSE.ncnn.txt`.
