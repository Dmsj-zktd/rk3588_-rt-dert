# 临时诊断：检查输出视频首帧顶部/中部是否存在绿/紫伪影（AFBC 未禁用的特征）
import sys

w, h = 1920, 1080
data = open(sys.argv[1], "rb").read()

def scan(y0, y1):
	g = 0
	p = 0
	for y in range(y0, y1):
		row = data[y * w * 3:(y + 1) * w * 3]
		for i in range(0, len(row), 3):
			r = row[i]
			g2 = row[i + 1]
			b = row[i + 2]
			if g2 > 150 and r < 100 and b < 100:
				g += 1
			if r > 150 and b > 150 and g2 < 100:
				p += 1
	return g, p

print("top_rows", scan(0, 20))
print("mid_rows", scan(500, 520))
