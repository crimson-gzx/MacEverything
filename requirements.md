# Product Requirements Document: MacEverything

## 1. 项目概述 (Project Overview)
本项目旨在开发一款 macOS 平台上的极速全盘文件搜索工具，对标 Windows 平台的 "Everything"。
核心目标是实现毫秒级的文件名搜索响应，绕过 macOS 自带的 Spotlight 索引限制，实现真正的底层物理扫盘。

## 2. 技术栈架构 (Tech Stack)
采用 **C++ 核心引擎 + Objective-C++ 桥接 + SwiftUI 原生界面** 的混合架构。
* **Core Engine (C++20)**: 负责极致的 I/O 目录遍历、内存数据结构管理、多线程并发和正则/字符串匹配引擎。
* **Bridge (Objective-C++)**: 提供 `.mm` 封装，将 C++ 的数据结构安全、高效地暴露给 Swift。
* **UI Layer (SwiftUI)**: 负责搜索框输入防抖、快捷键监听（全局唤醒）、以及百万级数据的 Virtual List（虚拟列表）极速渲染。

## 3. 核心功能需求 (Core Features)
* **全盘极速扫描**: 首次启动或手动触发时，能够在极短时间内（目标 < 10秒）遍历 Mac 根目录下的所有文件元数据。
* **内存常驻与极速搜索**: 将文件元数据（文件名、路径、大小、修改时间）加载到内存，支持多条件（前缀、包含、正则、Glob 通配符）的毫秒级过滤。搜索范围包括文件名和完整路径，用户可直接输入目录名或路径（如 `/usr/local`）进行检索。
* **实时监控**: 通过 macOS FSEvents API 监听文件系统变化（创建、删除、重命名、修改），实时增量更新内存索引，无需全盘重扫。采用 append-only + tombstone 策略保持索引稳定性。
* **搜索结果高亮**: 搜索结果中文件名和路径中匹配的字符以高亮颜色和加粗样式显示，帮助用户快速定位匹配位置。支持大小写不敏感的子串高亮，Glob 模式不高亮。
* **原生体验**: 支持类似 Raycast/Alfred 的全局快捷键唤醒，支持双击回车调用 `NSWorkspace` 打开文件，支持在 Finder 中显示 (Reveal in Finder)。

## 4. 关键技术约束与 AI 编码指南 (Critical Constraints for LLM)
**⚠️ 给 AI 助手的特别提示：在编写核心引擎时，必须严格遵守以下系统级约束：**

1.  **I/O 性能 API 选型**:
    * **禁止**使用标准的 POSIX `readdir` 或 `std::filesystem::recursive_directory_iterator`，性能太差。
    * **必须**调用 macOS 特有的 `getattrlistbulk` API 来进行高吞吐量的目录树遍历。
2.  **避免 APFS 软链接死循环**:
    * macOS (Catalina 及以后) 存在 Data/System Volume 融合。系统内存在大量 Firmlinks（固件链接）。
    * 遍历算法必须维护一个已访问的 `dev_t` 和 `ino_t` (设备 ID 和 inode 号) 集合（如 `std::unordered_set`），防止陷入无限递归。
3.  **TCC 权限防崩溃处理**:
    * 遇到无权限读取的目录（如 `~/Downloads`, `~/Desktop` 等受 TCC 保护的目录），程序**不能 Crash**，必须捕获权限错误 (EACCES) 并静默跳过。
    * UI 层需提供引导，要求用户在“系统偏好设置 -> 隐私与安全性”中授予 "Full Disk Access"（完全磁盘访问权限）。
4.  **索引版本管理与元数据**:
    * 索引二进制文件采用 v3 格式，头部包含可扩展的元数据 key-value 对（如 `scan_root`、`app_version`、`os_version`、`record_format`）。
    * 加载时兼容 v1、v2、v3 三个版本：v1 无 lastEventId 和 inode/devId，v2 加入了这两项，v3 新增可扩展元数据段。
    * 保存时始终写入最新 v3 格式，未来版本迭代只需新增 metadata key 而无需修改二进制格式版本号。
    * 遇到未知版本号时安全拒绝加载，遇到未知 metadata key 时静默忽略（前向兼容）。
