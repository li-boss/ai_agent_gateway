const express = require('express');
const cors = require('cors');
const http = require('http');
const WebSocket = require('ws');
const axios = require('axios');
const initSqlJs = require('sql.js');
const fs = require('fs');
const path = require('path');

const app = express();
app.use(cors());
app.use(express.json());

// Serve static frontend files
app.use(express.static(path.join(__dirname, 'frontend')));

// Config
const PORT = process.env.PORT || 3000;
const ENGINE_HOST = process.env.ENGINE_HOST || '127.0.0.1';
const ENGINE_PORT = process.env.ENGINE_PORT || '8080';

const dbPath = path.join(__dirname, 'gateway.db');

// Create HTTP Server
const server = http.createServer(app);

// Create WebSocket Server
const wss = new WebSocket.Server({ server });

// Broadcast helper
function broadcast(data) {
  const message = JSON.stringify(data);
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

wss.on('connection', (ws) => {
  console.log('Frontend WebSocket client connected.');
  ws.send(JSON.stringify({ type: 'connection', status: 'connected' }));

  ws.on('close', () => {
    console.log('Frontend WebSocket client disconnected.');
  });
});

// Session States for NPCs (US 3.2: State Machine)
// Key: `${userId}_${npcId}` -> Value: 'Normal' | 'Combat'
const sessionStates = {};

// Initialize sql.js database
initSqlJs().then(SQL => {
  let db;
  if (fs.existsSync(dbPath)) {
    try {
      const filebuffer = fs.readFileSync(dbPath);
      db = new SQL.Database(filebuffer);
      console.log('Loaded SQLite database from file.');
    } catch (err) {
      console.error('Failed to read db file, creating new memory database:', err.message);
      db = new SQL.Database();
    }
  } else {
    db = new SQL.Database();
    console.log('Created new SQLite database in memory.');
  }

  // Create tables
  db.run(`CREATE TABLE IF NOT EXISTS NpcProfile (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(64) NOT NULL,
    system_prompt TEXT,
    personality_tags VARCHAR(256),
    max_tokens_per_min INTEGER DEFAULT 100,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  db.run(`CREATE TABLE IF NOT EXISTS DialogueTask (
    task_id INTEGER PRIMARY KEY AUTOINCREMENT,
    npc_id INTEGER,
    user_id INTEGER,
    prompt TEXT,
    result TEXT,
    status VARCHAR(16) DEFAULT 'Pending',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  // US 2.3: NPC Profile Versions table
  db.run(`CREATE TABLE IF NOT EXISTS NpcProfileVersion (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    npc_id INTEGER,
    system_prompt TEXT,
    version INTEGER,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  // US 3.1: Intercept Rules table (Short-circuiting)
  db.run(`CREATE TABLE IF NOT EXISTS InterceptRule (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    trigger_word VARCHAR(64) NOT NULL,
    response_text TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  // Helper to save DB to disk
  function saveDb() {
    try {
      const data = db.export();
      const buffer = Buffer.from(data);
      fs.writeFileSync(dbPath, buffer);
    } catch (err) {
      console.error('Failed to save database to disk:', err.message);
    }
  }

  // Helper to get multiple rows
  function getRows(sql, params = []) {
    const stmt = db.prepare(sql);
    stmt.bind(params);
    const rows = [];
    while (stmt.step()) {
      rows.push(stmt.getAsObject());
    }
    stmt.free();
    return rows;
  }

  // Helper to get a single row
  function getRow(sql, params = []) {
    const rows = getRows(sql, params);
    return rows[0] || null;
  }

  // Seed default NPCs, NPC Versions, and Intercept Rules if tables are empty
  try {
    const resNpc = db.exec("SELECT COUNT(*) as count FROM NpcProfile");
    const countNpc = resNpc[0] ? resNpc[0].values[0][0] : 0;
    if (countNpc === 0) {
      // Seed NPC profiles
      const npcsToSeed = [
        { name: "小林 (严谨程序员)", prompt: "你是一个非常专业且严谨的 C++ 程序员 NPC，用语简练，热衷于提供规范的代码解答。", tags: "严谨, 专业, 程序员" },
        { name: "阿尔萨斯 (荣耀战士)", prompt: "你是一个高傲威严的魔兽战士 NPC，说话带有强烈的战斗使命感与狂热荣耀感。", tags: "高傲, 战斗, 战士" },
        { name: "爱丽丝 (赛博导游)", prompt: "你是一个活泼热情的未来城市向导，喜欢用赛博朋克的术语和幽默的语调来解释一切。", tags: "活泼, 科技感, 导游" }
      ];

      npcsToSeed.forEach(npc => {
        db.run(
          "INSERT INTO NpcProfile (name, system_prompt, personality_tags, max_tokens_per_min) VALUES (?, ?, ?, 200)",
          [npc.name, npc.prompt, npc.tags]
        );
        const lastIdRow = getRow("SELECT last_insert_rowid() as id");
        // Save first version history
        db.run(
          "INSERT INTO NpcProfileVersion (npc_id, system_prompt, version) VALUES (?, ?, 1)",
          [lastIdRow.id, npc.prompt]
        );
      });
      console.log('Seeded default NPC profiles & version histories.');
    }

    const resRules = db.exec("SELECT COUNT(*) as count FROM InterceptRule");
    const countRules = resRules[0] ? resRules[0].values[0][0] : 0;
    if (countRules === 0) {
      db.run("INSERT INTO InterceptRule (trigger_word, response_text) VALUES (?, ?)", ["你好", "你好！我是调度中台托管的智能 NPC，很高兴为您服务。"]);
      db.run("INSERT INTO InterceptRule (trigger_word, response_text) VALUES (?, ?)", ["打开商店", "【系统指令】正在为您打开游戏内置道具商店，请稍候..."]);
      db.run("INSERT INTO InterceptRule (trigger_word, response_text) VALUES (?, ?)", ["再见", "愿圣光/代码与你同在，期待我们下一次相遇。"]);
      console.log('Seeded default short-circuit intercept rules.');
    }
    saveDb();
  } catch (err) {
    console.error('Error seeding tables:', err.message);
  }

  // API Routes

  // 1. Get C++ Engine Online Status (US 1.1)
  app.get('/api/engine/status', async (req, res) => {
    try {
      const response = await axios.get(`http://${ENGINE_HOST}:${ENGINE_PORT}/ping`, { timeout: 1500 });
      if (response.data === 'pong') {
        return res.json({ online: true });
      }
      res.json({ online: false });
    } catch (error) {
      res.json({ online: false });
    }
  });

  // 1.2 Get C++ Engine Load/Telemetry Info (US 1.2)
  app.get('/api/engine/load', async (req, res) => {
    try {
      const response = await axios.get(`http://${ENGINE_HOST}:${ENGINE_PORT}/status`, { timeout: 1500 });
      res.json(response.data);
    } catch (error) {
      res.status(502).json({ error: 'C++ 引擎服务连接失败，无法获取负载指标。' });
    }
  });

  // 2. Get NPC List
  app.get('/api/npcs', (req, res) => {
    try {
      const rows = getRows("SELECT * FROM NpcProfile ORDER BY id ASC");
      res.json(rows);
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // 3. Create NPC Profile with Version tracking (US 2.1 & 2.3)
  app.post('/api/npcs', (req, res) => {
    const { name, system_prompt, personality_tags, max_tokens_per_min } = req.body;
    if (!name || !system_prompt) {
      return res.status(400).json({ error: 'Name and system prompt are required.' });
    }
    try {
      db.run(
        "INSERT INTO NpcProfile (name, system_prompt, personality_tags, max_tokens_per_min, updated_at) VALUES (?, ?, ?, ?, datetime('now'))",
        [name, system_prompt, personality_tags || '', max_tokens_per_min || 200]
      );
      saveDb();
      const lastIdRow = getRow("SELECT MAX(id) as id FROM NpcProfile");
      const npcId = lastIdRow.id;

      // Save initial version
      db.run(
        "INSERT INTO NpcProfileVersion (npc_id, system_prompt, version) VALUES (?, ?, 1)",
        [npcId, system_prompt]
      );
      saveDb();

      res.status(201).json({
        id: npcId,
        name,
        system_prompt,
        personality_tags,
        max_tokens_per_min: max_tokens_per_min || 200
      });
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // US 2.2: Import JSON Background Story
  app.post('/api/npcs/:id/import', (req, res) => {
    const npcId = parseInt(req.params.id);
    const { background } = req.body;
    if (!background) {
      return res.status(400).json({ error: '背景故事内容 (background) 不能为空' });
    }

    try {
      const npc = getRow("SELECT * FROM NpcProfile WHERE id = ?", [npcId]);
      if (!npc) {
        return res.status(404).json({ error: 'NPC 档案未找到。' });
      }

      const updatedPrompt = npc.system_prompt + "\n\n【导入背景设定】\n" + background;
      
      // Update profile
      db.run(
        "UPDATE NpcProfile SET system_prompt = ?, updated_at = datetime('now') WHERE id = ?",
        [updatedPrompt, npcId]
      );

      // Increment version history
      const maxVerRow = getRow("SELECT MAX(version) as version FROM NpcProfileVersion WHERE npc_id = ?", [npcId]);
      const nextVersion = (maxVerRow && maxVerRow.version ? maxVerRow.version : 0) + 1;
      
      db.run(
        "INSERT INTO NpcProfileVersion (npc_id, system_prompt, version) VALUES (?, ?, ?)",
        [npcId, updatedPrompt, nextVersion]
      );
      
      saveDb();
      res.json({ success: true, version: nextVersion, system_prompt: updatedPrompt });
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // US 2.3: Get NPC Version History
  app.get('/api/npcs/:id/versions', (req, res) => {
    const npcId = parseInt(req.params.id);
    try {
      const rows = getRows("SELECT * FROM NpcProfileVersion WHERE npc_id = ? ORDER BY version DESC", [npcId]);
      res.json(rows);
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // US 2.3: Rollback NPC to a Specific Version
  app.post('/api/npcs/:id/rollback', (req, res) => {
    const npcId = parseInt(req.params.id);
    const { version } = req.body;
    if (!version) {
      return res.status(400).json({ error: '版本号 (version) 不能为空。' });
    }

    try {
      const versionRow = getRow("SELECT * FROM NpcProfileVersion WHERE npc_id = ? AND version = ?", [npcId, version]);
      if (!versionRow) {
        return res.status(404).json({ error: '没有找到对应的历史版本记录。' });
      }

      db.run(
        "UPDATE NpcProfile SET system_prompt = ?, updated_at = datetime('now') WHERE id = ?",
        [versionRow.system_prompt, npcId]
      );

      // Save a new version tracking snapshot for the rollback action itself
      const maxVerRow = getRow("SELECT MAX(version) as version FROM NpcProfileVersion WHERE npc_id = ?", [npcId]);
      const nextVersion = (maxVerRow && maxVerRow.version ? maxVerRow.version : 0) + 1;

      db.run(
        "INSERT INTO NpcProfileVersion (npc_id, system_prompt, version) VALUES (?, ?, ?)",
        [npcId, versionRow.system_prompt, nextVersion]
      );

      saveDb();
      res.json({ success: true, version: nextVersion, system_prompt: versionRow.system_prompt });
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // 4. Submit Dialogue Task (Optimized with immediately return, rate limiting, and state machines)
  app.post('/api/task', (req, res) => {
    const { npc_id, prompt, user_id } = req.body;
    if (!npc_id || !prompt) {
      return res.status(400).json({ error: 'npc_id and prompt are required.' });
    }

    const uid = user_id || 1;
    const sessionKey = `${uid}_${npc_id}`;

    try {
      // Fetch NPC
      const npc = getRow("SELECT * FROM NpcProfile WHERE id = ?", [npc_id]);
      if (!npc) {
        return res.status(404).json({ error: 'NPC not found.' });
      }

      // --- 1. US 3.3: Token Rate Limiting (防浪涌) ---
      // Estimate token count as characters (prompt + result) in the last 60 seconds
      const oneMinAgo = new Date(Date.now() - 60000).toISOString().replace('T', ' ').substring(0, 19);
      const recentTasks = getRows(
        "SELECT prompt, result FROM DialogueTask WHERE npc_id = ? AND created_at >= ?",
        [npc_id, oneMinAgo]
      );
      
      let estimatedTokens = 0;
      recentTasks.forEach(t => {
        estimatedTokens += t.prompt.length + (t.result ? t.result.length : 0);
      });

      const currentLimit = npc.max_tokens_per_min || 200;
      if (estimatedTokens + prompt.length > currentLimit) {
        console.warn(`NPC ${npc_id} rate limited! Consumed: ${estimatedTokens}, incoming: ${prompt.length}, limit: ${currentLimit}`);
        return res.status(429).json({ 
          error: `分钟级 Token 限流拦截：该 NPC 过去 1 分钟已消耗约 ${estimatedTokens} 词/字符，新请求字数 ${prompt.length} 已超出每分钟限额 (${currentLimit})，请稍后再试！` 
        });
      }

      // --- 2. US 3.2: State Machine Condition Switcher ---
      let activeState = sessionStates[sessionKey] || 'Normal';
      let statePromptModifier = '';

      // Match trigger conditions for combat state transition
      if (prompt.match(/(攻击|攻击你|砍你|打你|拔剑|受击|决斗|战斗|去死)/i)) {
        sessionStates[sessionKey] = 'Combat';
        activeState = 'Combat';
      } else if (prompt.match(/(和平|停下|逃跑|认输|住手|原谅)/i)) {
        sessionStates[sessionKey] = 'Normal';
        activeState = 'Normal';
      }

      // Apply modifiers based on state machine
      if (activeState === 'Combat') {
        statePromptModifier = "\n【NPC当前进入受击/敌对战斗状态：请放弃平时的礼貌或冷静，使用战斗狂热、愤怒、警觉或充满战意的语气进行防守性、威胁性或反击性的回复！】\n";
      }

      // --- 3. US 3.1: Local Short-circuit Intercept ---
      // Check if prompt matches local rules exactly (ignoring spaces/case)
      const sanitizedPrompt = prompt.trim();
      const rules = getRows("SELECT * FROM InterceptRule");
      let matchedRule = null;
      for (const rule of rules) {
        if (sanitizedPrompt.includes(rule.trigger_word)) {
          matchedRule = rule;
          break;
        }
      }

      if (matchedRule) {
        console.log(`Short-circuit rule matched: "${matchedRule.trigger_word}" -> "${matchedRule.response_text}"`);
        
        // Write finished task to DB
        db.run(
          "INSERT INTO DialogueTask (npc_id, user_id, prompt, result, status) VALUES (?, ?, ?, ?, 'Done')",
          [npc_id, uid, prompt, matchedRule.response_text]
        );
        saveDb();

        const lastIdRow = getRow("SELECT MAX(task_id) as id FROM DialogueTask");
        const taskId = lastIdRow.id;

        // Return 202 Accepted immediately
        res.status(202).json({ taskId, status: 'Pending', state: activeState });

        // Asynchronously push Done status to simulate engine callback
        setImmediate(() => {
          broadcast({
            type: 'task_update',
            taskId: taskId,
            status: 'Done',
            result: matchedRule.response_text,
            state: activeState,
            isShortCircuited: true
          });
        });
        return;
      }

      // --- 4. C++ Asynchronous Loop: Fix Response Order ---
      // Insert Pending task in DB
      db.run(
        "INSERT INTO DialogueTask (npc_id, user_id, prompt, status) VALUES (?, ?, ?, 'Pending')",
        [npc_id, uid, prompt]
      );
      saveDb();

      const lastIdRow = getRow("SELECT MAX(task_id) as id FROM DialogueTask");
      const taskId = lastIdRow.id;

      // 4.1 Return 202 status IMMEDIATELY (🔴 Bug Fix)
      res.status(202).json({ taskId, status: 'Pending', state: activeState });

      // 4.2 Forward request to C++ engine in background
      // Inject state machine modifier prompt if applicable
      const promptToSend = statePromptModifier + prompt;

      axios.post(`http://${ENGINE_HOST}:${ENGINE_PORT}/agent/task`, {
        taskId: taskId,
        userId: uid,
        prompt: promptToSend,
        npcId: parseInt(npc_id)
      }, { timeout: 4000 }) // Added timeout to prevent hanging (🟡 Optimization)
      .then(response => {
        console.log(`C++ engine accepted task ${taskId}.`);
      })
      .catch(error => {
        console.error(`Failed to forward task ${taskId} to C++ engine:`, error.message);
        
        db.run(
          "UPDATE DialogueTask SET status = 'Failed', result = ?, updated_at = datetime('now') WHERE task_id = ?",
          [`引擎转发失败或超时: ${error.message}`, taskId]
        );
        saveDb();

        broadcast({
          type: 'task_update',
          taskId: taskId,
          status: 'Failed',
          result: `引擎转发失败: ${error.message}`,
          state: activeState
        });
      });

    } catch (err) {
      console.error(err);
      res.status(500).json({ error: err.message });
    }
  });

  // 5. Webhook endpoint called by C++ Engine
  app.post('/api/webhook', (req, res) => {
    const { taskId, result } = req.body;
    if (!taskId || !result) {
      return res.status(400).json({ error: 'taskId and result are required.' });
    }

    console.log(`Received Webhook result for task ${taskId}. Updating status...`);

    try {
      db.run(
        "UPDATE DialogueTask SET status = 'Done', result = ?, updated_at = datetime('now') WHERE task_id = ?",
        [result, taskId]
      );
      saveDb();

      // Retrieve NPC associated with this task to find the current active state
      const taskRow = getRow("SELECT npc_id, user_id FROM DialogueTask WHERE task_id = ?", [taskId]);
      let activeState = 'Normal';
      if (taskRow) {
        const sessionKey = `${taskRow.user_id}_${taskRow.npc_id}`;
        activeState = sessionStates[sessionKey] || 'Normal';
      }

      // Broadcast result to frontend clients via WebSockets
      broadcast({
        type: 'task_update',
        taskId: taskId,
        status: 'Done',
        result: result,
        state: activeState
      });

      res.status(200).json({ success: true });
    } catch (err) {
      console.error(`Database error updating task ${taskId}:`, err.message);
      res.status(500).json({ error: err.message });
    }
  });

  // US 1.3: Cancel running task cooperatively
  app.post('/api/tasks/:taskId/cancel', (req, res) => {
    const taskId = parseInt(req.params.taskId);
    try {
      const task = getRow("SELECT * FROM DialogueTask WHERE task_id = ?", [taskId]);
      if (!task) {
        return res.status(404).json({ error: 'Task not found.' });
      }

      if (task.status !== 'Pending' && task.status !== 'Processing') {
        return res.status(400).json({ error: 'Only Pending or Processing tasks can be cancelled.' });
      }

      // 1. Update status to Failed in local database
      db.run(
        "UPDATE DialogueTask SET status = 'Failed', result = '任务已被用户强制终止 (Cooperative Cancelled)', updated_at = datetime('now') WHERE task_id = ?",
        [taskId]
      );
      saveDb();

      // 2. Notify C++ engine to drop task execution
      axios.post(`http://${ENGINE_HOST}:${ENGINE_PORT}/agent/cancel`, { taskId }, { timeout: 1500 })
        .then(() => console.log(`Notified C++ engine to cancel task ${taskId}`))
        .catch(err => console.warn(`Failed to notify C++ cancel endpoint (might be offline): ${err.message}`));

      // 3. Broadcast cancellation
      broadcast({
        type: 'task_update',
        taskId: taskId,
        status: 'Failed',
        result: '任务已被用户强制终止 (Cooperative Cancelled)'
      });

      res.json({ success: true });
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // US 4.3: Export Dialogue Logs
  app.get('/api/tasks/export', (req, res) => {
    try {
      const rows = getRows("SELECT * FROM DialogueTask ORDER BY created_at DESC");
      res.setHeader('Content-Type', 'application/json');
      res.setHeader('Content-Disposition', 'attachment; filename=dialogue_tasks.json');
      res.send(JSON.stringify(rows, null, 2));
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // 6. Query Task Status (Polling Fallback)
  app.get('/api/tasks/:taskId', (req, res) => {
    const taskId = req.params.taskId;
    try {
      const row = getRow("SELECT * FROM DialogueTask WHERE task_id = ?", [taskId]);
      if (!row) {
        return res.status(404).json({ error: 'Task not found.' });
      }
      res.json(row);
    } catch (err) {
      res.status(500).json({ error: err.message });
    }
  });

  // Start Server
  server.listen(PORT, '0.0.0.0', () => {
    console.log(`Control Plane Server is running on port ${PORT}`);
    console.log(`C++ Engine Target configured at http://${ENGINE_HOST}:${ENGINE_PORT}`);
  });
}).catch(err => {
  console.error("SQL.js initialization failed:", err);
});
