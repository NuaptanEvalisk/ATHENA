import torch
print(f"PyTorch Version: {torch.__version__}")
print(f"CUDA Available: {torch.cuda.is_available()}")
print(f"GPU Name: {torch.cuda.get_device_name(0)}")

# 核心测试：验证 P100 的 FP16 计算是否正常
try:
    x = torch.randn((1024, 1024), dtype=torch.float16).cuda()
    res = torch.matmul(x, x)
    print("✅ P100 FP16 Matrix Multiplication: Success")
except Exception as e:
    print(f"❌ FP16 Error: {e}")

# 核心测试：验证 BF16 是否报错（P100 应该不支持）
try:
    y = torch.randn((1024, 1024), dtype=torch.bfloat16).cuda()
    print("⚠️ 警告：这台 P100 竟然支持 BF16？（这不科学）")
except Exception:
    print("✅ 验证通过：P100 确实不支持 BF16，微调时记得避开。")
