# Artifact definition-range model

ATHENA expects a small instruction-tuned 3B to 4B GGUF model at:

```text
$ATHENA_PATH/tools/artifacts/artifact-range-model.gguf
```

The model selects which nearby paragraphs define a bold mathematical term. It
must follow a constrained prompt and return only a bracketed list of paragraph
offsets, such as `[0]` or `[-1, 0, 1]`. A quantized instruct model with a 4096
token context is sufficient; larger models are unnecessary for this task.

For development and packaging, `ATHENA_ARTIFACT_RANGE_MODEL` may point to a
different GGUF. The user preference `artifacts definition range model` provides
the same override after ATHENA has initialized. If no model is installed,
ATHENA indexes bold terms with paragraph `0` only and reports a warning.