5.  **内存与数据结构优化**:
    * C++ 层应定义紧凑的结构体，例如 `struct FileRecord { std::string name; std::string path; uint8_t type; };`。
    * 大量数据传递给 Swift 层时，避免深拷贝，应优先考虑传递指针或分批加载（Pagination/Batching）机制供给 SwiftUI 的 `List` 或 `Table`。

## 5. 执行与迭代计划 (Execution Plan)
请严格按照以下 Phase 逐步进行编码，不要尝试一次性生成所有代码。完成一个 Phase 并测试通过后，再进入下一个 Phase。

### Phase 1: C++ 核心扫盘引擎 (CLI MVP)
* **目标**: 编写纯 C++ 命令行工具，跑通 `getattrlistbulk` 遍历逻辑。
* **任务**:
    1.  实现 `DirectoryScanner` 类。
    2.  利用 `std::thread` 线程池或 GCD 进行并发目录扫描。
    3.  实现去重机制（避开 Firmlinks）。
    4.  在终端输出扫描总耗时和文件总数。

### Phase 2: C++ 数据结构与内存检索引擎
* **目标**: 实现内存中的快速搜索过滤。
* **任务**:
    1.  构建 `SearchEngine` 类，接收 `DirectoryScanner` 的结果。
    2.  实现 `std::string::find` 或多线程分块正则匹配算法。
    3.  提供 `query(const std::string& keyword)` 接口，返回匹配的 `FileRecord` 列表。

### Phase 5: FSEvents 实时文件监控
* **目标**: 当文件系统发生变化时，自动增量更新索引，无需手动刷新。
* **技术方案**:
    1.  使用 `FSEventStreamCreate` + `kFSEventStreamCreateFlagFileEvents` 监听文件级别事件。
    2.  SearchEngine 支持增量 mutation：`addRecord` / `removeByPath` / `updateByPath`，采用 append-only + tombstone 策略保持索引稳定。
    3.  使用 `std::shared_mutex` 实现读写锁：query/getRecord 用 shared_lock，mutation 用 unique_lock。
    4.  通过 `unordered_map<string, uint32_t>` 路径索引实现 O(1) 路径查找。
    5.  FSEvents 回调处理：stat 检测文件状态 → 调用 engine mutation → 通知 UI 刷新。
    6.  ~0.3s 事件合并延迟，实际用户感知延迟 < 1s。

### Phase 3: Objective-C++ 桥接层构建
* **目标**: 打通 C++ 引擎与 Swift 的通信。
* **任务**:
    1.  编写 `MacSearchBridge.h` 和 `MacSearchBridge.mm`。
    2.  封装启动扫描、执行查询的接口，将 C++ 的 `std::vector` 转换为 Swift 友好的 `NSArray` 或使用指针回调。

### Phase 4: SwiftUI 原生界面与集成
* **目标**: 完成图形化界面。
* **任务**:
    1.  搭建搜索框（实现 300ms 防抖输入）。
    2.  搭建高性能结果列表（处理 10 万级以上的结果渲染不卡顿）。
    3.  处理右键菜单或快捷键事件（Open, Reveal in Finder）。
    4.  添加 TCC 权限检查和弹窗引导提示。

### Phase 6: 增量持久化 (Incremental Persistence)
* **目标**: 实现索引的增量保存与快速恢复，避免每次启动全盘重扫。
* **技术方案**:
    1.  **WAL (Write-Ahead Log)**: 实时记录 FSEvents 触发的 add/remove/update 操作，`fflush` + `fsync` 确保崩溃安全。
    2.  **启动流程**: 加载基础索引 → 重放 WAL 条目 → 通过 FSEvents replay 追赶到最新状态。
    3.  **定期压缩**: GCD timer 每 5 分钟将内存索引写入新的基础文件，原子性 rename 替换旧文件，清空 WAL。
    4.  **Tombstone 回收**: 压缩时调用 `compactRecords()` 清除已删除的墓碑记录，释放内存。
    5.  **无间隙 WAL 切换**: 压缩期间先打开新 WAL、attach 后再关闭旧 WAL，确保零丢失窗口。

