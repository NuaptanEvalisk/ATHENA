import torch
from datasets import load_dataset
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    BitsAndBytesConfig,
    TrainingArguments,
    Trainer,
    DataCollatorForLanguageModeling
)
from peft import LoraConfig, get_peft_model, prepare_model_for_kbit_training

# -----------------------------
# 1. 关闭所有可能触发 BF16 的东西
# -----------------------------
torch.backends.cuda.matmul.allow_tf32 = False
torch.backends.cudnn.allow_tf32 = False

# -----------------------------
# 2. 数据集
# -----------------------------
dataset = load_dataset("json", data_files="finetune_dataset.jsonl", split="train")

def format_llama2(example):
    text = "<s>"
    for msg in example["messages"]:
        if msg["role"] == "user":
            text += f"[INST] {msg['content']} [/INST] "
        elif msg["role"] == "assistant":
            text += f"{msg['content']} </s><s>"

    if text.endswith("<s>"):
        text = text[:-3]

    return {"text": text}

dataset = dataset.map(format_llama2, remove_columns=["messages"])

# -----------------------------
# 3. tokenizer
# -----------------------------
model_id = "NousResearch/Llama-2-7b-chat-hf"

tokenizer = AutoTokenizer.from_pretrained(model_id)
tokenizer.pad_token = tokenizer.eos_token
tokenizer.padding_side = "right"

def tokenize(example):
    return tokenizer(
        example["text"],
        truncation=True,
        padding="max_length",
        max_length=1024
    )

dataset = dataset.map(tokenize)

# -----------------------------
# 4. 4bit 量化
# -----------------------------
bnb_config = BitsAndBytesConfig(
    load_in_4bit=True,
    bnb_4bit_quant_type="nf4",
    bnb_4bit_compute_dtype=torch.float16,
    bnb_4bit_use_double_quant=True,
)

model = AutoModelForCausalLM.from_pretrained(
    model_id,
    quantization_config=bnb_config,
    torch_dtype=torch.float16,
    device_map="auto"
)

model.config.use_cache = False

# -----------------------------
# 5. LoRA
# -----------------------------
model = prepare_model_for_kbit_training(model)

peft_config = LoraConfig(
    r=16,
    lora_alpha=32,
    target_modules=[
        "q_proj","k_proj","v_proj","o_proj",
        "gate_proj","up_proj","down_proj"
    ],
    lora_dropout=0.05,
    bias="none",
    task_type="CAUSAL_LM"
)

model = get_peft_model(model, peft_config)

# -----------------------------
# 6. Data collator
# -----------------------------
data_collator = DataCollatorForLanguageModeling(
    tokenizer=tokenizer,
    mlm=False
)

# -----------------------------
# 7. TrainingArguments
# 关键：完全禁用 AMP
# -----------------------------
training_args = TrainingArguments(
    output_dir="./llama2-latex-cleaning",
    per_device_train_batch_size=2,
    gradient_accumulation_steps=4,
    learning_rate=1e-4,
    num_train_epochs=3,

    fp16=False,        # ← 关键
    bf16=False,        # ← 关键
    tf32=False,

    logging_steps=2,
    save_strategy="steps",
    save_steps=10,

    gradient_checkpointing=True,
    optim="adamw_torch",
)

# -----------------------------
# 8. Trainer
# -----------------------------
trainer = Trainer(
    model=model,
    args=training_args,
    train_dataset=dataset,
    data_collator=data_collator,
)

# -----------------------------
# 9. Train
# -----------------------------
trainer.train()

model.save_pretrained("./final_latex_llama2")
tokenizer.save_pretrained("./final_latex_llama2")
