# AI Agent Gateway 一键启动与验证闭环指南

本指南详细介绍了如何在 Windows + WSL2 混合开发环境中一键配置、启动并完成 AI Agent Gateway (v2.1) 系统全链路闭环验证的操作步骤。

---

## 🛠️ 1. 环境准备与编译

### A. 管控面 (Node.js) 依赖安装
在 Windows 或 WSL2 中进入项目 `control_plane` 目录，安装运行所需的 npm 依赖（包括 `express`, `ws`, `sqlite3` / `sql.js` 等）：
```bash
cd control_plane
npm install
```

### B. 数据面 (C++ 引擎) 编译
在 WSL2 中，进入项目根目录的 `build` 文件夹进行编译：
```bash
cd build
cmake ..
make
```
编译成功后，会生成二进制文件 `./AIAgentGateway`。

---

## 🚀 2. 服务启动顺序

为了保证 C++ 数据面在启动时能够顺利向 Node.js 管控面注册自身并维持心跳，**必须先启动管控面，再启动数据面。**

### 第一步：启动 Node.js 管控面
在 `control_plane` 目录下启动 Express 后台服务：
```bash
node server.js
```
*   管控面服务将监听在 **`http://localhost:3000`**。
*   静态前端页面将托管在同一端口，可直接用浏览器访问。

### 第二步：获取 WSL 虚拟网关 IP 并启动 C++ 引擎
由于 C++ 引擎运行在 WSL2 的独立虚拟子网中，而 Node.js 运行在 Windows 宿主机上，C++ 引擎需要向 Windows 的 IP 触发 Webhook 回调。

可以使用以下 **PowerShell 脚本** 自动提取虚拟网关 IP，设置必要的环境变量（`DEEPSEEK_API_KEY`、`CONTROL_HOST`、`CONTROL_PORT`），并一键在 WSL 中拉起 C++ 网关引擎：

```powershell
# 在 Windows PowerShell 中执行本段命令：
$gateway = '127.0.0.1'
wsl ip route | Where-Object { $_ -match 'default via' } | ForEach-Object {
    if ($_ -match 'default via ([\d\.]+)') { $gateway = $Matches[1] }
}
Write-Host "检测到 Windows 宿主机在 WSL 中的网关 IP 为: $gateway"
wsl sh -c "export DEEPSEEK_API_KEY=dummy && export CONTROL_HOST=$gateway && export CONTROL_PORT=3000 && /home/lenovo/ai_agent_gateway/build/AIAgentGateway"
```
*注：如果不配置 `DEEPSEEK_API_KEY`，可设为 `dummy` 以使用 Mock 路由或进行短路与协同取消测试。*

---

## 🧪 3. 全链路验证闭环操作步骤

打开浏览器，访问 **[http://localhost:3000](http://localhost:3000)**，按照以下用例进行功能验证：

### 用例 1：实时负载 Telemetry 仪表盘 (US 1.2)
*   **操作**：观察网页顶部蓝色的 **“C++ 引擎实时负载仪表盘”**。
*   **预期**：当没有任务时，活跃线程数为 `0/4`，等待队列为 `0`；在高并发发送任务时，进度条与数字会实时变动，展示排队与并发状况。

### 用例 2：意图短路拦截测试 (US 3.1)
*   **操作**：在聊天框中发送 `"你好"` 或 `"打开商店"`。
*   **预期**：管控面直接命中 `InterceptRule` 规则，页面上会瞬间展示出默认回复（例如 `"你好！我是调度中台托管的智能 NPC..."`），任务状态为 `Done`，耗时接近 0ms，不会向 C++ 引擎转发，节省 API 成本。

### 用例 3：任务协同取消测试 (US 1.3)
*   **操作**：发送一条普通聊天语句（如 `"帮我写一段冒泡排序代码"`），在气泡下方显示“处理中...”时，**立刻点击“强制中断任务”按钮**。
*   **预期**：
    *   前端气泡状态更新为：`Failed (任务已被用户强制终止 (Cooperative Cancelled))`。
    *   C++ 引擎终端输出：`[Worker] Task X was cancelled during sleep delay. Aborting.`
    *   工作线程感知到取消，放弃执行并自动优雅退出，不污染上下文记忆。

### 用例 4：分钟级 Token 防浪涌限流 (US 3.3)
*   **操作**：在 NPC 人设的 `max_tokens_per_min` 限制较低时（例如 200 字符），复制一大段长文本（大于 200 字符）发送。
*   **预期**：网页弹窗拦截并提示 `429 Too Many Requests: 分钟级 Token 限流拦截...`，该请求被管控面直接抛弃，不进入调度队列。

### 用例 5：人设背景导入与快照回滚 (US 2.2 & 2.3)
*   **操作**：
    1.  在顶部“背景故事导入”框中粘贴 JSON 背景，如 `{"background": "在大裂变战役中荣获了大十字军勋章。"}` 并点击“导入背景故事”。
    2.  观察“版本回滚”下拉框，此时已自动生成版本 `v2`。
    3.  在下拉框中选择 `v1` 并点击“执行回滚”。
*   **预期**：后台数据库 `NpcProfileVersion` 记录快照，回滚操作成功执行并重置 NPC 设定，NPC 恢复至未导入背景故事时的状态。

### 用例 6：动态状态机切换与 Prompt 修饰 (US 3.2)
*   **操作**：
    1.  在对话框输入 `"拔剑！攻击我吧！"`。
    2.  观察 NPC 头像旁边的 Badge 指示灯由绿色的 `Normal` 转变为红色闪烁的 `Combat`。
    3.  再次发送消息，观察 C++ 接收到的提示词会自动混入战斗态修饰语，使其语气充满战意。
    4.  输入 `"住手，我要和平"`，指示灯恢复为绿色 `Normal`，Prompt 修饰语被移除。

### 用例 7：对话交互日志导出 (US 4.3)
*   **操作**：点击页面右上角的 **“导出日志”** 链接。
*   **预期**：浏览器自动下载 `dialogue_tasks.json` 文件，其中包含历史任务的详细记录。