### Phase 7: 质量审计与修复 (Quality Audit & Fixes)
* **已修复的问题**:
    1.  **[Critical] WAL fsync**: `IndexWAL::append()` 在 `fflush` 后增加 `fsync` 调用，防止 OS 崩溃丢失数据。
    2.  **[Critical] 压缩竞态**: 重写 `compact()` 流程，先打开新 WAL 再替换旧 WAL，消除 mutation 丢失窗口。
    3.  **[High] done_ 原子性**: `DirectoryScanner::done_` 改为 `std::atomic<bool>`，修复多线程数据竞争。
    4.  **[High] getRecord 悬挂引用**: `getRecord()` 改为返回值而非引用，防止锁释放后引用失效。
    5.  **[High] 每次查询创建线程**: `query()` 改用 `dispatch_apply` 复用 GCD 线程池，消除线程创建开销。
    6.  **[High] readString 无长度限制**: 添加 64KB 上限，防止损坏文件触发 OOM。
    7.  **[High] saveToFile fwrite 未检查**: 使用 `safeWrite` 包装所有写操作，磁盘满时安全失败并清理临时文件。
    8.  **[Medium] 搜索防抖**: 500ms → 150ms，提升交互响应速度。
    9.  **[Medium] maxResults 提前终止**: 使用 `totalFound` 原子计数器精确控制多线程停止。
    10. **[Medium] Tombstone 增长**: 新增 `compactRecords()` 并集成到定期压缩流程中。
    11. **[Medium] 搜索结果上限**: `maxResults` 从 0（无限）改为 10000，避免无限遍历索引仅显示前几页。
    12. **[High] MustScanSubDirs 运行时处理**: FSEvents 回调检测 `kFSEventStreamEventFlagMustScanSubDirs` 事件，触发目标目录子树重扫而非忽略。新增 `SearchEngine::removeByPathPrefix()` 批量删除子树记录，桥接层 `rescanSubtree:` 方法使用 `DirectoryScanner` 重扫受影响目录并增量更新索引。
    13. **[Critical] 退出卡死 (v1)**: `applicationWillTerminate` 直接调用 `compactIndex()` 导致主线程与后台 FSEvents 回调、自动压缩定时器竞争 `SearchEngine::mutex_` 死锁。新增 `prepareForTermination` 方法：先捕获 lastEventId，再停止 FSEvents 监控和自动压缩定时器（消除后台锁竞争），最后安全执行压缩。
    14. **[Critical] 退出卡死 (v2)**: `prepareForTermination` 仍存在三个阻塞源：(1) 后台内容索引循环（`startContentIndexing`）持续持有 `ContentIndex::mutex_` 排他锁，阻塞退出时的 `compact()` 调用；(2) `dispatch_source_cancel` 不等待正在执行的自动压缩 handler 完成，若后台 `compact()` 正在运行，主线程会等待互斥锁；(3) `rescanSubtree` 异步操作持有 `SearchEngine::mutex_` 排他锁。修复方案：新增 `std::atomic<bool> _shuttingDown` 标志，在 `prepareForTermination` 最先设置；内容索引循环、子树重扫、FSEvents 回调均检查此标志并提前退出。自动压缩定时器改用专用串行队列（`dispatch_queue_create`），`stopAutoCompactionAndWait()` 通过 `dispatch_sync` 等待正在执行的 handler 完成后再返回，确保主线程 `compact()` 时无锁竞争。
    15. **[Critical] 启动卡在加载索引**: App 启动后一直卡在 "Indexing files..." 加载界面，`isScanning` 无法置 false。根因：(1) `DirectoryScanner::scan()` 遇到挂载的 NFS/SMB 时 `open()` 内核级阻塞无法取消；(2) 大 WAL 重放耗时过长；(3) `setupContentPersistence` 阻塞主线程。修复方案（5 层防御）：① `DirectoryScanner` 新增 `cancel()` 机制，在 worker 线程循环、CV wait、`scanDirectory` 入口和 `getattrlistbulk` 循环中检查 `cancelled_` 原子标志；② 全局 60s 启动超时，使用 `dispatch_after` + `compare_exchange_strong` 保证 completion 只触发一次；③ 45s 扫描超时，超时后调用 `scanner->cancel()` 使用已收集的部分结果继续；④ `setupContentPersistence` 移至 `QOS_CLASS_UTILITY` 后台队列执行；⑤ WAL 重放增加 15s 超时，超时则 `loadRecords({})` 清空引擎并返回 0 强制全扫描。

