# Phase 3 Hardening Notes

> 本文档记录 Phase 3（M5 信号丢失 / M6 外部信号源接入）完成后做的代码硬化观察项与修复。
> 术语口径以 [TERMINOLOGY.md](TERMINOLOGY.md) 为准。
> 延续 [phase-1-hardening-notes.md](phase-1-hardening-notes.md) /
> [phase-2-hardening-notes.md](phase-2-hardening-notes.md) 的"已修复 / 观察项"结构。
> 修复范围：multiview-window.{hpp,cpp}、multiview-window-lost-image.cpp、multiview-instance.cpp。
>
> 相关文档：
>
> - 设计基准：[phase-3-signal-lost-and-external-sources-design.md](phase-3-signal-lost-and-external-sources-design.md)
> - 验收清单：[phase-3-acceptance-checklist.md](phase-3-acceptance-checklist.md)（"观察项"在验收清单或后续 issue 中归档跟进）
> - 已知限制：[known-limitations.md](known-limitations.md)
>
> Phase 3 文字 phase（A-H）属于过程编号，与全局 Phase 1 / Phase 2 不在同一坐标系。
> 本次硬化覆盖 M5 与 M6（M6.0–M6.6 全部子里程碑），是 Phase 3 主体收尾。

---

## MultiviewWindow / 外部 Cell 生命周期

### 已修复

- **`fallback_latched` 在 cell rebind 时未清零**：[multiview-window.cpp](../src/multiview-window.cpp)
  - `CellSlotState.fallback_latched` 是 M6.6 引入的"sticky 显示状态"位 — 失效窗口期保持唯一一种 overlay（红色 SIGNAL LOST / 黄色 FALLBACK / 蓝色 CONNECTING），由 supervisor 在 Active 时清零；
  - 但 cell rebind（Edit Source / Change Source / 删源后重新指派 / install 全量刷新）会替换 `private_source` 但**不**走 supervisor 的 Active 路径。原代码在 `refresh_cell()` 与 `update_source_refs()` 安装新源时只重置 `state = Connecting`，遗留的 `fallback_latched = true` 让新源在 Connecting 阶段就被画成 fallback；
  - 修复：在两个 install 路径同时重置 `cs.fallback_latched = false; cs.last_health_state = SignalRuntimeState::Empty;`，回到"全新窗口期"。
- **`rebuild_lost_signal_images` 多 cell 同时转换时的 post 风暴**：[multiview-window.cpp](../src/multiview-window.cpp)
  - supervisor 每帧扫描 cells，外部 cell 状态变更（Active↔Lost 等）需要在主线程重算 `compute_wanted_lost_image_path()`。原实现用 `QTimer::singleShot(0, this, ...)` 投递 lambda；
  - 多个外部 cell 同时失效（例如共享 NDI 源下线）会在同一帧内连续投递 N 个 lambda，Qt 事件队列在 lambda 实际执行前累积冗余；
  - 修复：新增 `std::atomic<bool> lost_images_rebuild_pending_`，仅在 `exchange(true)` 返回 false 时投递 lambda；lambda 进入时先 `store(false)` 再调用 `rebuild_lost_signal_images()`。"在已有未执行 post 时跳过新 post"——单次 rebuild 已覆盖所有先前观察到的转换。
  - 与已有 `volmeters_rebuild_requested_` 同源（atomic 合并请求）。
- **`provider_settings_hash` 死字段移除**：[multiview-window.{hpp,cpp}](../src/multiview-window.hpp)
  - `CellSlotState.provider_settings_hash` 自引入起仅在两处被赋 0（init / release），从未读取或比较；
  - Phase 2 hardening 中类似的 `dirty_` 用法被审查但保留（有未来用途）；这次 audit 确认本字段无任何引用并已从设计文档中移除，删去声明 + 两处赋值，使 `CellSlotState` 缩小一字节并消除"看似为后续指纹检查准备"的误导。
- **`Connecting` 状态缺少 status overlay 映射**：[multiview-window-status.cpp](../src/multiview-window-status.cpp)
  - `status_overlay_kind_for_state()` 原本只在 `RetryScheduled` 分支返回 `StatusOverlayKind::Reconnecting`（蓝色 CONNECTING... 带），`Connecting` 走 `default → None`；
  - 这导致 ffmpeg / vlc 失败循环中刚 `refresh_cell` 安装新源、provider 还没回报第一个 verdict 的窗口期（典型 100–300 ms，最长可达 `kConnectingTotalNs == 30 s`）渲染为黑底空白 — 与"未配置源"视觉等价，但实际 recreate 正在进行；
  - 复现：cell 配错误 URL → 红 SIGNAL LOST → 黑底空白（应有 CONNECTING）→ 再次红 SIGNAL LOST → 循环；
  - 修复：`Connecting` 与 `RetryScheduled` 共用 `Reconnecting` 蓝带。两个状态的差异只在 supervisor bookkeeping（等 cooldown vs 等首帧），用户视角都是"正在尝试恢复"，统一为同一条 overlay。
  - 闭环检查：`fallback_latched` 仍由 supervisor 的 Lost/Error/RetryScheduled 转换驱动，Connecting 不更改 latch，因此 fallback 路径不受影响。

