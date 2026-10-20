# 提交给 Main 的文档边缘对齐与多帧超分研究

日期：2026-10-14（Asia/Taipei）。目标：文档、小字、标点和细线的实际清晰度。
状态：公开路线调研、离线原型及有限实验已完成；未替换 Android 算法，未证明真实
文字分辨率提升或手机性能达标。读取的生产基线 HEAD 为 `b93bad5`；同目录另有正在
进行的曝光控制修改，本研究保留那些修改。

## 1. 给 Main 的决策

**优先研究“全局初值＋局部亚像素拟合＋独立歧义检查＋原始观测重建”。**
公开的 Pixel burst SR 和 IPOL 可复现实现已经足够启动这条路线，本轮不需要先逆向
闭源相机。文档应优先验证观测支持的笔画，不能以生成模型补出的清晰字形作为通过。

目前有两个可运行候选：

1. 保留 ECC，增加方向可辨识性、匹配唯一性、残差支持和双向一致性检查，作为较小
   的增量实验。这主要改善证据筛选，不会自动修正 ECC 已有的亚像素偏差。
2. 分层块匹配＋稳健 Lucas–Kanade（LK），作为精度候选；先限制在细节区域研究。
   合成位移更准确，但本 Python 实现更慢，真实样本也明显过于保守，尚不能直接上线。

**不能宣布已经找到可直接替换的“够用算法”。** 我们已经找到值得继续验证的路线、
可复现代码和明确的失败证据。Main 下一轮应比较相同 patch 大小、相同像素支持下的
完整生产验证流程，并进行端到端重建；不应仅靠增加接受率或更大的文件来判断效果。

## 2. 已有实现与真正缺口

代码观察来自 `local_registration.cpp`、`fusion_pipeline.cpp`、
`regional_reconstruction.cpp` 与 `regional_refinement.cpp`：

- 输入是 ISP JPEG，CPU 处理；不是线性 Bayer RAW，不能直接套用 RAW 成果的收益。
- 已比较全局 ECC、ORB、phase 等候选；局部默认节点间距 256，约 321×321 重叠
  patch 内做平移 ECC，校正幅度限 8 源像素。局部高频相关、细节比例、噪声、逐像素
  残差和相邻位移分歧已有保护，不能把这些描述成缺失。
- rc11 已有 64 源像素区域资格、分布式相位检查和有限反投影。对齐、相位资格与
  重建器共同决定结果；更准的 flow 本身不保证更好的小字。
- 当前局部拟合没有显式梯度 Hessian 二维可辨识性、竞争匹配峰唯一性或反向一致性
  契约。这是本轮识别的增量研究点；不是现有伪影的已证实唯一原因。

直边能约束法向位置，但沿边方向常无法判断。表格线、重复文字则可能有多个看似
合理的对应。单纯对二值边缘做匹配会丢失灰度、笔画宽度和抗锯齿信息。应该使用
灰度/梯度估计几何，再保留原始样本用于融合。模糊、ISP 锐化/降噪、JPEG 非线性、
曝光差异和真实局部运动仍可能限制可恢复细节。

## 3. 成熟产品与公开实现

下表区分已经出货的功能、可复现实验和营销描述；没有实机横向画质排名。