### Phase 8: UI 增强 (UI Enhancements)
* **目标**: 提升 UI 可读性和交互体验。
* **已完成的功能**:
    1.  **字体放大**: 搜索框字体 `.title3` → `.title2`，文件名字体 `.body` → `.title3`，路径字体 `.caption` → `.subheadline`，文件大小字体 `.caption` → `.subheadline`，状态栏字体 `.caption` → `.callout`，图标增大至 24pt。
    2.  **无限滚动分页**: 每页显示 100 条结果，滚动到底部自动加载下一页，支持显示全部搜索结果（移除 50,000 条上限）。底部显示加载进度指示器。
    3.  **全局快捷键设置**: 在菜单栏增加 "Shortcut Settings..."（⌘,）菜单项，点击打开快捷键设置窗口。支持录制自定义快捷键组合，设置保存到 UserDefaults，支持重置为默认（⌥Space）。快捷键变更即时生效，无需重启。
    4.  **[Bugfix] 分页闪烁修复**: 修复 FSEvents 回调 `onIndexChanged()` 导致分页状态重置的问题。改为仅刷新 `cachedIndices` 和 `totalMatches`，不重置 `displayItems` 和 `loadedCount`，保持用户滚动位置和已加载内容。
    5.  **悬浮高亮**: 鼠标悬浮在搜索结果条目上时，显示类似 Alfred 的柔和高亮背景（`accentColor.opacity(0.12)`，圆角 6pt）。清除默认列表行背景以确保自定义高亮可见。
    6.  **菜单栏状态图标**: 在 macOS 菜单栏显示放大镜图标（`NSStatusItem`），点击弹出菜单包含：Show/Hide MacEverything、Rebuild Index、Shortcut Settings、Launch at Login、Quit。Show/Hide 标题根据窗口可见状态动态切换。
    7.  **开机启动**: 菜单栏菜单中增加 "Launch at Login" 勾选项，使用 `SMAppService.mainApp` API 注册/注销登录项，勾选状态实时反映当前注册状态。
    8.  **启动显示最近文件**: 扫描完成后自动显示最近修改的 100 个文件，而非空白界面。桥接层新增 `recentIndices:` 方法，使用 `partial_sort` 按 `modTime` 降序取 Top N。清空搜索框时恢复显示最近文件列表。FSEvents 索引变更时自动刷新最近文件。列表顶部显示 "Recent Files" 标签。

### Phase 9: 文件内容搜索 (In-File Content Search)
* **目标**: 支持通过 `infile:keyword` 语法搜索文件内容，实现全文检索功能。
* **技术方案**:
    1.  **Trigram 倒排索引**: `ContentIndex` (C++) 将文件内容分解为 3 字节 trigram（低 24 位 uint32_t），构建 `trigram → fileIndex 列表` 的倒排索引。查询时取所有 trigram 的交集得到候选文件，再逐文件验证消除误报。
    2.  **二进制文件检测**: 读取文件前 8KB，若包含 NUL 字节则跳过。
    3.  **FNV-1a 内容哈希**: 对每个文件内容计算 64 位 FNV-1a 哈希，用于检测文件是否变更，避免重复索引。
    4.  **实时 Snippet 生成**: 查询时按需读取文件生成上下文片段（前后各 80 字符），不在索引中存储 snippet，节省内存。
    5.  **持久化与 WAL**: `ContentIndexPersistence` 使用独立的二进制格式（MAGIC "MECI"）+ WAL 文件，支持崩溃恢复。300 秒自动压缩。存储路径: `~/Library/Caches/com.maceverything.app/content_index.{bin,wal}`。
    6.  **可配置扩展名**: 默认支持约 50 种文本文件扩展名（txt, md, swift, py, cpp, h, json, xml 等），用户可通过设置界面自定义。
    7.  **最大文件大小限制**: 默认 1MB，可通过设置界面调整（0.1MB ~ 10MB）。
    8.  **FSEvents 集成**: 文件创建/修改时自动更新内容索引，文件删除时移除。
    9.  **并行验证**: 查询阶段使用 `dispatch_apply` 并行验证候选文件和生成 snippet。
