import json
from pathlib import Path
from transformers import AutoTokenizer

base = "NousResearch/Llama-2-7b-chat-hf"
out = Path("./formula-cleaner-merged-hf")

for name in (
    "tokenizer.json",
    "added_tokens.json",
    "special_tokens_map.json",
):
    path = out / name
    if path.exists():
        path.unlink()

tokenizer = AutoTokenizer.from_pretrained(base, use_fast=False)
tokenizer.save_pretrained(out)

tokenizer_json_path = out / "tokenizer.json"
if tokenizer_json_path.exists():
    tokenizer_json = json.loads(tokenizer_json_path.read_text())
    vocab = tokenizer_json.get("model", {}).get("vocab", {})
    vocab_size = len(vocab)
    tokenizer_json["added_tokens"] = [
        token
        for token in tokenizer_json.get("added_tokens", [])
        if token.get("id", -1) < vocab_size
    ]
    tokenizer_json_path.write_text(json.dumps(tokenizer_json, ensure_ascii=False) + "\n")

tokenizer_config_path = out / "tokenizer_config.json"
if tokenizer_config_path.exists():
    tokenizer_config = json.loads(tokenizer_config_path.read_text())
    tokenizer_config.pop("pad_token", None)
    tokenizer_config_path.write_text(json.dumps(tokenizer_config, indent=2) + "\n")

config_path = out / "config.json"
config = json.loads(config_path.read_text())
config["vocab_size"] = 32000
config_path.write_text(json.dumps(config, indent=2) + "\n")
