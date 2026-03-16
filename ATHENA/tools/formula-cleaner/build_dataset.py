import os
import re
import json
import random
import argparse


def is_clean_formula(formula):
    """
    反向启发式过滤器：拦截包含已知视觉 Hack 的公式，
    确保抽出来的都是原生干净的公式。
    """
    bad_patterns = [
        r"\\choose",  # 老式组合数
        r"\\begin\{array\}",  # 用 array 强行画矩阵
        r"\\text\{d\}",  # 用 text 模拟微分
        r"\\left<",
        r"\\right>",  # 不规范的尖括号
        r"\\text\{rank\}",
        r"\\text\{ker\}",
        r"\\text\{im\}",  # 未使用 operatorname
        r"\\text\{sgn\}",
        r"\\text\{Tr\}",
        r"\\left\vert",
        r"\\right\vert",  # 不规范的行列式竖线
    ]

    for pattern in bad_patterns:
        if re.search(pattern, formula):
            return False

    # 过滤掉太短的玩具公式 (如 $$ x+y=z $$)
    if len(formula.strip()) < 30:
        return False

    return True


def extract_from_obsidian(vault_path, max_samples):
    """遍历 Obsidian 仓库，提取符合要求的干净公式块"""
    print(f"正在扫描 Obsidian 仓库: {vault_path}")
    all_formulas = set()  # 使用 set 自动去重

    # 精确匹配 Obsidian 的独立公式块 $$ ... $$
    block_math_pattern = re.compile(r"\$\$(.*?)\$\$", re.DOTALL)

    # 遍历所有 markdown 文件
    for root, dirs, files in os.walk(vault_path):
        # 忽略 .obsidian 等隐藏目录
        if ".obsidian" in root or ".git" in root or ".trash" in root:
            continue

        for file in files:
            if file.endswith(".md"):
                file_path = os.path.join(root, file)
                try:
                    with open(file_path, "r", encoding="utf-8") as f:
                        content = f.read()
                        matches = block_math_pattern.findall(content)
                        for match in matches:
                            formula = match.strip()
                            if is_clean_formula(formula):
                                all_formulas.add(formula)
                except Exception as e:
                    # 忽略个别编码异常的文件
                    continue

    all_formulas = list(all_formulas)
    print(f"扫描完毕，在仓库中发现了 {len(all_formulas)} 个高度纯净的独立公式块。")

    if len(all_formulas) > max_samples:
        print(f"正在随机抽取 {max_samples} 条作为正则化对照组...")
        return random.sample(all_formulas, max_samples)
    else:
        print(f"警告：仓库中提取到的纯净公式不足 {max_samples} 条。")
        return all_formulas


def main():
    parser = argparse.ArgumentParser(
        description="扫描 Obsidian 仓库并与合成数据合并为最终微调集"
    )
    parser.add_argument(
        "--vault-path", required=True, help="你的 Obsidian 仓库本地绝对路径"
    )
    parser.add_argument(
        "--synthetic",
        default="synthetic_dataset.jsonl",
        help="已生成的合成数据 JSONL 文件",
    )
    parser.add_argument(
        "--output", default="finetune_dataset.jsonl", help="最终输出的微调文件"
    )
    parser.add_argument(
        "--max-clean", type=int, default=1600, help="需抽取的纯净公式数量"
    )

    args = parser.parse_args()

    system_instruction = """你是一个顶级的 LaTeX 数据集合成与排版规范化专家。
你的任务是基于用户提供的少量（Few-Shot）真实样本，合成出全新的、跨数学领域（如代数几何、拓扑、偏微分方程、数论、矩阵论、同调代数、概率论等）的公式对。

【绝对不可违反的铁律 (Hard Constraints)】
1. 仅做句法转换（Syntactic ONLY）：绝对禁止修改、修正、计算或化简公式的数学语义。哪怕原公式在数学逻辑上是荒谬的，或者保留了未化简的组合数（如 a^{p-1 \\choose 2}），你也只能将其规范排版为 a^{\\binom{p-1}{2}}。
2. 绝对保留私人记号（Private Notation）：必须原样保留自定义的非标准后缀、下标或文本标签（如 <v, e_j>_{priv}, \\subset_{open} 等）。
3. 顶级排版微调（Micro-typography）：在 clean 字段中，需严格执行最高标准的 LaTeX 排版规范。例如，微分算子 d 必须转为直立体 \\mathrm{d} 并前置微小间距 \\,；精准使用 \\operatorname{} 处理 ker, im, rank 等算子；精准使用 \\begin{vmatrix}, \\begin{pmatrix}, \\begin{cases} 等高级环境。
4. 强制巨型公式占比（Giant Formula Quota）：你生成的公式绝不能全是短句。必须包含大量长度超过 400 字符、深度嵌套的“地狱级复杂度”公式。请尽情构造包含 4 行以上的 \\begin{align} 长串推导、5x5 以上的巨型行列式/块矩阵、包含连分数或多重求和积分的复杂结构。

【输出格式约束】
你必须且只能输出一个合法的 JSON 对象，包含一个名为 "dataset" 的数组。数组内的每个元素必须包含：
- "dirty": 包含你故意构造的视觉 Hack 和排版劣习的 MathJax 公式字符串。
- "clean": 严格遵守上述铁律、语义绝对忠诚的纯净 amsmath 字符串。"""

    dataset = []

    # 1. 加载 DeepSeek R1 合成数据 (Dirty -> Clean)
    if os.path.exists(args.synthetic):
        with open(args.synthetic, "r", encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    dataset.append(json.loads(line))
        synthetic_count = len(dataset)
        print(f"成功加载 {synthetic_count} 条合成脏数据。")
    else:
        raise FileNotFoundError(f"找不到合成数据文件: {args.synthetic}")

    # 2. 从 Obsidian 提取并加载纯净数据 (Clean -> Clean)
    clean_formulas = extract_from_obsidian(args.vault_path, args.max_clean)

    # 3. 包装纯净数据
    clean_count = 0
    for formula in clean_formulas:
        record = {
            "messages": [
                {"role": "system", "content": system_instruction},
                {"role": "user", "content": formula},
                {
                    "role": "assistant",
                    "content": formula,
                },  # Input == Output 避免模型患上修改强迫症
            ]
        }
        dataset.append(record)
        clean_count += 1

    # 4. 全局洗牌
    print("正在执行全局洗牌 (Shuffling)...")
    random.seed(42)
    random.shuffle(dataset)

    # 5. 落盘写入
    with open(args.output, "w", encoding="utf-8") as f:
        for item in dataset:
            f.write(json.dumps(item, ensure_ascii=False) + "\n")

    print("=" * 40)
    print("微调数据集组装完毕！")
    print(f"Gemma-3-4B-IT 将学习到:")
    print(f"- {synthetic_count} 条需要纠正的复杂公式映射")
    print(f"- {clean_count} 条按兵不动的原生纯净公式")
    print(f"总数据量: {len(dataset)} 条")
    print("=" * 40)


if __name__ == "__main__":
    main()