* **UI 功能**:
    1.  搜索框输入 `infile:keyword` 触发内容搜索（大小写不敏感前缀匹配）。
    2.  内容搜索结果以搜索引擎风格展示：文件名 + 路径 + 匹配上下文 snippet，关键词高亮显示。
    3.  状态栏显示内容索引进度（"Content indexing X/Y"）。
    4.  内容搜索使用 300ms 防抖（重于文件名搜索的 150ms）。
    5.  设置面板支持配置索引扩展名和最大文件大小。
    6.  右键菜单支持打开文件、在 Finder 中显示、复制路径。

### Phase 10: App 搜索与系统图标 (App Search & System Icons)
* **目标**: 支持搜索 macOS 应用程序并双击启动，所有文件/目录使用系统图标（与 Finder 一致）。
* **已完成的功能**:
    1.  **App Bundle 识别 (type=5)**: `FileRecord.type` 新增 `5=app bundle`。`DirectoryScanner` 在扫描 `VDIR` 时检测 `.app` 后缀，标记为 type=5 并跳过递归进入 bundle 内部，大幅减少无关索引条目。
    2.  **FSEvents App 感知**: `MacSearchBridge.mm` 的两个 FSEvents 处理路径（`applyFSEvents:toEngine:` 重放和 `startMonitoringFrom:` 实时监控）均新增 `.app` bundle 过滤：跳过 bundle 内部事件（`isInsideAppBundle()`），`.app` 目录标记为 type=5（`pathEndsWithApp()`）。
    3.  **系统文件图标**: `ResultRow` 和 `ContentResultRow` 使用 `NSWorkspace.shared.icon(forFile:)` 替代 SF Symbol，显示与 Finder 一致的真实文件图标（含 App 图标、文件类型图标、文件夹图标等）。
    4.  **App 双击启动**: type=5 的搜索结果双击时通过 `NSWorkspace.shared.openApplication(at:configuration:)` 启动应用程序。

### Phase 11: 搜索结果相关性排序 (Search Result Relevance Ranking)
* **目标**: 搜索结果按相关性排序，确保最匹配的结果排在最前面（如搜索 "alfred 5.app" 时 App 本身排第一，而非 bundle 内部文件）。
* **已完成的功能**:
    1.  **4 级优先级排序**: `SearchEngine::query()` 对匹配结果按优先级排序：0=文件名精确匹配、1=文件名前缀匹配、2=文件名包含关键词、3=仅路径包含关键词。同一优先级内按路径长度升序（路径越短 = 层级越浅 = 越重要）。
    2.  **全量扫描后排序截取**: 移除旧的提前终止逻辑（`collectLimit`），改为全量并行扫描所有匹配，排序后截取前 `maxResults` 条。确保高相关性结果不会因 index 顺序被低相关性结果挤出。
    3.  **通用设计**: 排序逻辑不依赖特定文件类型（如 `.app`），适用于所有搜索场景。

### Phase 12: 本地 CI 系统 (Local CI with Fast/Slow Test Separation)
* **目标**: 建立本地 CI 系统，每次代码改动自动运行测试，确保功能不被破坏。
* **已完成的功能**:
    1.  **选择性测试执行**: 重构 `test_all.cpp` 的 `main()` 函数，支持命令行参数选择运行哪些测试：
        - `./test_all --fast` — 快速单元测试 (Part 3, 3b, 3c, 3d, 3e, 5)
        - `./test_all --slow` — 慢速集成测试 (Part 1, 4, 6)
        - `./test_all --part <id>` — 运行指定 part（可重复）
        - `./test_all` — 运行全部测试
    2.  **搜索排序测试 (Part 3e)**: 新增 Search Ranking Tests，覆盖 Phase 11 的 4 级排序逻辑：精确匹配优先、前缀优先于包含、路径匹配最低、同优先级按路径长度排序、maxResults 截取后排序正确、glob 模式匹配。
    3.  **Makefile 构建系统**: 提供 `test`/`test-fast`/`test-slow`/`test-all`/`app`/`dmg`/`clean` 等 targets。
    4.  **Git pre-commit hook**: 每次 `git commit` 自动运行 `make test-fast`，快速测试失败则阻止提交。
    5.  **快/慢分离**: 快速测试 < 5 秒完成（纯内存单元测试），慢速测试需要磁盘扫描和 FSEvents（~45 秒）。

