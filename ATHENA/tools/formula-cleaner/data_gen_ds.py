import argparse
import json
import time
import os
import random
import re
from openai import OpenAI


def load_formulas(file_path):
    """按独立的 '-----' 行拆分文本文件并提取公式块"""
    formulas = []
    current_formula = []

    if not os.path.exists(file_path):
        raise FileNotFoundError(f"找不到输入文件: {file_path}")

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            if line.strip() == "-----":
                if current_formula:
                    formulas.append("".join(current_formula).strip())
                    current_formula = []
            else:
                current_formula.append(line)

    if current_formula:
        formulas.append("".join(current_formula).strip())

    return [f for f in formulas if f]


def extract_json_from_response(text):
    """
    鲁棒的 JSON 提取器：
    由于 DeepSeek-Reasoner (R1) 可能会在输出中包含 Markdown 代码块标记 (```json ... ```)，
    我们需要精准提取出最外层的 JSON 对象。
    """
    start_idx = text.find("{")
    end_idx = text.rfind("}")
    if start_idx != -1 and end_idx != -1:
        json_str = text[start_idx : end_idx + 1]
        return json.loads(json_str)
    raise ValueError("在模型的回复中未找到有效的 JSON 结构。")


def main():
    parser = argparse.ArgumentParser(
        description="使用 DeepSeek R1 (Reasoner) 基于种子公式进行动态数据扩增"
    )
    parser.add_argument("--api-key", required=True, help="DeepSeek API Key")
    parser.add_argument(
        "--input",
        default="seed_formulas.txt",
        help="包含原始种子公式的文本文件路径 (例如你的 164 个种子)",
    )
    parser.add_argument(
        "--output",
        default="synthetic_dataset.jsonl",
        help="输出的 JSONL 格式微调文件路径",
    )
    parser.add_argument(
        "--target-count", type=int, default=3000, help="期望合成的公式总条数"
    )
    parser.add_argument(
        "--sample-size",
        type=int,
        default=5,
        help="每次请求给模型提供的随机种子数量 (Few-shot 示例数)",
    )
    parser.add_argument(
        "--gen-per-req", type=int, default=30, help="要求模型单次请求生成的全新公式数量"
    )
    parser.add_argument("--sleep", type=int, default=2, help="每次请求后的休眠时间(秒)")

    args = parser.parse_args()

    # 初始化 DeepSeek 客户端 (兼容 OpenAI SDK)
    client = OpenAI(api_key=args.api_key, base_url="https://api.deepseek.com")

    system_instruction = """你是一个顶级的 LaTeX 数据集合成与排版规范化专家。
你的任务是基于用户提供的少量（Few-Shot）真实样本，合成出全新的、跨数学领域（如代数几何、拓扑、偏微分方程、数论、矩阵论、同调代数、概率论等）的公式对。

【绝对不可违反的铁律 (Hard Constraints)】
1. 仅做句法转换（Syntactic ONLY）：绝对禁止修改、修正、计算或化简公式的数学语义。哪怕原公式在数学逻辑上是荒谬的，或者保留了未化简的组合数（如 a^{p-1 \\choose 2}），你也只能将其规范排版为 a^{\\binom{p-1}{2}}。
2. 绝对保留私人记号（Private Notation）：必须原样保留自定义的非标准后缀、下标或文本标签（如 <v, e_j>_{priv}, \\subset_{open} 等）。
3. 顶级排版微调（Micro-typography）：在 clean 字段中，需严格执行最高标准的 LaTeX 排版规范。例如，微分算子 d 必须转为直立体 \\mathrm{d} 并前置微小间距 \\,；精准使用 \\operatorname{} 处理 ker, im, rank 等算子；精准使用 \\begin{vmatrix}, \\begin{pmatrix}, \\begin{cases} 等高级环境。
4. 强制巨型公式占比（Giant Formula Quota）：你生成的公式绝不能全是短句。必须包含大量长度超过 400 字符、深度嵌套的“地狱级复杂度”公式。请尽情构造包含 4 行以上的 \\begin{align} 长串推导、5x5 以上的巨型行列式/块矩阵、复杂的代数拓扑长正合列、包含连分数或多重求和、各种复杂算子的多重积分等复杂结构。

【输出格式约束】
你必须且只能输出一个合法的 JSON 对象，包含一个名为 "dataset" 的数组。数组内的每个元素必须包含：
- "dirty": 包含你故意构造的视觉 Hack 和排版劣习的 MathJax 公式字符串。
- "clean": 严格遵守上述铁律、语义绝对忠诚的纯净 amsmath 字符串。"""

    print(f"正在加载种子公式: {args.input}")
    seed_formulas = load_formulas(args.input)
    print(f"成功加载 {len(seed_formulas)} 个种子公式。")

    if len(seed_formulas) == 0:
        print("错误：种子库为空。请检查输入文件。")
        return

    # 如果输出文件已存在，读取已生成的数量以便断点续传
    generated_count = 0
    if os.path.exists(args.output):
        with open(args.output, "r", encoding="utf-8") as f:
            generated_count = sum(1 for _ in f)
        print(f"发现已有输出文件，当前已生成 {generated_count} 条数据，继续追加...")

    request_counter = 1

    # 开启扩增循环，直到达到目标数量
    while generated_count < args.target_count:
        # 1. 动态随机抽样构建 Few-Shot 上下文
        current_sample_size = min(args.sample_size, len(seed_formulas))
        sampled_seeds = random.sample(seed_formulas, current_sample_size)

        user_prompt = f"""以下是我提供的 {current_sample_size} 个不规范公式的真实案例，请深度学习它们产生视觉 Hack 的坏习惯：
{json.dumps(sampled_seeds, ensure_ascii=False, indent=2)}

请发散思维，捏造 {args.gen_per_req} 个全新的、不同数学领域的公式对。
务必确保：
1. 新公式故意包含上述案例中的排版错误特征作为 dirty 字段。
2. 对应的 clean 字段必须是语义完全等价的标准排版。
请以 JSON 格式返回。"""

        try:
            print(
                f"\n[请求 #{request_counter}] 正在调用 DeepSeek R1 进行推理与合成 (目标进度: {generated_count}/{args.target_count})..."
            )
            print("-" * 50)
            print("🧠 R1 实时思考过程:")

            response = client.chat.completions.create(
                model="deepseek-reasoner",
                messages=[
                    {"role": "system", "content": system_instruction},
                    {"role": "user", "content": user_prompt},
                ],
                stream=True,  # 开启流式传输
            )

            raw_output = ""
            for chunk in response:
                # 实时打印思考过程 (reasoning_content)
                if chunk.choices[0].delta.reasoning_content:
                    print(chunk.choices[0].delta.reasoning_content, end="", flush=True)

                # 收集最终的 JSON 输出内容 (content)
                elif chunk.choices[0].delta.content:
                    raw_output += chunk.choices[0].delta.content

            print("\n" + "-" * 50)
            print("✅ 思考结束，正在解析生成的 JSON...")

            # 使用鲁棒的解析器提取 JSON
            parsed_json = extract_json_from_response(raw_output)

            extracted_data = parsed_json.get("dataset", [])

            new_records_count = len(extracted_data)
            if new_records_count == 0:
                print("警告：模型返回了空的 dataset 数组，将重试。")
                continue

            # 2. 实时追加为 JSONL 格式 (ShareGPT/标准微调格式)
            with open(args.output, "a", encoding="utf-8") as f:
                for item in extracted_data:
                    record = {
                        "messages": [
                            {"role": "system", "content": system_instruction},
                            {"role": "user", "content": item.get("dirty", "")},
                            {"role": "assistant", "content": item.get("clean", "")},
                        ]
                    }
                    f.write(json.dumps(record, ensure_ascii=False) + "\n")

            generated_count += new_records_count
            print(
                f"成功合成并保存了 {new_records_count} 条数据。当前总计: {generated_count}"
            )

            request_counter += 1
            time.sleep(args.sleep)

        except json.JSONDecodeError:
            print(
                f"解析失败，模型输出的 JSON 格式损坏。休眠 {args.sleep * 2} 秒后重试..."
            )
            time.sleep(args.sleep * 2)
        except Exception as e:
            print(f"请求发生严重错误: {e}")
            print(f"休眠 {args.sleep * 5} 秒后重试...")
            time.sleep(args.sleep * 5)

    print(
        f"\n合成任务完成！最终共获取 {generated_count} 条微调数据，文件保存在: {args.output}"
    )


if __name__ == "__main__":
    main()
