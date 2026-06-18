# External Output (Spout/NDI) Hardening Notes — issue #11

> 本文档记录 issue #11（外部输出：把合成多视图通过 Spout/NDI 直接送出，Approach B）
> 功能完成后做的代码硬化观察项与修复。
> 延续 [phase-1-hardening-notes.md](phase-1-hardening-notes.md) /
> [phase-2-hardening-notes.md](phase-2-hardening-notes.md) /
> [phase-3-hardening-notes.md](phase-3-hardening-notes.md) 的"已修复 / 观察项"结构。
> 修复范围：multiview-output.{hpp,cpp}、multiview-output-spout.cpp、
> multiview-window.{hpp,cpp}、plugin-main.cpp、multiview-instance.cpp、
> external-output-settings-dialog.cpp。
>
> 第一轮针对 Spout 输出 + 通用输出层（backend 无关）的收尾硬化。
> **第二轮（NDI 后端落地后）** 追加 NDI 专属硬化，见下方「NDI 后端 / 运行时加载器」节；
> 修复范围扩展至 multiview-output-ndi.cpp、multiview-ndi-runtime.cpp。

---

## 输出驱动 / Host 生命周期（plugin-main + MultiviewWindow）

### 已修复

- **`close_multiview_window` 删除 host 早于把它移出图形线程驱动列表（UAF 风险）**：
  [plugin-main.cpp](../src/plugin-main.cpp)
  - 全局 `obs_add_main_rendered_callback`（`on_main_rendered`）在图形线程每帧遍历
    `g_output_hosts`（裸指针）驱动各 host 的输出。`g_output_hosts` 由
    `multiview_refresh_output_driver()` 在 `obs_enter_graphics` 下重建，与
    `on_main_rendered`（运行于 render_main_texture 持图形上下文期间）互斥。
  - 原 `close_multiview_window` 顺序为 `delete → erase → refresh`：在 `delete`
    与 `refresh` 之间，`g_output_hosts` 仍持有已释放窗口的指针，期间图形线程的
    `on_main_rendered` 可能解引用 → use-after-free 崩溃。触发场景：删除一个正在
    输出的 instance。
  - 修复：改为 `erase(open_windows) → multiview_refresh_output_driver()（在图形锁下
    重建 g_output_hosts，排除该 host）→ close → delete`。refresh 返回后已无任何
    渲染帧持有该指针，删除才安全。
  - 对照：`on_window_closed` / `ensure_or_release_host` 走 `deleteLater` + 同步
    refresh（refresh 在返回事件循环前同步执行，先把指针移出 g_output_hosts，
    deleteLater 的实际析构在之后），本来就安全；本次只把同步 `delete` 的
    `close_multiview_window` 对齐到同一不变量。

### 观察项

- **`g_output_hosts` 的无锁遍历依赖"图形帧串行化"不变量**：`on_main_rendered` 不取
  显式锁遍历 `g_output_hosts`，正确性依赖"`multiview_refresh_output_driver` 在
  `obs_enter_graphics` 下重建列表，而 rendered 回调运行于 render_main_texture 持图形
  上下文期间"这一前提。与既有 `apply_output_settings` / `set_spout_enabled` 的
  `obs_enter_graphics` 串行化同源。若未来 OBS 改变 rendered 回调的执行上下文，需重审。
- **可见窗口 + 输出时每帧两次合成**：可见 host 的 display 回调画显示、`on_main_rendered`
  画输出，`draw_grid` 每帧执行两次（各 cell 源 `obs_source_video_render` 两次）。这是
  Phase 1 既定取舍（显示原生 + 输出独立分辨率），非缺陷；记录在案。
- **Headless host 维持源 showing**：headless 渲染宿主仍对 scene/source `inc_showing`，
  屏幕捕获类源的黄色边框在"无窗口但输出开启"时持续。属预期（输出需要源出帧）。

---

## MultiviewOutputManager / 资源与配置

### 已修复

- **`customWidth` / `customHeight` 缺上下界 clamp**：[multiview-instance.cpp](../src/multiview-instance.cpp)
  - `OutputBackendSettings::from_obs_data` 原样读入自定义分辨率；手工 / 损坏配置可写入 0
    或极大值，直接进入 `gs_texrender_create` 与 GPU 共享纹理分配。
  - 修复：clamp 到 `[16, 16384]`（下界对齐对话框 spinbox 16，上界 >8K 且远低于纹理尺寸
    上限）。沿用 Phase 1 layout span clamp / Phase 3 路径 4096 clamp 的防御性输入约束方法论。
