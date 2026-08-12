# SSH 交互注意事项（记忆文档，执行 ssh 指令前必须自检）

> 由 AGENTS.md 启用调度。**每次执行 ssh/scp 命令前，先对照本清单检查引号与特殊字符**，确保一次执行正确，避免"先犯错再修复"。

## 根因

从 Windows PowerShell 调用 ssh.exe 时，远端命令作为单个参数传给 Windows 命令行解析（CommandLineToArgvW），**内嵌的双引号会被吞掉**，导致远端 bash 看到的内容与本地写的不一致。

## 规则清单

### 1. 远端 bash 命令一律用 PowerShell 单引号字符串包裹
- 正确：`$cmd = 'cd /tmp; echo hello'`
- 错误：`$cmd = "cd /tmp; echo $HOME"`（PowerShell 会展开 `$HOME`）

### 2. 远端需要引号时，用 bash 单引号，PowerShell 中用 `''` 转义
- 需要 bash 单引号 `'...'` 时，在 PowerShell 单引号字符串里写成 `''...''`
- 正确：`$cmd = 'sudo -n sh -c ''echo performance > /sys/.../governor'''`
- 错误：`$cmd = 'sudo -n sh -c "echo performance > /sys/..."'`（双引号会被吞，重定向落到非 root shell，报 Permission denied）

### 3. grep 多模式用 `-e`，禁止用引号包 `\|`
- 正确：`grep -rn -e imresize -e imcvtcolor /usr/include/rga/`
- 错误：`grep -rn "a\|b" dir`（双引号被吞后 `\|` 变管道，报 command not found）

### 4. echo 等输出文案避免 `( )`、`|`、`&`、`>` 等特殊字符
- 正确：`echo === FPS 回归 默认全日志 ===`
- 错误：`echo === FPS 回归(默认) ===`（括号被 bash 解析，报 syntax error）

### 5. 含空格的路径（如录屏 mp4）用 bash 单引号（`''` 转义）
- 正确：`-v ''/path/with space.mp4''`

### 6. scp 远程目标必须是单个字符串参数
- 正确：`& $scp -i $key "$base\f.cc" "$dst/src/f.cc"`（`$dst` 与文件名都在同一个双引号字符串内）
- 错误：`"$dst"src/f.cc`（PowerShell 解析成两个参数，scp 报 No such file）

### 7. scp 失败要检查退出码，不要用 Out-Null 掩盖
- 每条 scp 后检查 `$LASTEXITCODE`，或直接保留输出；上传成功后再进入下一步。

### 8. 含单引号/复杂逻辑的 awk、printf 程序，改用无引号方案
- 优先 bash 整数运算 + `cut -d. -f1`；或写临时脚本文件，避免引号地狱。

### 9. 先小步验证，再跑长命令
- 新命令模式先跑一条最小样例确认输出符合预期，再批量执行。

### 10. 板端性能指令（每次 SSH 登录后）必须用 bash 单引号包 `sh -c`
- 参照第 2 条，5 条 governor 指令统一写成 `sudo -n sh -c ''echo performance > ...''` 形式。
