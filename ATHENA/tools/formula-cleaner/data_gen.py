import argparse
import json
import time
import os
import google.generativeai as genai


def load_formulas(file_path):
    """按独立的 '-----' 行拆分文本文件并提取公式块"""
    formulas = []
    current_formula = []

    if not os.path.exists(file_path):
        raise FileNotFoundError(f"找不到输入文件: {file_path}")

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            # 剥离空白符后精确匹配分隔符
            if line.strip() == "-----":
                if current_formula:
                    # 将收集到的行合并，并剥离首尾多余空白
                    formulas.append("".join(current_formula).strip())
                    current_formula = []
            else:
                current_formula.append(line)

    # 处理文件末尾没有分隔符的情况
    if current_formula:
        formulas.append("".join(current_formula).strip())

    # 过滤掉可能的空字符串
    return [f for f in formulas if f]


def main():
    parser = argparse.ArgumentParser(
        description="使用 Gemini API 批量清洗 MathJax 视觉 Hack"
    )
    parser.add_argument("--api-key", required=True, help="Google Gemini API Key")
    parser.add_argument(
        "--input", default="formulas.txt", help="包含原始公式的文本文件路径"
    )
    parser.add_argument(
        "--output", default="cleaned_formulas.json", help="输出的 JSON 文件路径"
    )
    parser.add_argument(
        "--chunk-size", type=int, default=10, help="每次 API 请求打包的公式数量"
    )
    parser.add_argument(
        "--sleep",
        type=int,
        default=5,
        help="每次请求后的休眠时间(秒)，防止触发 Rate Limit",
    )

    args = parser.parse_args()

    genai.configure(api_key=args.api_key)
    model = genai.GenerativeModel(
        model_name="gemini-3.1-pro-preview",
        generation_config={"response_mime_type": "application/json"},
    )

    system_instruction = """
你是一个顶级的 LaTeX 数据集合成与排版规范化专家。
你的任务是基于用户提供的少量（Few-Shot）真实样本，合成出 50 个全新的、跨数学领域（如代数几何、拓扑、偏微分方程、数论、矩阵论、集合论、数理逻辑、同调代数、数学分析、线性代数、概率论、复分析等）的公式对。

【核心任务定义】
用户提供的样本展示了特定的“不规范 MathJax 视觉排版 Hack”及其对应的“标准 amsmath 纯净写法”。
请深度学习这些样本中的“错误模式”（例如：省略下标的下划线、使用 \\array 强行模拟行列式或分段函数、老式 \\choose 语法、微积分算符粘连、用 \\text{d} 表示微分算子等），并将其发散应用到你全新捏造的数学公式中，生成具有高度多样性的训练数据。

【绝对不可违反的铁律 (Hard Constraints)】
1. 仅做句法转换（Syntactic ONLY）：绝对禁止修改、修正、计算或化简公式的数学语义。哪怕原公式在数学逻辑上是荒谬的，或者保留了未化简的组合数（如 a^{p-1 \\choose 2}），你也只能将其规范排版为 a^{\\binom{p-1}{2}}，绝不允许擅自修改其数学内涵。
2. 绝对保留私人记号（Private Notation）：必须原样保留自定义的非标准后缀、下标或文本标签（如 <v, e_j>_{priv}, \\subset_{open} 等），绝不可将其替换为学界通用符号。
3. 顶级排版微调（Micro-typography）：在 clean 字段中，需严格执行最高标准的 LaTeX 排版规范。例如，微分算子 d 必须转为直立体 \\mathrm{d} 并前置微小间距 \\,；精准使用 \\operatorname{} 处理 ker, im, rank, sgn 等算子；精准使用 \\begin{vmatrix}, \\begin{pmatrix}, \\begin{cases} 等高级环境替代定界符与 array 的生硬组合。

【输出格式约束】
严格且仅输出一个合法的 JSON 对象。必须包含一个名为 "dataset" 的数组，数组内的每个元素必须包含以下两个字段：
- "dirty": 包含你故意构造的视觉 Hack 和排版劣习的 MathJax 公式字符串。
- "clean": 严格遵守上述铁律、语义绝对忠诚的纯净 amsmath 字符串。
警告：不要输出任何形式的 Markdown 格式标记（如 ```json），不要包含任何解释性文本或换行符，确保 JSON 能够被标准解析器直接加载。
"""

    print(f"正在加载公式数据: {args.input}")
    formulas = load_formulas(args.input)
    total_formulas = len(formulas)
    print(f"成功加载 {total_formulas} 个公式。")

    results = []

    for i in range(0, total_formulas, args.chunk_size):
        chunk = formulas[i : i + args.chunk_size]
        prompt = (
            f"{system_instruction}\n\n输入列表: {json.dumps(chunk, ensure_ascii=False)}"
        )

        try:
            print(
                f"正在处理第 {i} 到 {min(i + args.chunk_size, total_formulas)} 个公式..."
            )
            response = model.generate_content(prompt)
            chunk_result = json.loads(response.text)

            extracted_data = chunk_result.get("dataset", [])
            results.extend(extracted_data)

            # 实时追加写入，防止中途崩溃丢失数据
            with open(args.output, "w", encoding="utf-8") as f:
                json.dump({"dataset": results}, f, ensure_ascii=False, indent=2)

            time.sleep(args.sleep)

        except Exception as e:
            print(f"处理第 {i} 批次时发生严重错误: {e}")
            print(f"休眠 {args.sleep * 3} 秒后重试...")
            time.sleep(args.sleep * 3)

    print(f"\n清洗完成！共处理了 {len(results)} 个公式，已保存至 {args.output}")


if __name__ == "__main__":
    main()