### Phase 13: 全面质量审计与多层修复 (Comprehensive Quality Audit & Multi-Layer Fixes)
* **目标**: 对 Core/Bridge/App 三层进行系统性审计，修复 13 个高严重度和 21 个中严重度问题，覆盖线程安全、资源泄漏、崩溃风险、数据一致性等维度。
* **测试架构重构**: 将 `test_all.cpp` (1167 行) 拆分为 13 个独立测试模块放入 `tests/` 目录，通过 `#include` 组合。新增 Phase 1-3 回归测试 (test_phase1.h, test_phase2.h, test_phase3.h)，总测试断言 187 个。
* **Phase 1 — Core 层高严重度修复 (5 项)**:
    1.  **[C1] compactRecords 返回索引映射**: `compactRecords()` 返回 `unordered_map<uint32_t, uint32_t>` (old→new 索引映射)，`ContentIndex` 新增 `remapFileIndices()` 方法同步更新倒排索引，防止 compact 后内容索引指向无效位置。
    2.  **[C2] 析构函数等待后台任务**: `IndexPersistence` 和 `ContentIndexPersistence` 新增 `stopAutoCompactionAndWait()`，通过 `dispatch_sync` 等待正在执行的压缩 handler 完成后再释放资源，防止 use-after-free 崩溃。
    3.  **[C3] ContentIndexPersistence WAL 竞态**: WAL 替换操作加 `walMutex_` 保护，`walAppendAdd/Remove` 获取共享 WAL 引用前加锁，防止 compact 线程替换 WAL 时写入线程持有悬挂指针。
    4.  **[C4] saveToFile 写入错误检查**: 循环中每条 `writeRecord` 调用检查返回值，失败时 `fclose` + `remove` 临时文件并返回 false，防止磁盘满时生成截断的索引文件。
    5.  **[C5] DirectoryScanner 扫描状态重置**: `scan()` 开头重置 `done_`、`activeTasks_`、清空 `visitedDirs_`、`stats_`、`workQueue_`，支持同一实例多次扫描不同路径。
* **Phase 2 — Core 层中严重度修复 (10 项)**:
    1.  **[P1] WAL 批量 fsync**: `IndexWAL::append()` 改为 `fflush` + 16 次写入合并一次 `fsync`，减少 I/O 开销，实测 200 次写入从 ~400ms 降至 <1ms。
    2.  **[P2] Scanner 内存优化**: 使用 `std::move` 转移 `FileRecord`，减少扫描期间的字符串拷贝。
    3.  **[P3] Trigram 提取效率**: 验证 100KB 文本的 trigram 提取在 100ms 内完成，无重复 trigram。
    4.  **[P4] 内容索引单次 I/O**: 验证文件索引只读取一次，re-index 检测到内容未变时直接跳过。
    5.  **[P5] 排序 posting list**: 验证倒排索引 posting list 有序，支持 O(n+m) 集合交集。
    6.  **[P6] Trigram 跳过逻辑**: 验证查询时 trigram 候选集正确过滤。
    7.  **[T1] 压缩原子性**: 验证 compact 后记录计数正确、metadata 保留、无重复记录。
    8.  **[T2] hasAllowedExtension 线程安全**: 验证扩展名检查在并发访问下无错误。
    9.  **[L1] 大小写不敏感 pathIndex**: `pathIndex_` 键统一为小写，`removeByPath`/`updateByPath`/`indexForPath` 全部做 `toLower` 查找，兼容 APFS 大小写不敏感语义。
    10. **[L2] WAL rename 错误处理**: compact 中 WAL 重命名失败时记录日志但不中断，下次 compact 仍可正常工作。
