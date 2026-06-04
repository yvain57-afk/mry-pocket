# 任务：在这台 Air 上装 Codex Usage Daemon

**说明给你看的**：用户在 Mac mini 上做了一个 M5StickS3 的硬件 Codex 用量监控器。Stick 通过 WiFi 拉两个端点：Mac mini（已装好）和 Air（这台机器，你来装）。你的工作是把 daemon 跑起来并验证能通。

---

## 0. 前提

- 这台 Air 已经在用 Codex CLI（`~/.codex/sessions/` 有数据）
- WiFi 接的是 `ChinaNet-mkbh-5G`，当前 IP 应当是 `192.168.2.151`（DHCP 但相对稳定）
- 同一 LAN 上还有 Mac mini `192.168.2.236`，daemon 已在 :8888 运行

---

## 1. 一键安装

```bash
git clone https://github.com/yvain57-afk/mry-pocket.git ~/mry-pocket
cd ~/mry-pocket
bash tools/install_codex_daemon.sh
```

`install_codex_daemon.sh` 做了什么：
- 把 `tools/codex_usage_daemon.py` 复制到 `~/.local/share/mry-pocket/`（launchd 不能从外置卷读）
- 渲染 `~/Library/LaunchAgents/com.mry.codex-usage-daemon.plist`（绝对路径替换）
- `launchctl load -w` 加载
- 等 1 秒后 `curl http://127.0.0.1:8888/health` 自检
- 打印当前数据样本

**成功标志**：脚本最后输出
```
✅ codex-usage-daemon running.
   Hostname:   <something>.local
```
+ 一段 JSON 显示 `primary.used_percent` 等字段。

**失败处理**：脚本会打印 `❌` 然后让你看
```bash
tail ~/Library/Logs/codex-usage-daemon.err
```
常见错误：
- `Operation not permitted` → 重跑脚本（已自动 cp 到 `~/.local/share/`，应该不再触发）
- `address already in use` → `launchctl unload ~/Library/LaunchAgents/com.mry.codex-usage-daemon.plist` 再重跑
- 端口被占 → 改 plist 里 `--port 8888` 改成 `8889`，**同时去和用户报备**让他改 Stick 这边 `wifi_config.h`

---

## 2. 验证可被局域网访问

```bash
# 1) 本机 health
curl -s http://127.0.0.1:8888/health
# 期望: {"status": "ok"}

# 2) 本机数据
curl -s http://127.0.0.1:8888/codex/usage | python3 -m json.tool | head -20
# 期望: data.primary.used_percent 是数字

# 3) 从 Mac mini 跨机访问（要在 Mac mini 上跑，或叫用户跑）
# curl -s http://192.168.2.151:8888/codex/usage | head
```

如果本机 curl 通了但局域网不通，**几乎一定是 macOS 防火墙**：

```bash
# 检查防火墙状态
/usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate

# 如果开着，允许 python3 入站
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /usr/bin/python3
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp /usr/bin/python3
```

或者建议用户去**系统设置 → 网络 → 防火墙 → 选项 → 添加 python3 并允许入站连接**。

---

## 3. Air 必须保持唤醒（critical）

Air 合盖/睡眠 daemon 就停。用户希望这是 24/7 的指示器，需要至少做一件：

### 选项 A：禁止合盖睡眠（最省心）
```bash
sudo pmset -c sleep 0           # 充电时永不睡
sudo pmset -c disablesleep 1    # 合盖也不睡（需要 SIP 允许）
```

### 选项 B：用 caffeinate 包裹 daemon
改 plist `ProgramArguments` 头部加 `/usr/bin/caffeinate -d -i` 来防显示器+空闲睡眠。

### 选项 C：什么都不做，告诉用户"合盖就停"
最现实——Air 是笔记本，合盖即关。Mac mini 那条数据流不受影响。

**建议你做的**：执行 A 的 `sudo pmset -c sleep 0`（充电时不睡），然后告知用户合盖仍会停。

---

## 4. 报告给用户的格式

回报必须包含这 6 条：

1. `LocalHostName`（运行 `scutil --get LocalHostName` 拿到）
2. 当前 IP（`ipconfig getifaddr en0`）
3. daemon 自检 JSON 中的 `primary.used_percent` 和 `secondary.used_percent`
4. 从 **Mac mini 那边** curl Air 的命令的返回是否正常
5. 防火墙是否需要手动处理
6. 睡眠策略你选了 A/B/C 的哪个

模板：

```
Air daemon 部署完成 ✓

Hostname: <xxx>.local
IP:       192.168.2.151
Primary:  XX% (5h)
Secondary: XX% (week)
跨机访问: <通 / 需开防火墙>
睡眠:     <已禁用 / 仍受合盖影响>
```

---

## 5. 你绝不要做的事

- ❌ 不要改 Stick 端的 firmware（`src/`、`platformio.ini`）——那是另一台机器的事
- ❌ 不要改 daemon 脚本本身（`tools/codex_usage_daemon.py`）——它是版本化的，所有 Mac 共用
- ❌ 不要 push 任何代码到 GitHub——用户拥有此仓库
- ❌ 不要触碰 `~/.codex/` 下任何文件，只能读
- ❌ Air 的 IP 现在 `192.168.2.151` 是 DHCP 分配的；如果你发现实际不是这个值，**别擅自改 wifi_config.h**，把新 IP 报给用户让他决定要不要做 DHCP 绑定或者改 firmware

---

## 6. 如果遇到本文没覆盖的问题

写明：
1. 你跑了什么命令
2. 报错原文（粘贴 stderr）
3. 你试图诊断的方向

然后停下，让用户决定下一步。**不要自己 hack 代码绕过问题**。

---

完成后跑一句让用户拷贝即可看到状态的命令：
```bash
curl -s http://192.168.2.151:8888/codex/usage | python3 -c "import sys,json;d=json.load(sys.stdin)['data'];print(f\"5h: {d['primary']['used_percent']}%  week: {d['secondary']['used_percent']}%\")"
```
