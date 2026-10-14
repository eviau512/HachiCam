# GPT 龙娘 × Gemini 猫娘：设置页 Banner

日期：2026-10-13。高清交付：[4K PNG](./gpt-gemini-banner-v2-4k.png)，
**3840 × 2160**，约 15.5 MB。使用 Real-ESRGAN 动漫插图模型超分，并核对实际尺寸。
原生 4 倍结果为 **6688 × 3764**，保留在本地
`.tools/banner-super-resolution/gpt-gemini-native-4x.png`。

场景原稿：[gpt-gemini-banner-v2.png](./gpt-gemini-banner-v2.png)，
**1672 × 941**，约 16:9，PNG，不透明背景，由内置 imagegen 工具生成。
4K 版基于此原稿直接超分，保持原构图、人物、表情与动作。

画面按用户草图安排：Gemini 猫娘在前偏左、GPT 白色龙娘在后偏右，从背后搂腰。
GPT 面颊潮红、害羞侧目，Gemini 睁大异色瞳、惊讶张嘴并眉心泛红。第二轮修订
补全了龙角上方空间，并强调了 Gemini 的眉心红晕。没有嵌入文字。

最新应用资源为 `app/src/hachimi/res/drawable-nodpi/settings_character_banner.webp`，
来自原生 4× 超分结果，**6688×3764**，并另存为
[无损 WebP](./gpt-gemini-banner-v2-native-4x.webp)。与原生 4× PNG 的解码 RGB
逐像素一致；编码参数与校验见 [WEBP_EXPORT.json](./WEBP_EXPORT.json)。
在 Hachimi 设置列表底部、关于与日志导出卡片之后完整显示，按实际显示宽度
异步采样解码，保持原生比例、20 dp 圆角；标准版没有此图或空白占位。
原始 v2 PNG 与 3840×2160 的 4K 版继续保留，供归档对照。

## 生成记录

- [首次生成提示词](./GENERATION_PROMPT.txt)：草图只控制站位，两张角色立绘只控制身份与造型。
- [定稿修订提示词](./REVISION_PROMPT.txt)：拉远构图、保留完整龙角、明确眉心红晕。
- [4K 提示词尝试](./UPSCALE_4K_PROMPT.txt)：内置工具实际仍返回 1672 × 941，因此未用此结果冒充 4K。
- [实际超分记录](./SUPER_RESOLUTION.json)：专用模型、文件尺寸、校验值及 4K 导出方法。
- [来源清单](./REFERENCE_MANIFEST.json)：11 张第三方参考的原图 URL、来源页、署名／授权标记、SHA-256。
- [用户站位草图](./references/user-pose-sketch.jpg)：单独保留在本地，不计入 11 张第三方参考。
- [出处研究](../../specs/research/LLM_MOE_ORIGINS_2026-10-13.zh-CN.md)：原作者入口、创作链、二手日期和未确认事项。
- [验证记录](./VALIDATION.md)：图像检查、两种版本的编译／资源检查及真机验证限制。

## 收集到的形象样本

以下是这次实际下载、查看的样本，代表各自图中的设计，**不作为全社区唯一标准**。
参考原图在本地 `references/`，被已有忽略规则排除；版本库保留本目录的来源清单。
第三方图不属于应用资源。

| 角色 | 本次样本的辨识特征 | 本地参考 | 来源与署名记录 |
| --- | --- | --- | --- |
| GPT | 白色龙娘；双龙角、白发、浅紫瞳、龙翼、白色鳞尾 | [立绘](./references/gpt-zip-reference.jpg) | [社区转载](https://linux.do/t/topic/2878430/9)署名 ZipZipPipe；单图许可未知；用于 Banner 身份参考 |
| Gemini／哈基米 | 蓝紫猫娘；猫耳、粉／金异色瞳、棱镜星饰、猫尾 | [立绘](./references/gemini-zip-reference.jpg) | 同上；单图许可未知；用于 Banner 身份参考 |
| Claude | 橙色长发、橙白黑长裙、花饰与书本 | [立绘](./references/claude-zip-reference.jpg) | 同一社区转载署名 ZipZipPipe；单图许可未知 |
| Grok | 金色双马尾、尖耳／蝙蝠翅饰、黑金红裙、巨斧 | [立绘](./references/grok-zip-reference.jpg) | 同一社区转载署名 ZipZipPipe；单图许可未知 |
| DeepSeek | 蓝色鲸鱼娘 Q 版；鲸鳍、鲸尾、女仆装 | [三视图](./references/deepseek-archive-reference.jpg) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/deepseek202610090014.html)；本样本原作者与单图授权未标明；谱系署名见研究 |
| GLM／智谱 | 黑长发狐娘；黑白狐耳尾、黑白女仆装 | [立绘](./references/glm-archive-reference.png) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/glm202610120001.html)；本样本原作者与授权未标明 |
| Kimi | 银白发、蓝白星月裙、长笛与月亮／音符 | [角色卡](./references/kimi-archive-reference.png) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/kimi202609270001.html)；原作者与授权未标明；卡上星级不作为性能证据 |
| Qwen／千问 | 紫色短发、猫耳耳机、绿瞳、绿背心与格裙 | [社区变体](./references/qwen-archive-reference.png) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/qwen202609200001.html)；@MX1951 投稿，档案标记 CC0，未另行验证其底层权利 |
| 豆包 | 黑短发、粉黑白女仆装、包子／蒸笼配饰 | [社区变体](./references/doubao-archive-reference.png) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/doubao202610090001.html)；原作者与授权未标明 |
| MiMo | 白发兔娘；长兔耳、橙白女仆装、橙色蝴蝶结 | [三视图变体](./references/mimo-archive-reference.png) | [档案页](https://xn--pssy23gqgbz2d718b.com/works/mimo202609300001.html)标记作者 @Immyxxr；授权未标明 |
| StepFun／阶跃星辰 | 浅蓝长发、黑白服装、方块徽饰和提灯 | [立绘](./references/stepfun-archive-reference.png) | [作者投稿](https://www.bilibili.com/opus/1250303372583501829)，档案作者 @甶丶、标记 CC BY-NC |

原作者 [《大AI和小AI们》](https://www.bilibili.com/video/BV1tE9XBbErS/) 的九位名单还包括
MiniMax；这次没有保存一张出处已核对的独立 MiniMax 立绘。新增收集的三位角色
及不同作者变体，不视为该原视频的组成部分。

用户提供的 [LLM_Moe](https://t.me/addstickers/LLM_Moe) 落地页确认包名为「LLM小妹」，
但没有提供逐图署名，也未确认它的维护者就是角色创作者。本目录没有冒充该包的
完整导出。

角色与参考素材权利说明见 [ARTWORK_NOTICE](../../ARTWORK_NOTICE.md)。