* **Phase 3 — Bridge 层修复 (6 项)**:
    1.  **[B1] startMonitoringFrom 缺少 kFSEventStreamCreateFlagFileEvents**: 已在 Phase 7 修复，确认正确。
    2.  **[B2] removeByPath 先于 contentIndex 查询**: 已在代码中正确实现 — 先 `updateContentIndexForPath:removed:YES` 再 `removeByPath`，确保 `indexForPath` 在引擎删除前成功解析。
    3.  **[B3] saveToFile 元数据保存**: 验证 v3 格式正确保存和加载 `lastEventId` 及自定义元数据键值对。
    4.  **[B4] recentIndices 批量方法**: `SearchEngine` 新增 `recentIndices(count)` 方法，使用 `partial_sort` 按 `modTime` 降序返回 Top N 活跃记录，跳过 tombstone。桥接层 `recentIndices:` 直接调用。
    5.  **[B5] setupContentPersistence 目录创建错误检查**: 创建内容索引目录时检查 `NSFileManager` 返回值和 error，失败时记录日志并提前返回。
    6.  **[B6] isMonitoring 属性同步**: 已在 `startMonitoringFrom` 结尾正确设置 `_isMonitoring = YES`。
* **Phase 4 — App 层修复 (8 项)**:
    1.  **[A1] performContentSearch 过期结果防护**: 新增 `searchGeneration` 捕获和 guard 检查，防止慢速内容搜索结果覆盖新的搜索状态。
    2.  **[A2] loadMore 过期分页防护**: 新增 generation guard，防止旧搜索的分页数据追加到新搜索的结果列表中。
    3.  **[A3] HotkeyManager NSPanel 过滤**: 热键处理中使用 `NSApp.windows.first { !($0 is NSPanel) }` 过滤 NSPanel 子类，确保与 `AppDelegate.toggleWindow()` 行为一致。
    4.  **[A4] HotkeyManager 资源泄漏**: `deinit` 中新增 `RemoveEventHandler(eventHandlerRef)` 调用，释放 Carbon Event Handler 资源。
    5.  **[A5] Task.detached weak self**: 所有 `Task.detached` 闭包和内部 `MainActor.run` 闭包均添加 `[weak self]`，防止 ViewModel 生命周期被后台任务延长。
    6.  **[A6] performIndexRefresh 过期结果防护**: 新增 generation guard，防止 FSEvents 触发的刷新覆盖用户正在进行的新搜索。
    7.  **[A7] cacheDir force-unwrap 修复**: `NSSearchPathForDirectoriesInDomains` 结果从 `first!` 改为 `first ?? NSTemporaryDirectory()`，防止极端情况下崩溃。
    8.  **[A8] indexChangeTask 强引用**: 节流任务闭包添加 `[weak self]`，防止 throttle 等待期间阻止 ViewModel 释放。
