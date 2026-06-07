import torch
from transformers import AutoModelForCausalLM, AutoTokenizer
from peft import PeftModel

base = "NousResearch/Llama-2-7b-chat-hf"
adapter = "/home/felix/data/Software/TeXmacs/texmacs/ATHENA/tools/formula-cleaner/finetuned-fcleaner"
out = "./formula-cleaner-merged-hf"

tokenizer = AutoTokenizer.from_pretrained(adapter, use_fast=True)

model = AutoModelForCausalLM.from_pretrained(
    base,
    torch_dtype=torch.float16,
    device_map="auto",
    low_cpu_mem_usage=True,
)

model = PeftModel.from_pretrained(model, adapter)
model = model.merge_and_unload()

model.save_pretrained(out, safe_serialization=True)
tokenizer.save_pretrained(out)