- **死字段 `MultiviewOutputManager::has_backends()` 移除**：[multiview-output.hpp](../src/multiview-output.hpp)
  - 显示/输出解耦后 `render()` 改为 `if (output_)` 门控、`render_output_only()` 用
    `if (!output_) return;`，`has_backends()` 无任何调用点；删去（沿用 Phase 3
    `provider_settings_hash` 死字段移除做法）。

### 观察项

- **`resolve_output_dimensions` 在 `obs_get_video_info` 失败时返回 {0,0}**：此时 entry
  保持 enabled 但 `render_all` 以 `w==0||h==0` 跳过，不创建零尺寸 texrender；输出在视频
  信息可用前静默不发。属可接受的瞬态。
- **rescale 模式每帧读 profile config**：`ObsStreamRescale` / `ObsRecordRescale` 经
  `render_all → reconcile → resolve_output_dimensions` 每帧 `config_get_*` + `sscanf`。
  config 为内存哈希查找、常数时间，当前可忽略；若未来 backend 数量增长可缓存。

---

## Spout 后端

### 已修复

- **`ensure_open()` 失败路径每帧 LOG_WARNING（日志风暴）**：[multiview-output-spout.cpp](../src/multiview-output-spout.cpp)
  - `ensure_open()` 在 `submit_frame` 内每帧调用；当 `gs_get_device_obj` 返回 null 或
    `OpenDirectX11` 持续失败时，原代码每帧打一条 WARNING。
  - 修复：新增 `warned_open_failed_` 一次性标志（与既有 `warned_no_d3d11_` 同模式），
    成功 open 或 `stop()` 时复位。沿用 Phase 2/3 的日志可观测性收敛思路。

### 观察项

- **OBS 图形设备重建未处理**：`OpenDirectX11(OBS device)` 缓存了 OBS 的 D3D11 设备；
  若 OBS 发生设备重置（GPU 驱动崩溃 / 切换），缓存设备失效，`SendTexture` 将静默失败。
  当前未检测重建；优先级 LOW（罕见，且恢复手段为重启输出）。
- **Spout 静态链接进插件 DLL**：部署为单一自包含 DLL（无额外 Spout 运行库）。SpoutDX
  来自 vendored submodule（deps/Spout2，pinned 2.007.017），三方源以 `/w /WX-` 编译、
  不受插件 `/WX` 约束。

---

## NDI 后端 / 运行时加载器（第二轮）

### 已修复

- **`NdiRuntime::available()` 每帧在图形线程探测运行时（无谓 loader 往返 / 失败时每帧 LoadLibrary）**：
  [multiview-ndi-runtime.cpp](../src/multiview-ndi-runtime.cpp)
  - `MultiviewOutputManager::reconcile` 每帧计算 `want = s.enabled && backend_available(Ndi)`，
    `backend_available(Ndi) → ndi_supported() → NdiRuntime::available()`。原实现每次都做
    `GetModuleHandleA` + `GetProcAddress`；当 NDI 已启用但运行时缺失（用户启用了 NDI 却没装
    运行时）时，更会每帧 `LoadLibraryA` 探测一次失败加载——全部发生在图形线程热路径。
  - 修复：`available()` 结果用 `static` 缓存（探测一次）。运行时的存在与否在单次会话内不变；
    会话中途安装运行时于下次重启生效（与 OBS 系 NDI 插件"加载时解析一次运行时"行为一致）。
    沿用 Spout `warned_*_` 一次性化 / 输出层"冷路径只算一次"的收敛思路。

### 观察项

- **单 stagesurface + 即时 map 的 GPU 回读停顿**：`submit_frame` 内 `gs_stage_texture` 入队后
  立即 `gs_stagesurface_map` 会阻塞至回读完成（约 1 帧）。首版选择"简单且正确"（与 Spout 首版
  一致）。**双缓冲异步回读**（stage 第 N 帧、map 第 N-1 帧，配合 `send_send_video_async_v2`）是
  明确的后续性能硬化项，因增加缓冲生命周期管理复杂度，本轮不引入。