### 观察项

- **supervisor 转换发生在锁内，post 也在锁内**：`lost_images_rebuild_pending_.exchange()` 与 `QTimer::singleShot(0, this, ...)` 当前位于持锁分支。`exchange` 是 atomic，`singleShot(0, ...)` 只把 `QMetaCallEvent` 推入主线程队列、不阻塞；当前未观测到锁竞争，但若 Phase 4 supervisor 内部转换变重，可考虑用"锁内置 atomic / 锁外 post"模式（参考 `rebuild_bg_images` 的四阶段做法）。
- **`signal_provider_is_internal()` 的快速路径**：每个外部转换都调用一次 `signal_provider_is_internal(cs.provider_type)`。当前实现是单字段比较（`type == SignalProviderType::Internal`），开销可忽略；记录在案以防未来 provider 类型扩展。
- **`fallback_latched` 同样需要在"切换到 Internal 源"时清零**：当前 install 路径（refresh_cell / update_source_refs）已统一处理，但若未来出现"绕开 install 直接改写 provider_type"的代码路径，必须同步重置 latched。建议沿用 install 路径（不要新建 mutator）。

---

## MultiviewInstance / 视觉与 LostSignal 设置

### 已修复

- **路径长度上界 clamp（防 GDI+/gs_image_file 输入异常）**：[multiview-instance.cpp](../src/multiview-instance.cpp)
  - 沿用 Phase 2 `fontFamily ≤ 128 字符`、Phase 1 layout span clamp 的防御性输入约束模式；
  - `BackgroundSettings::imagePath` / `OverlaySettings::imagePath` / `LostSignalSettings::placeholderImagePath` / `LostSignalSettings::signalLostImagePath` / `LostSignalSettings::fallbackName`：全部 clamp 到 4096 字节；
  - 字段值为持久化 JSON 写入，正常 UI（QFileDialog）路径不会触碰上限；clamp 仅用于阻挡手工 / 恶意 / 损坏配置导致的 `gs_image_file_init` 内部缓冲分配异常。
  - 4096 选取理由：Windows MAX_PATH 历史 260；启用 long path 后 32767。4096 涵盖典型 long path 场景且远低于触发分配器问题的体量。
- **lost-image 加载失败日志加上 instance 前缀**：[multiview-window-lost-image.cpp](../src/multiview-window-lost-image.cpp)
  - `gs_image_file_init` 失败时原日志为 `failed to load lost-signal image: <path>`，无 instance 标识，多窗口场景下无法定位；
  - 修复：使用与 health.cpp / status.cpp 一致的 `log_prefix()` 助手 → `[name(uuid8)] failed to load lost-signal image: <path>`。
  - 与 Phase 2 第二轮硬化的 VU 日志前缀约定保持一致。

### 观察项

- **`from_obs_data(nullptr)` 沉默**（继承自 Phase 2 观察项）：所有 `*Settings::from_obs_data()` 在 `data == nullptr` 时返回默认对象但不记录日志。LostSignalSettings 同样如此，未做改动。
- **图片路径只 clamp 长度，不 clamp 内容**：仍允许相对路径、UNC 路径、网络驱动器映射；OBS 自身在 `gs_image_file_init` 中处理这些情况，未在 clamp 中重复实现。`gs_image_file_init` 失败时由上一项日志（带 prefix）兜底。
- **fallback name 4096 字节远超 OBS source name 的合理范围**：保留 4096 是为统一 path/name 上限简化思维负担；后续如有 source name UI 暴露，可在 dialog 端额外加更紧的 UI clamp（不影响数据层 clamp）。

---

## Provider 抽象层

> 见 [phase-3-signal-lost-and-external-sources-design.md](phase-3-signal-lost-and-external-sources-design.md) §6 设计基准。
> 本次未对 ISignalProvider 接口或四个 provider 实现做硬化级改动；M6.6 阶段已完成主要收敛。

### 已修复（M6.6 主要收尾）

> 以下条目对应已合入的 M6.6 commits（b51d976 等），列在此处作为 Phase 3 hardening 的完整事件序列。

- **deep_copy_provider_settings 静态助手**（commit a9d6a89）：四个 provider 各自 ad-hoc clone settings，
  容易遗漏新增字段；统一到 `SignalProvider::deep_copy_provider_settings(obs_data_t*)` 单入口。
- **per-cell 文件拆分**（commit 103fece, 5f15e73）：multiview-window.cpp 一度突破 4000 行，
  拆为 `multiview-window-{health,context-menu,highlight,image,label,lost-image,safe-area,status,vu}.cpp`
  与 `provider-settings-forms-{ffmpeg,ndi,spout,vlc}.cpp`；主文件目前 ~2386 行。
- **install 时进入 Connecting 状态**：避免外部源刚指派就以 Empty 状态被 supervisor 误判为 Lost。
- **retry_on_all_behaviors**：之前仅 `RestartOnLost` 真正启动重试；现在 Paused / Lost / Error 都会
  按 backoff 重试，统一交给 supervisor 决定，不再依赖 dialog UI 的语义路径。
