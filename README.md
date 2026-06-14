# AI Agent Gateway (C++ 高性能网关与 Node.js 行为管控平台 v2.1)

🚀 **一个基于 C++ 原生异步调度与 Node.js 规则管控的混合架构智能体中台系统。**

本项目在原有高性能 C++ 异步网关的基础上，全新引入了 `control_plane` 行为管控面。通过 **C++ 高并发数据面** 与 **Node.js 规则管理面** 的联动，不仅实现了极低延迟的多用户上下文隔离管理，更提供了敏感词拦截、防浪涌限流、人设快照版本管理、动态战斗/常规状态机切换及多级协同任务取消等工业级智能体中台特性。

---

## 🏗️ 整体混合架构 (Hybrid Architecture)

系统由**数据面 (C++ 引擎)**与**管控面 (Node.js)**双层架构组成：

```mermaid
graph TD
    User((Web 前端沙盒)) -->|1. 提交任务 /api/task| Express[Node.js 管控面 server.js]
    Express -->|2. 短路拦截 / 限流| Express
    
    subgraph 数据面 (C++ Gateway Engine)
        HttpServer[HttpServer / 8080] -->|3. 队列派发| ThreadPool[ThreadPool 线程池]
        ThreadPool -->|4. 协同取消检查| Task[Worker 线程任务]
        Task <-->|5. 读写上下文| LRU[(LRU 记忆池<br>O1哈希+双向链表)]
        Task -->|6. 请求推理| DeepSeek((DeepSeek API))
        Task -->|7. 完成后回调 Webhook| WebhookClient[WebhookClient]
    end

    Express -->|8. 发送调度请求| HttpServer
    WebhookClient -->|9. 推送结果| Express
    Express -->|10. 实时推送结果 / 负载| User
```

---

## ✨ 核心特性 (Core Features)

### 1. ⚙️ C++ 高性能数据面 (Data Plane)
*   **⚡ 高吞吐异步调度 (Asynchronous Dispatching)**：基于 C++11 原生 `std::thread` 构建自定义任务队列，后台工作线程池全异步接管大模型网络 I/O，极大地提高了并发处理上限。
*   **🧠 O(1) 线程安全记忆池 (LRU Cache)**：针对多轮对话的 Token 膨胀痛点，基于 `std::unordered_map` 与双向链表手写了工业级 LRU 缓存淘汰引擎，确保高并发下的上下文严格隔离。
*   **📝 RAII 工业级双写日志库 (Dual-write Logger)**：自主实现基于 RAII 生命周期的线程安全日志组件，支持控制台 ANSI 颜色高亮与本地 `gateway.log` 文件自动追加。
*   **🛡️ 健壮的网络防腐层与组装逻辑**：封装底层 HTTPS 握手与大模型鉴权，内置超时熔断与错误隔离机制。

### 2. 🎛️ Node.js 行为管控面 (Control Plane)
*   **🔴 意图短路拦截 (Intent Short-circuiting)**：维护本地规则表 `InterceptRule`。对于命中预设 trigger 词（如 `"你好"`, `"打开商店"`）的请求，管控面在本地 1ms 内直接生成固定响应并置状态为 `Done`，节省大模型 API 成本。
*   **🔴 分钟级 Token 限流 (Rate Limiting)**：实时估算 NPC 在过去 60 秒内已消耗的字符数。一旦超出 `max_tokens_per_min` 的限额，立刻返回 `429 Too Many Requests`。
*   **🔴 协作式多级任务取消 (Cooperative Cancellation)**：支持在任务排队或大模型调用前被中途强行终止。Node.js 触发 C++ `/agent/cancel` 接口，C++ 工作线程在多个关键节点进行**三级协同检查**并优雅退出。
*   **档案版本控制与快照回滚 (Profile Versioning & Rollback)**：自动将 NPC 的人设修改记录归档入 `NpcProfileVersion` 快照表中。支持查看人设历史版本并执行一键回滚。
*   **JSON 背景故事导入 (Context Injection)**：提供 API 导入复杂 NPC 背景材料，将其拼接到 system prompt 中，同时自动递增人设版本号。
*   **动态状态机状态转移 (State Machine Switcher)**：内置常规 (`Normal`) 与战斗 (`Combat`) 状态机。当用户输入 `"拔剑"`, `"攻击"` 等战斗词汇时，状态机自动切换至战斗态，并在 prompt 中动态混入愤怒、充满战意的语气修饰，同时前端指示灯变红闪烁。
*   **引擎实时负载 Telemetry 仪表盘 (Real-time Telemetry)**：C++ 引擎通过 `/status` 暴露线程池活跃数与等待队列。管控面代理该接口，前端进行实时轮询与进度条渲染。
*   **对话交互日志导出 (Dialogue Logging Export)**：一键将所有交互日志打包为 JSON 格式并支持前端下载。
*   **引擎节点注册与状态监控**：支持引擎节点启动时自动注册及连通性心跳监测。

---

## 📂 核心文件清单

```text
ai_agent_gateway/
├── include/core/
│   ├── ThreadPool.h       # C++ 线程池（支持获取活跃线程及队列数）
│   ├── HttpServer.h       # C++ HTTP 引擎服务（提供 /status 负载及 /agent/cancel 取消端点）
│   ├── WebhookClient.h    # C++ 异步 Webhook 回调客户端
│   ├── MemoryStorage.h    # C++ LRU 记忆引擎
│   ├── LLMClient.h        # C++ 大模型 HTTP 封装
│   └── Logger.h           # RAII 双写日志组件
├── src/
│   └── main.cpp           # 数据面引擎启动主入口
├── control_plane/
│   ├── server.js          # Node.js Express 后端路由、限流与行为树管理
│   ├── package.json       # 管控面 npm 依赖配置
│   └── frontend/
│       └── index.html     # 前端 UI 沙盒界面（Vue 3）
├── STARTUP.md             # 一键服务启动与全链路闭环验证指南
└── walkthrough.md         # 7项核心功能的联调验证测试报告
```

---

## 🚀 快速开始

关于如何配置环境变量、编译 C++ 引擎、启动管控面与数据面以及进行全链路闭环验证，请参考专门的：
👉 **[STARTUP.md (一键启动与验证指南)](file:///wsl.localhost/Ubuntu/home/lenovo/ai_agent_gateway/STARTUP.md)**

关于系统针对短路拦截、协同取消、限流防浪涌、状态机切换等功能的实际运行结果与测试截屏日志，请参考：
👉 **[walkthrough.md (交付验证报告)](file:///wsl.localhost/Ubuntu/home/lenovo/ai_agent_gateway/walkthrough.md)**