- **`available()` 仅校验 DLL 可加载、不校验 `initialize()`**：极少数"DLL 在场但 CPU 不受支持"
  情形下，对话框会把 NDI 显示为可用、用户启用后 `acquire()` 的 `initialize()` 失败 → 后端静默
  休眠（`acquire` 已一次性 warn）。让 `available()` 跑 `initialize()` 需在 UI 线程 init/destroy
  往返，得不偿失；保持轻量探测，记录在案。
- ~~**NDI 帧 `frame_rate` 取 OBS 全局 fps**~~ **（已修复）**：原先标称 `frame_rate_N/D` 恒为全速，
  half-rate 下 Studio Monitor 等接收端显示 60fps（实际只发 30）。修复：`submit_frame` 增加
  `fpsDivisor` 参数，NDI 后端声明真实发送速率 `fps / divisor`（Spout 无帧率元数据，忽略此参）。
  用户实测撞上此项，已随该修复解决。
- **运行时引用计数销毁顺序依赖后端先析构 sender**：`NdiRuntime` 经 `shared_ptr` 引用计数，末个
  持有者析构时 `NDIlib destroy` + `FreeLibrary`；后端 `stop()` 保证先 `send_destroy` 再释放
  `shared_ptr`，满足"所有 sender 先于 destroy"的 SDK 约束。后端的 `stop()`/析构、stagesurface
  创建销毁均在图形锁下（reconcile / teardown_locked），与既有输出层不变量同源。

---

## NDI 音频 + 跨平台（第三轮）

> 覆盖上次硬化（NDI 运行时探测缓存）之后加入的内容：NDI 音频发送、QLibrary 跨平台
> 加载器、Win/macOS/Linux 三平台门控、外部输出 UX（右键开关移除 + 音频源 UI）。

### 已修复

- **音频回调每帧 `obs_get_audio_info` 重查、且可能与连接格式不一致**：
  [multiview-output-ndi.cpp](../src/multiview-output-ndi.cpp)
  - `on_audio`（OBS 音频线程）原先每帧调 `obs_get_audio_info` 取声道数/采样率。`audio_output_connect`
    的 `audio_convert_info` 已把交付格式**在连接时固定**；若会话中途改 OBS 音频设置，回调读到的
    声道数会与实际交付的平面数不一致（虽有 `if (data->data[ch])` 防空指针、不致越界，但格式错配）。
    且这是音频线程上每帧一次的全局查询。
  - 修复：连接时缓存 `connected_channels_`/`connected_sample_rate_`（在 `audio_output_connect`
    **之前**写入，经 OBS 连接路径 publish 给音频线程），回调直接用缓存值。消除每帧查询 + 格式错配。

### 观察项（音频 / 线程模型，已审核认定安全）

- **sender 跨图形线程 / 音频线程**：`sender_` 在图形线程建/毁、音频线程发送。`sender_mutex_` 守护
  其有效性：建/毁（recreate_sender / stop）与音频发送（on_audio）持锁；**视频发送不持锁**（图形线程
  独写 sender_ 生命周期 + NDI 允许音视频并发发送）。`on_audio` 的 `!sender_ || !runtime_` 短路保证
  `sender_` 为空时不读 `runtime_`，而 `sender_` 非空即蕴含 `runtime_` 已 publish 且稳定 → 无 `runtime_`
  数据竞争。`stop()` 在锁内同时清 `sender_` 与释放 `runtime_`，杜绝音频回调读到悬垂 `runtime_`。
- **无锁序反转 / 死锁**：`configure_audio`（图形线程）经 `audio_output_connect/disconnect` 取 OBS 音频锁，
  但**不**持 `sender_mutex_`；`on_audio` 持 `sender_mutex_` 但不取 OBS 音频锁的相反序 → 无环。`stop()`
  先 `disconnect_audio`（停新回调、等在飞回调）再持锁毁 sender，二者不同时持两把锁。
- **FollowStreaming 每 30 帧重轮询 `obs_frontend_get_streaming_output`（图形线程）**：沿用 VU 表
  `compute_active_track_bit` 的同源做法（亦于渲染线程调用）。Manual 模式零 frontend 调用。
