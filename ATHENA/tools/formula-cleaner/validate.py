import torch
from transformers import AutoModelForCausalLM, AutoTokenizer
from peft import PeftModel

# 1. 加载基础模型和分词器
base_model_id = "NousResearch/Llama-2-7b-chat-hf"
lora_path = "./final_latex_llama2"

print("正在加载底座模型 (纯 FP16)...")
tokenizer = AutoTokenizer.from_pretrained(lora_path)
base_model = AutoModelForCausalLM.from_pretrained(
    base_model_id,
    torch_dtype=torch.float16,
    device_map="auto"
)

# 2. 挂载你训练好的 LoRA 权重
print("正在注入 LaTeX 清洗专家权重...")
model = PeftModel.from_pretrained(base_model, lora_path)

# 3. 准备一段没见过的、带有乱码的 LaTeX 进行测试
raw_text = r"""
\begin{align}
\left<E,\psi\right>&=\int_0^{+\infty}\frac{1}{4\pi a^2t}\int_{S_{at}}\psi(x,t)\text{d}\sigma \text{d}t=\int_0^{+\infty}t\bar\psi(at,t)\text{d}t\\
\left<\square_a E,\varphi\right>&=\int_0^{+\infty}t\overline{\square_a\varphi}(at,t)\text{d}t=\int_0^{+\infty}t\left( \overline{\varphi_{tt}}-a^2\overline{\varphi}_{rr}-\frac{2a^2}{r}\overline{\varphi_r} \right)_{r=at,t=t}\text{d}t\\&=\int_0^{+\infty}\frac{1}{a^2}\left(\left(\partial_t^2-a^2\partial_r^2\right)\left(ar\bar\varphi(r,t)\right)\right)_{r=at}\text{d}t\\&=\frac1{a^2}\int_0^{+\infty}\frac{\text d}{\text d t}\left[\left(\partial_t-a\partial_r\right)ar\bar\varphi\right]_{(r,t)=:(at,t)}\text{d}t\\&=\frac1{a^2}\Big[G(at,t)\Big]_{t=0}^{t=+\infty}
\end{align}
"""

# 严格按照训练时的 [INST] 模板进行拼接
prompt = f"<s>[INST] {raw_text} [/INST] "
inputs = tokenizer(prompt, return_tensors="pt").to("cuda")

print("开始生成...")
with torch.no_grad():
    outputs = model.generate(
        **inputs,
        max_new_tokens=1024,
        temperature=0.1,  # 清洗任务建议设为极低温度，防止模型乱发挥
        top_p=0.9,
        repetition_penalty=1.1
    )

# 裁切掉输入的 Prompt 部分，只提取模型回答
response = tokenizer.decode(outputs[0][inputs.input_ids.shape[1]:], skip_special_tokens=True)

print("\n========== 清洗结果 ==========\n")
print(response)
