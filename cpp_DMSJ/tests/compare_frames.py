# 临时诊断：两帧原始 RGB 的平均绝对差（MSE 类指标）
import sys

w = int(sys.argv[3])
h = int(sys.argv[4])
a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
n = w * h * 3
total = 0
for i in range(0, n, 3):
	total += abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2])
print("mean_abs_diff", total / (n // 3))