- **音频恒开**：NDI 启用即发音频（UI 无"关音频"项，仅源选择）。如需视频-only，后续加"无"选项即可。

### 跨平台 / 构建

- **QLibrary 加载器无泄漏**：`load_runtime_library` 失败的 `QLibrary` 在循环内 `delete`，成功者交
  调用方（`NdiRuntime` 持有、析构 `unload()+delete`；`available()` 探测后 `unload()+delete`）。
- **macOS / Linux 仅 CI 验证**：开发机为 Windows，NDI 后端为跨平台代码（libobs `gs_*` + NDI 表 +
  QLibrary），三平台 CI（windows / macos-15 / ubuntu-24.04）经 vendored 头编译 NDI；mac/Linux 运行
  行为未本地验证，依赖 CI 编译 + 用户运行验收。

### 观察项（既有）

- **仅 Windows + Release 验证**：与 Phase 1–3 同源观察项。Spout 在非 Windows 下
  `AMV_ENABLE_SPOUT_OUTPUT` 关闭、不含 Spout 代码；headless 驱动 / 生命周期为跨平台
  代码但未在 macOS / Linux 运行验证。
- **构建期 PDB 被运行中的 OBS 占用**：开发回归——OBS 加载插件后其崩溃处理器持有
  build PDB，链接器 LNK1201。需关闭 OBS 后再链接（编译 + 符号解析本身不受影响）。

---

## 修复清单

### 第一轮（Spout + 通用输出层）

| 文件 | 修复 | 风险等级 |
|---|---|---|
| `src/plugin-main.cpp` | close_multiview_window 改为 erase + refresh(图形锁) 早于 delete，消除 on_main_rendered 对已释放 host 的 UAF | HIGH（删除正在输出的 instance 可能崩溃） |
| `src/multiview-instance.cpp` | OutputBackendSettings customWidth/Height clamp 到 [16,16384] | MED（防恶意 / 损坏配置撑爆纹理分配） |
| `src/multiview-output-spout.cpp` | ensure_open 失败日志一次性化（warned_open_failed_） | MED（持续失败时每帧日志风暴） |
| `src/multiview-output.hpp` | 移除死方法 MultiviewOutputManager::has_backends() | LOW（清理） |

### 第二轮（NDI 后端）

| 文件 | 修复 | 风险等级 |
|---|---|---|
| `src/multiview-ndi-runtime.cpp` | NdiRuntime::available() 结果缓存，消除 reconcile 每帧在图形线程的 loader 往返 / 失败时每帧 LoadLibrary 探测 | MED（NDI 启用时图形线程热路径无谓开销） |

### 第三轮（NDI 音频 + 跨平台）

| 文件 | 修复 | 风险等级 |
|---|---|---|
| `src/multiview-output-ndi.cpp` | 音频回调改用连接时缓存的声道数/采样率，消除每帧 obs_get_audio_info + 与连接格式错配 | MED（中途改音频设置时格式错配；音频线程每帧全局查询） |

> 本轮另对 NDI 音频跨线程模型（sender_mutex_、短路防竞争、无锁序反转、停止顺序）与 QLibrary
> 加载器（无泄漏）做了审核，认定安全，记于上方观察项；未发现需改动的缺陷。

---

## 与既往硬化的关系

| 主题 | 既往（Phase 1–3） | 本次（issue #11 外部输出） |
|---|---|---|
| 配置容错 | 路径 / fontFamily / span clamp | 自定义分辨率 [16,16384] clamp；fpsDivisor 仅 1/2 |
| 锁顺序 / 并发 | bg/overlay 纹理释放锁外；rebuild coalesce | host 删除早于图形线程列表移除 → 修正为 refresh-then-delete |
| 资源生命周期 | 私有 source RAII；纹理回收防泄漏 | texrender per-resolution GC；teardown_locked / shutdown_graphics |
| 日志可观测性 | VU / lost-image 加 instance prefix | Spout open 失败日志一次性化 |
| 死代码 | provider_settings_hash 移除 | has_backends() 移除；blit 路径移除（解耦时） |

本轮硬化未引入新设计概念；把既往方法论对照应用到输出层的新代码面（backend 无关的输出
管理器、headless 全局驱动 + host 生命周期、Spout 发送后端、输出配置序列化）。
