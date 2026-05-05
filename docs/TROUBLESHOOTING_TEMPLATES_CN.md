# NCEditor 高频故障 3 步排查模板

## A. 100MB 场景卡顿/抖动

### 第 1 步：先看参数是否过激

- 检查运行时参数：
  - `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT`
  - `NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB`
  - `NCEDITOR_SEARCH_DEBOUNCE_MS`
  - `NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS`
- 先用默认值或标准机档位验证，不先动结构代码。

### 第 2 步：确认瓶颈在渲染还是查找

- 渲染侧看 `PaintedEditorItem`（可视区渲染、水平滚动、高亮刷新）。
- 查找侧看 `DocumentSession::rebuildSearchCache/startQueuedSearch/computeSearch`。
- 结论要求：明确“是画得慢”还是“算得慢”。

### 第 3 步：最小改动原则

- 优先：调参数。
- 次选：局部节流/预算调整（例如可视区高亮预算）。
- 最后：结构性优化（仅在前两步无效时）。

## B. 内存不足/异常（bad_alloc / QUnhandledException）

### 第 1 步：先判定是否触发保护阈值

- 打开限制：`NCEDITOR_MAX_OPEN_FILE_MB`
- 文档限制：`NCEDITOR_MAX_DOCUMENT_MB`
- 粘贴限制：`NCEDITOR_PASTE_LIMIT_KB`
- 查找高亮上限：`NCEDITOR_SEARCH_HIGHLIGHT_LIMIT`

### 第 2 步：确认是否走低内存路径

- 打开：优先 `QFile::map` 解码路径。
- 保存：分段编码写盘 + `QSaveFile` 提交路径。
- 查找：大文件走分段快照搜索而非整文拼接。

### 第 3 步：再做结构修复

- 若仍异常，再看编辑异常恢复路径与回退策略。
- 修复后至少回归：
  - 编辑 -> 保存 -> 关闭 -> 再打开（多轮）
  - 查找高重复内容（大量匹配）

## C. 生命周期泄漏（关闭后内存不降）

### 第 1 步：先验证“对象是否真正释放”

- 复现步骤固定：
  - 打开大文件 -> 编辑 -> 保存 -> 关闭 -> 重复多轮
- 观察：
  - 关闭后再开 100MB 是否更容易 OOM
  - 关闭后是否仍有对应文档行为回调

### 第 2 步：检查信号连接捕获关系

- 优先排查是否存在 `QSharedPointer` 强捕获 lambda。
- 优先改成 `QWeakPointer` 或裸指针（有上下文对象约束）。

### 第 3 步：检查 disconnect 时机

- 关闭窗口路径是否覆盖：
  - 槽位替换
  - 槽位关闭
  - 文档注销
- 验证 `unregisterSession` 是否同时清理连接与映射。

## 通用记录模板（每次排障都填）

- 现象：
- 复现步骤：
- 首先命中的责任层（UI/编排/文档/缓冲/索引）：
- 已验证函数入口：
- 最小改动方案：
- 回归结果：
- 是否影响对外行为兼容：