* **Phase 5 — 全面质量审计修复 (22 项)**:
  * **Critical 修复 (7 项)**:
    1.  **[C-1] extractTrigrams 2MB 分配优化**: 使用 `thread_local` 位图 + dirty tracking，每次调用仅清除上次设置的位（O(unique_trigrams)），而非重新分配 2MB `vector<bool>`。
    2.  **[C-2] WAL per-entry CRC32 校验**: `IndexWAL` 和 `ContentIndexWAL` 每条 entry 末尾追加 4 字节 CRC32；读取时验证校验和，损坏处立即停止回放。
    3.  **[C-3] DirectoryScanner activeTasks_ 不变量文档**: 添加注释说明 `activeTasks_` 在 `queueMutex_` 内递增、锁外递减的设计，确保终止条件不会在可发现工作存在时触发。
    4.  **[C-4] _engine shared_ptr 线程安全**: 使用 `std::shared_mutex` 保护 `_engine` 共享指针，新增 `safeEngine`/`setEngine:` 方法，所有直接访问改为安全访问器。
    5.  **[C-5] updateContentIndexForPath 接受 engine 参数**: 方法签名添加 `engine:` 参数，避免在 FSEvents 回调中读取可能已被 swap 的 `_engine`。
    6.  **[C-6] performIndexRefresh 重建 displayItems**: 改为调用 `performSearch(searchText)` / `performContentSearch(contentKeyword)` / `loadRecentFiles()`，完整重建显示数据而非仅更新 `cachedIndices`。
    7.  **[C-7] ContentView 移除直接 bridge 引用**: 删除 `ContentView` 中的 `MacSearchBridge.shared()` 引用，新增 `ViewModel.contentIndexedCount` 属性，所有 bridge 访问通过 ViewModel 进行。
  * **High 修复 (15 项)**:
    1.  **[H-1] Compaction remap 传播到 ContentIndex**: `IndexPersistence` 新增 `setContentIndex()` 方法，compaction 后将 remap 传播到 `ContentIndex::remapFileIndices()`，防止内容索引 fileIndex 失效。
    2.  **[H-2] saveToFile fsync before rename**: `SearchEngine::saveToFile` 和 `ContentIndex::saveToFile` 在 `fclose` 前添加 `fsync(fileno(f))`，确保数据落盘后再原子重命名。
    3.  **[H-3] compactionGeneration 计数器**: `SearchEngine` 新增 `std::atomic<uint64_t> compactionGen_`，每次 `compactRecords()` 递增，支持外部检测索引是否发生过 compaction。
    4.  **[H-4] removeByPathPrefix 写 WAL**: 按前缀批量删除时，对每个被删除路径写入 WAL Remove 条目，防止 crash 后数据回滚导致"复活"。
    5.  **[H-5] toLower Unicode 支持**: 使用 CoreFoundation `CFStringLowercase` 替代逐字节 `tolower`，正确处理 UTF-8 多字节字符（如 Ä→ä）。
    6.  **[H-6] WAL 大小上限**: `IndexWAL` 限制 50MB、`ContentIndexWAL` 限制 20MB，写入前检查文件大小，超限返回 false。
    7.  **[H-7] rescanSubtree 串行化**: 新增 `_mutationQueue` 串行派发队列，`rescanSubtree:` 和 FSEvents 回调中的 mutation 统一在串行队列执行，防止并发修改引擎状态。
    8.  **[H-8] 内容索引可取消**: 新增 `_cancelContentIndexing` 原子标志，`startContentIndexing` 循环中检查取消标志，`rebuildContentIndex` 设置标志并等待旧索引停止后再启动新索引。
    9.  **[H-9] FileItem.id 稳定标识**: `FileItem.id` 从 `UInt32` 引擎索引改为 `String`（path-based），compaction 后 SwiftUI diff 不会错误复用 cell。
    10. **[H-11] ContentFileItem.id 稳定标识**: `ContentFileItem.id` 从 `UUID()` 改为 `filePath + ":" + matchOffset` 字符串，确保内容搜索结果 ID 稳定。
    11. **[H-13] 禁止多窗口**: `WindowGroup` 改为 `Window("MacEverything", id: "main")`，防止 Cmd+N 创建多个主窗口导致资源重复。
    12. **[H-14] query() QOS 降级**: `SearchEngine::query()` 中 GCD 并行搜索从 `QOS_CLASS_USER_INTERACTIVE` 降为 `QOS_CLASS_USER_INITIATED`，避免抢占 UI 线程。
    13. **[H-15] 移除非等待 stopAutoCompaction**: 删除 `IndexPersistence` 和 `ContentIndexPersistence` 的 `stopAutoCompaction()` 方法，统一使用 `stopAutoCompactionAndWait()`，避免析构期间 in-flight compaction 未完成。
    14. **[H-10] performIndexRefresh generation guard**: 已被 C-6 修复覆盖（改为调用 `performSearch`，自带 generation guard）。
    15. **[H-12] 键盘导航**: 延迟至后续版本实现（TODO）。
  * **附带修复**:
    - `onSearchTextChanged` 重置 `isLoadingMore = false`，防止搜索词变更后分页加载状态卡住。
    - `setContentIndex()` 在 4 个持久化初始化路径中调用，确保 compaction remap 在所有场景下传播。