| 对象 | 查到的明确能力与边界 | 对本项目的用处 |
| --- | --- | --- |
| Pixel Camera / Super Res Zoom | 官方当前应用仍列出该功能；2019 公开论文说明 burst RAW 对齐融合曾用于 Pixel 的 Super Res Zoom 和 Night Sight。不能据此推断当代全部内部算法 | 最强的“已用于真实手机”路线依据。[官方应用](https://play.google.com/store/apps/details?hl=en-US&id=com.google.android.GoogleCamera)、[论文](https://research.google/pubs/handheld-multi-frame-super-resolution/) |
| IPOL / Jamy-L Handheld Multi-Frame Super-Resolution | 2023 文章补足公开算法实现细节；作者仓库说明 2026 更新已与在线 demo 分歧，仍主要面向科研，非工业性能实现；当前仓库 LICENSE 为 MIT | 首选可复现研究基线；固定 commit 再比较，不能把论文、旧 demo 和当前代码当同一版本。[文章](https://www.ipol.im/pub/art/2023/460/)、[仓库说明](https://github.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution/blob/main/README.md)、[许可证](https://raw.githubusercontent.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution/main/LICENSE) |
| PhotonCamera | 开源 Android 相机，官方描述 HDRX 多张欠曝 RAW 堆叠，且明确为 beta；不能由 HDR 堆叠推出已经验收的超分 | 相机 capture/GPU 管线的公开参考，不列为已证明的小字超分冠军。[官方描述](https://raw.githubusercontent.com/eszdman/PhotonCamera/dev/fastlane/metadata/android/en-US/full_description.txt)、[GPL 仓库](https://github.com/eszdman/PhotonCamera) |
| Samsung Adaptive Pixel | 官方说明特定 S22 系列可组合高分辨率与高感光模式画面；不等同于任意 JPEG burst 的通用超分接口 | 对标降噪/细节体验，但公开页面没有足够算法或第三方可调用性证据。[官方说明](https://www.samsung.com/sg/support/mobile-devices/enjoy-the-epic-standard-of-smartphone-experiences-with-the-galaxy-s22-series/) |
| Adobe Camera Raw Super Resolution | 成熟的机器学习单图增强，可把宽高各翻倍；与连拍获得新的观测不是同一条件 | 可作“清晰观感”参考，不能充当本项目位移估计或真实笔画恢复的证据。[官方介绍](https://blog.adobe.com/en/publish/2021/03/10/from-the-acr-team-super-resolution) |
| ImageStackAlignator | 作者公开的 RAW burst SR 实现，Windows/WPF＋CUDA，GPL-3.0；面向桌面 GPU | 另一种可检查的公开工程路线，不直接证明 Android 成本或质量。[作者仓库](https://github.com/kunzmi/ImageStackAlignator) |

一些厂商把高倍“AI Zoom”与生成式细节处理结合，例如 OPPO 对 Find X8 的官方说明。
这能提供观感目标，但不建立细小文字的证据保真。
[OPPO 官方说明](https://www.oppo.com/in/newsroom/press/oppo-find-x8-series-dual-periscope-camera/)

## 4. 算法筛选

| 路线 | 选择理由或暂缓理由 |
| --- | --- |
| 分层块匹配＋LK＋可靠性筛选 | 首选几何研究方向。公开 Pixel 论文采用块匹配后 LK 亚像素精化，并明确边缘方向歧义与高频重复纹理的伪影；本项目需独立设计 JPEG 验证。[论文正文](https://arxiv.org/html/1905.03277v1) |
| ECC＋独立可靠性检查 | 最小增量候选。已有实现可复用，但相关性分数不是精度证书；只补检查也无法保证所有坏偏移被识别。本轮实测见下一节 |
| 全图 TV-L1 / DIS 等稠密 flow | 暂不作为默认：需要验证亚像素误差、运动边界和计算成本。平滑的 flow 不证明小字对应正确；本轮未运行，不做优劣断言 |
| Deep Burst SR / BSRT | 公开 RAW 学习路线，涉及特征对齐、融合、训练域与模型部署；先作为后续质量上界研究，缺少当前 JPEG/文档/手机适配证据。[Deep Burst SR](https://arxiv.org/abs/2101.10997)、[BSRT](https://arxiv.org/abs/2204.08332) |
| 多曝光 RAW 的联合 HDR＋SR 优化 | 与远期目标接近，但需要 RAW 与成像模型，迁移不是换一个对齐函数。作者提供多曝光手持案例和学习表示/迭代优化路线。[作者项目](https://teboli.github.io/hdr_sr_raw_burst/) |
| 单图生成式超分、OCR 重绘 | 不作为文档证据重建；更可读的字形仍需证明来自观测。不能用它掩盖对齐失误 |

“边缘感知重建核”与“边缘对齐”是两个环节。沿边抑噪可以减少跨笔画污染，但不能
替代可靠的几何对应。本研究没有因公开论文提到各向异性核就替换 rc11 的反投影。

## 5. 本轮离线代码与验证

按 AGENTS.md 顺序先修订 SPEC_02 研究契约和 SPEC_06 ALIGN-R01..03，再实现。
原型位于 [research/document_alignment](../../research/document_alignment/README.md)，
参数和结果摘要在 [日期快照](../../research/document_alignment/RESULTS_2026-10-14.json)。

实现包括：半分辨率整数初值、两层稳健 LK、仅辅助几何的 gain/bias、最大 8 像素位移、
梯度 Hessian、竞争峰、残差支持和反向检查。没有稠密 field、逐像素生产 gate 或新的
SR renderer。所有参数为实验值，`accepted` 不是概率。输入为 128×128 灰度 patch；
合成参考/候选从 8 倍场景分别位移并积分采样，没有把已降采样参考简单平移来生成
全部“真值”。有限高分辨率插值仍有离散误差，没有模拟完整 OEM ISP。

### 合成探索集

17 个有效正例，6 个无二维对应/歧义/遮挡/混合运动负例。单位为源像素：

| 方法 | 正例接受 | 接受位移误差 p95 | 负例接受 |
| --- | --- | --- | --- |
| ECC 拟合器单独运行 | 17/17 | 0.0504 | 3/6 |
| ECC＋实验检查 | 17/17 | 0.0504 | 0/6 |
| 块匹配＋LK＋实验检查 | 17/17 | 0.0292 | 0/6 |

**ECC 单独运行不包含生产已有的高频、细节、插值与逐像素残差保护。** 因而 3/6
不能描述成 HachiCam 成片会接受三种错误。本测试是拟合器与新增检查的探索。

一次有意义的验证发现：若竞争峰仅取整数位置，却拿亚像素拟合最低误差与之相比，
重复网格会被错误判定为唯一。修正为竞争峰使用相同分数相位后，ECC 候选拒绝了
该网格；仅靠 Hessian 与双向一致性仍无法排除这个歧义。

### 不同随机种子/位移的合成留出正例

参数固定后，使用 seeds 100..119，各 0/3/10 encoded-level 噪声，共 60 例，另加
一例亮度变化。六种负例仍作为安全回归，不宣称独立的完整负例留出集。

| 方法 | 正例接受 | 接受误差中位数 / p95 | 接受误差最大值 | 负例接受 | 主机每块时间中位数 / p95 |
| --- | --- | --- | --- | --- | --- |
| ECC 拟合器 | 61/61 | 0.0254 / 0.1625 | 0.2495 | 3/6 | 2.11 / 6.68 ms |
| ECC＋检查 | 55/61 | 0.0247 / 0.1398 | 0.2495 | 0/6 | 11.07 / 20.38 ms |
| 块匹配＋LK＋检查 | 53/61 | 0.0101 / 0.0327 | 0.0424 | 0/6 | 34.55 / 45.36 ms |

LK 的正例接受率为 86.9%，拒绝主要集中在高噪声组；ECC＋检查为 90.2%。只看
接受误差会隐藏这些损失。ECC＋检查虽通过探索 p95≤0.15 目标，仍有约 0.25 像素
接受偏差，不能视为每个细节像素都可靠。时间来自一次 Python/桌面运行，包含检查，
没有预热/多轮分布或手机热状态；不能按块数直接推断 Android 延迟。

### 真实屏幕文字：调用现有生产对齐的只读主机探针

使用既有 `hachicam_capture_20261014_132454` 配对 JPEG 生成的 RGB 文件，参考索引
1，候选 0/2/3，全部 4096×3072。C++ 探针直接调用现有 `alignBaseFrame` 和
`registerLocal`，然后导出已全局配准域的 27 对文字 patch。

- 生产局部位移在 26/27 中心可用；18/27 中心具备生产细节资格；patch 平均逐像素
  支持的中位数为 0.8854。这三个统计不是相同资格定义。
- 两个新候选各接受 12/27：其中 7 个中心原本具备生产细节资格，5 个原本不具备。
  不能把 5 个计为新增恢复，也不能把其余拒绝自动计为修复。
- LK 与生产偏移的分歧中位数约 0.164 源像素、最大 0.387；ECC＋检查约
  0.154 / 0.380。**这是分歧，不是误差**：真实样本没有独立位移真值。
- 原生产拟合使用较大重叠 patch/节点插值，新实验是独立 128×128 patch；支持域
  和采样格不同，尚不是公平的完整替换对比。原型未把拒绝 map 接入成片。

结果使直接上线结论不成立：严格检查有可能保护错位，也有可能因 JPEG/噪声或
局部模型差异丢掉大量有效小字样本。需要端到端证据判断，不能靠放宽门槛补回比例。

执行环境：Python 3.12.14、NumPy 2.3.5、Python OpenCV 4.10.0；生产 C++ 探针使用
已有 MSYS2 OpenCV 5 主机依赖，编译和运行成功。Android 的 OpenCV 4.10.0 路径
未执行本候选。没有生成新 APK，也没有做设备资源或成片分辨力验收。

## 6. 提供给 Main 的最小接入规格

这些是下一轮实验建议，不是本轮已实现功能：

1. 保留全局配准和 native 降噪/HDR 路径。把几何估计、降噪支持、细节资格分开，
   歧义 tile 失去 SR 资格时不应自动丢掉有效降噪。
2. 使用相同 patch 大小、halo、像素掩码对比当前 ECC、ECC＋检查与块/LK。
   先看文档区域；是否细化到 96/128 节点/patch 是待测参数，不承诺全图稠密 flow。
3. 保存 dx/dy、二维结构、竞争峰 margin、残差支持、双向分歧及拒绝原因。对位移
   不连续处保留参考；不能把相邻不同运动平均成看起来平滑的假 flow。
4. 映射约定为 `source_global = reference + local_delta`。合成逆全局映射后，重建从
   原始 JPEG 观测采样；研究用的 warped patch 不能反复重采样后冒充独立原始证据。
5. 短曝光继续遵守归一化几何与原始像素辐射恢复契约；本轮 base patch 的 gain/bias
   不授权任何新的高光替换，也不把 clipped 参考当文字真值。
6. 在冻结的验收配置中定义收益/退化上限：同尺寸单帧插值、native fusion、候选输出
   同时比较真实小字、细线、标点、浅色笔画、噪声、重影和边缘扩散；OCR仅辅助。
   独立图表达到新增可分辨细节，关键标注笔画零捏造/丢失，且设备 capture-to-ready
   p50/p95、峰值内存和回退符合项目预算后，才能叫“够用”。本轮没有完成这些门槛。

优先实验顺序：**同域比较 → 选择性局部精化 → 原始观测端到端重建 → 手机成本**。
当前报告不建议开启全图新 flow、不建议削弱 rc11 区域资格，也没有证明学习模型
在本设备/文档输入上成本不合适；那条路线只是暂未取得适配证据。

## 7. 净室逆向的法律与工程边界

“脏室 subagent 写规格＋净室 subagent 实现”不能自动规避法律风险。著作权保护
表达而非算法思想，但独立写代码不等于获得全部知识产权许可。
[美国版权局：程序保护边界](https://www.copyright.gov/register/tx-programs.html)

美国 17 USC §1201(f) 的特定反规避例外涉及合法使用权、独立程序互操作等条件；
欧盟官方说明也区分正常使用时的观察/研究与为互操作所必需的反编译。纯粹追求相机
画质更好并不自动满足这些条件。未指定分析/发布司法辖区或具体许可，本报告不能
判断某个未来逆向项目是否合法。
[美国条文](https://www.copyright.gov/title17/92chap12.html)、
[欧盟官方说明](https://eur-lex.europa.eu/EN/legal-content/summary/computer-programs-legal-protection.html?fromSummary=31)

此外，专利权是排除他人实施的权利，独立实现/公开论文不是专利许可；具体软件的
合同、获取权限和秘密信息来源也需要分别判断。这里没有做专利自由实施调查。
[USPTO 专利权说明](https://www.uspto.gov/patents/basics/essentials)

若以后确有必要观察闭源对象，交接应限于自有/可合法使用样本的可观察输入输出、
失败条件、公开接口和独立验收要求，并保留来源记录。专有代码、反编译表达、私有
符号、模型权重、提取表格和逐行翻译不应通过“规格”洗成新实现。上下文分离还需
实际的文件/权限与交接检查；给两个 agent 不同名字不是可审计的隔离。

本轮只阅读公共资料、当前 HachiCam 代码和既有自有样本；未读取闭源反编译材料，
未执行竞争程序。本项目已有 GPL-3.0 许可，公开 MIT/GPL 代码路线具有明确许可文本，
可先在遵守对应许可的条件下研究；无需用净室流程来代替开源许可遵从。
