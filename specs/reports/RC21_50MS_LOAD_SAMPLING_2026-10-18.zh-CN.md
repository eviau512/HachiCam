# rc21：50ms CPU/GPU 负载采样

用户要求提高时间分辨率。分析 rc20 代码和 SPEC_00/06 后，先修订两份权威规格，
再修改采样器、原始计数、容量与回归检查。500ms 适合整轮趋势，但不能可靠观察更短
的 CPU/GPU 负载变化；50ms 作为诊断读取目标合理，手机上的代价仍需实际测量。

## 实现与语义

- 应用采样器与长驻固定 helper 都改成 **50ms 目标间隔**。仍只有一个共享后台采样器，
  不逐次拉起进程、不阻塞拍照/导出。记录实际间隔和计数读取年龄，调度及 IO 可能延长周期。
- CPU 占用最小有效间隔由 100ms 调至 25ms，否则真实 50ms 读数会被旧门槛全部清空。
  启动极短间隔仍不计算比例；保留累计进程 CPU 毫秒、非负差分及整机 CPU 的独立差分间隔。
  重置/无效计数为未知，短窗口的量化跳动不冒充负载精度；可用原始差分后续加权聚合。
- JSON schema **2** 在原有列后追加 `processCpuMs`、`processCpuDeltaMs`、
  `deviceCpuIntervalMs`，继续按 `columns` 解释。应用 CPU 是整个进程，多核单核等效值可 >100%。
- 记录上限同步从 600→**6000 条**、128KiB→**2MiB**，保留约五分钟名义时长，
  而非提高十倍频率后只留下最后 30 秒。helper 同步扩大到 6000 次和 4MiB 输出上限；
  超出记录上限淘汰最旧数据并计数，所有数据仍绑定 captureId。
- 设备快照新鲜度由 1500→**250ms**；Android 热等级仍约 **500ms** 更新一次并标注缓存周期，
  避免每秒 20 次热状态服务调用。GPU 频率仍随设备计数读取。
- 整机 GPU 保持 busy/total 驱动窗口语义。驱动更新可能慢于读取、窗口可能重复或为零；
  **50ms 读取不等于每次有全新的 50ms GPU 测量**，更不代表 HachiCam 独占占用率。
  缺失或陈旧仍是 `null`，不伪造空闲。release 仍为空采样实现。

CPU API 的 [Android 文档](https://developer.android.com/reference/android/os/Process#getElapsedCpuTime())
给出毫秒返回值；[AOSP 实现](https://android.googlesource.com/platform/frameworks/base/+/7b2f8b8/core/jni/android_util_Process.cpp)
使用进程 CPU clock 后转换为毫秒，因此这里保留原始计数，不把短窗比例说成无限精细。
[KGSL 源码](https://android.googlesource.com/kernel/msm/+/bb9406fd87124400118f8034c745d8523267f1a5/drivers/gpu/msm/kgsl_pwrctrl.c)
返回驱动维护的 busy_old/total_old，不能用采样频率推导其刷新频率。

## 验证

两模式各 **111** 项单测、standard Kotlin 编译通过。覆盖 50ms 有效 CPU 百分比与原始计数、
计数重置、极短启动、设备独立间隔/重复/陈旧、6000 行五分钟保留及同组完整导出，
并确认大于原 128KiB 的合法轨迹可写入，超过 2MiB 不覆盖上一个完整快照。

由实际 Kotlin helper 脚本展开后在 Windows Git Bash 运行 60 次：协议通过，
实际平均 62.44ms、p95 65.31ms，stdin 停止约 5.26ms、退出 0。宿主计时含其调度/
计时器限制；该检查只验证协议、节奏与停止，不用于监控开销结论。这不是 Android/root/
GPU 节点速度或权限验收，更不能承诺手机固定 50ms 或无额外耗时。

原生算法没有更改；本轮无需重复图像算法基准。安装包内三 ABI 原生库将与 rc20
逐字节核对。新包手机验收需观察实际采样间隔/p95、有效/缺失覆盖、驱动重复窗口和
完整处理等待，确认监控额外开销是否可接受。

## 交付

rc21 /versionCode 45、`HachiCamTest`、`moe.hachimi.cam.test` universal 已构建并校验：

- `HachiCamTest-0.1.2-rc21-1702-universal.apk`，126,901,487 字节。
- SHA-256 `6c75fde1e2d6e911b70b88478ef145a62b9eebc667a3ba67bad5b77eb7c8374c`。
- 干净源码 `6dc065cbadac18f1cc662f490b7932dc857c505f` 已推送 `gpu-hdr`，实时远端核对一致。
- 原调试证书/v2 签名、版本/名称、ZIP CRC/对齐、三 ABI/Banner、新采样标记与不可变副本哈希通过；
  测试钩子仍未进入 APK，三 ABI 原生库逐字节等同 rc20。原有第三方库完整 16KiB 页兼容
  待办保持，不能将 ZIP 对齐当作其验收。
- **2026-10-18 17:04:23 +08:00**，Tailscale 发送 Hyper-obs（100.79.217.41）成功，退出码 0，
  前后本地大小/哈希一致；接收端完整性、安装及手机实际采样频率/开销仍未独立验收。