- **FallbackActive 状态下保留 image 渲染**：原代码只在 SignalLost 状态画 fallback image，
  fallback 模式下空帧；现在 FallbackActive 也走 lost-image 渲染管线（不同图片）。
- **行级 pillarbox snap**：行内 cell 高度 / 宽度对齐到 pixel grid，
  避免 fractional layout 在 letterbox / pillarbox 边界出现 1px 缝。
- **NDI Receive audio 持久化**：dialog 关闭后 audio_enabled 写回 settings；之前只在内存中切换。
- **VLC 播放列表 Edit 入口**：之前需要先 Clear 再 Add，UX 不可接受；新增 Edit 复用现有 source-picker。

### 观察项

- **macOS / Linux 运行时未验证**：M5 / M6 全程仅在 Windows + RelWithDebInfo + OBS 31.1.1 / 32.1.2 portable 验证；CI workflow 存在但 macOS / Linux 运行结果未确认。Phase 4 之前需要至少跑一次三平台 CI。
- **OBS 32.0 smoke 未做**：Phase 3 验收覆盖 31.1.1 与 32.1.2，未中间测 32.0 release。32.0 是用户群中的常见版本之一，发布前应补充。
- **Provider 静态 LOG_WARNING 未带 instance prefix**：`signal-provider-{ffmpeg,ndi,spout,vlc}.cpp` 的部分 warning 来自静态上下文（无 MultiviewWindow 引用），无法直接调 `log_prefix()`。当前保留无前缀；如需统一可在 cell 接收方（refresh_cell / supervisor）转译时补 prefix，但代价是延长日志链路。优先级 LOW。
- **`compute_wanted_lost_image_path()` 每次 supervisor 转换重算**：内部为字符串比较 + 单字段查表，常数时间；rebuild_lost_signal_images 已合并 post，但 compute 本身仍每帧 / 每转换调用。当前性能足够，记录在案。

---

## CI / Build / Distribution

### 观察项

- **Phase 3 仅在 RelWithDebInfo + Windows 验证**：与 Phase 2 同源观察项；仍未消除。
- **portable OBS deploy 脚本仅检测两个固定路径**：[deploy-plugin.ps1](setup/deploy-plugin.ps1) 硬编码 31.1.1 / 32.1.2 portable 安装位置，多版本测试时新增路径需要手动改脚本。后续可改为环境变量或扫描 `C:\Downloads\OBS-Studio-*-Windows-x64`。
- **dist 包目前是手工拷贝**：Phase 3 没有为 M6 引入新的打包脚本；Phase 4 / GA 阶段需要补一个 release 流程（zip + checksum + GitHub Release upload）。

---

## 修复清单（本次硬化提交）

| 文件 | 修复 | 风险等级 |
|---|---|---|
| `src/multiview-window.cpp` | refresh_cell / update_source_refs 安装路径重置 fallback_latched + last_health_state | HIGH（外部 cell 失效后 rebind 会持续错画 fallback） |
| `src/multiview-window.{hpp,cpp}` | rebuild_lost_signal_images 投递 atomic coalesce（lost_images_rebuild_pending_） | MED（多 cell 同时失效时 Qt 队列冗余） |
| `src/multiview-window.{hpp,cpp}` | 移除死字段 CellSlotState.provider_settings_hash 及两处赋值 | LOW（清理） |
| `src/multiview-instance.cpp` | BackgroundSettings/OverlaySettings/LostSignalSettings 路径与 fallbackName 4096 字节 clamp | MED（防恶意 / 损坏配置） |
| `src/multiview-window-lost-image.cpp` | failed-to-load lost-signal image 日志加 log_prefix() | LOW（可观测性） |
| `src/multiview-window-status.cpp` | Connecting 状态映射到 Reconnecting overlay（蓝带 CONNECTING...） | MED（recreate 窗口期黑底空白回归） |

---

## 与 Phase 1 / Phase 2 硬化的关系

| 主题 | Phase 1 | Phase 2 | Phase 3（本次） |
|---|---|---|---|
| 配置容错 | parse 失败保留旧值 | configVersion 升级日志 | 路径 / fallbackName 长度 clamp |
| 数值上界 | layout span clamp | 字体 / margin / dB 区间 clamp | 4096 字节字符串 clamp |
| 锁顺序 | layout 计算锁外 | bg/overlay 纹理释放锁外 | rebuild_lost_signal_images coalesce 在锁内 atomic + 锁外 post |
| 资源生命周期 | 私有 source 释放走 RAII | 越界纹理回收防泄漏 | install 路径重置 cell sticky 状态 |
| 日志可观测性 | — | rebuild_volmeters 单行 summary + instance prefix | lost-image 失败日志加 instance prefix |
| 死代码 / 重复 | — | dialog 颜色 / 文件 / 字体替换为 OBS-native 控件 | provider_settings_hash 字段移除；deep_copy_provider_settings 统一 |

Phase 3 硬化没有引入新的设计概念；本质是把 Phase 1 / Phase 2 的方法论对照应用到 M5 / M6 的新代码面（外部 provider、lost-signal 渲染管线、cell sticky 状态）。
