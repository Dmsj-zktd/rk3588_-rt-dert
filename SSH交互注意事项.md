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

### 11. 含空格路径**禁止**走“远端变量 + 双引号展开”组合
- PowerShell 会吞掉 `"$S"` 里的双引号 → 远端 `uav_Unconventional: command not found`。
- 正确：每个命令处直接内联 `''/path/with space/file''`（bash 单引号，PS 双写）。
- 错误：`S=''/path/with space/file''; ffmpeg -i "$S" ...`（`"$S"` 被吞，路径被空格拆散）。

### 12. bash 单引号内的 `$n` 不会展开（ffmpeg select 等过滤器）
- `-vf ''select=eq(n\,$n)''` 在循环里会原样传给 ffmpeg → `Undefined constant '$n'`。
- 正确：帧号写死字面量 `''select=eq(n\,200)''`；需要循环时把变量放到引号外再拼接。

### 13. 后台测试进程必须加超时/看门狗，且注意 `$!` 指向的对象
- 进程可能阻塞在不可中断等待（本次模型路径错误导致 wait_idle 永久挂起、SIGTERM 无效）。
- 看门狗写法：后台运行后 `END=$((SECONDS+180))`，循环里 `kill -0 $PID && [ $SECONDS -lt $END ]`，
  超时 `kill -9 $PID`；不要用 `timeout` 包装再取 `$!`（会采到包装进程，RSS 只有 ~800KB）。
- 任何跑在板端的测试命令一律加 timeout 或看门狗。

### 14. base64 直传复杂脚本在本机 PowerShell 下不可靠，不作首选
- 外层 `"echo $b64 | base64 -d | bash"` 存在解析歧义（偶发引号/命令错位）；复杂脚本优先拆成
  多条“内联字面量”命令，或写成脚本文件（需用户许可）再执行。
