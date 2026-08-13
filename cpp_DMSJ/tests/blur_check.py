# 临时诊断：计算原始 RGB 帧的边缘能量（平均相邻像素差），值越低越模糊
import sys

w = int(sys.argv[2])
h = int(sys.argv[3])
data = open(sys.argv[1], "rb").read()

def edge_energy(y0, y1):
	total = 0
	n = 0
	for y in range(y0, y1, 2):
		row = data[y * w * 3:(y + 1) * w * 3]
		for x in range(0, w - 1, 2):
			i = x * 3
			total += abs(row[i] - row[i + 3]) + abs(row[i + 1] - row[i + 4]) + abs(row[i + 2] - row[i + 5])
			n += 1
	return total / n

print("edge_energy_all", edge_energy(0, h))
print("edge_energy_top", edge_energy(0, min(100, h)))
print("edge_energy_mid", edge_energy(h // 2 - 50, h // 2 + 50))
print("edge_energy_bot", edge_energy(max(0, h - 100), h))

# 拉普拉斯方差（标准模糊指标，值越低越模糊）
vals = []
for y in range(2, h - 2, 3):
	for x in range(2, w - 2, 3):
		for c in range(3):
			i = y * w * 3 + x * 3 + c
			lap = 4 * data[i] - data[i - 3] - data[i + 3] - data[(y - 1) * w * 3 + x * 3 + c] - data[(y + 1) * w * 3 + x * 3 + c]
			vals.append(lap)
mean = sum(vals) / len(vals)
var = sum((v - mean) ** 2 for v in vals) / len(vals)
print("laplacian_var", var)
