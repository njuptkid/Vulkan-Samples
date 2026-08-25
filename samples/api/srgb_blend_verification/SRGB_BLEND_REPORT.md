# sRGB 非线性空间与线性空间混合(Blending)关系报告

> 基于 Vulkan-Samples 项目 `srgb_blend_verification` 示例的实测验证结果撰写。
> 验证程序:`samples/api/srgb_blend_verification/`
> 实测日期:2026-08-25

---

## 1. 摘要

本报告通过一个可复现的 Vulkan 实验,验证了一个核心命题:

> **当颜色附件(color attachment)使用 sRGB 格式时,Vulkan 硬件 blend 单元会先把目标(dst)颜色从 sRGB 编码线性化,然后在线性空间执行混合运算,最后将结果重新编码为 sRGB 后写回。源(src)颜色(fragment shader 输出)不进行任何转换。**

实验的三个 patch 实测结果与该假设完全一致(误差 ≤1,来自硬件取整差异),而与非线性的反例假设相差 12~60 级字节,差异具有判别性。

P1(黑背景 + 白色 α=0.5)单独即可定论:**线性假设预期 188,非线性假设预期 128,实测 187** —— 不可能由"不做线性化"路径产生。

---

## 2. 背景:为什么需要非线性编码

### 2.1 人眼对亮度的感知是非线性的

人眼对暗部的细节变化比对亮部的细节变化更敏感。如果以**线性方式**(光子数正比)等量分配 8 位精度,暗部会出现严重的等高线(banding),亮部则浪费精度。

sRGB 编码(OETF,光学-电子传递函数)通过一条 gamma ≈ 2.2 的幂曲线,把线性亮度重新映射到 8 位字节空间,**让暗部获得更多量化级数**:

```
byte 0   ↔ linear 0      (黑)
byte 128 ↔ linear 0.214  (中灰,看起来约 50% 灰)
byte 188 ↔ linear 0.5    (看起来约 73% 灰)
byte 255 ↔ linear 1      (白)
```

视觉上"50% 灰"对应的字节是 128(sRGB 编码),但其代表的**真实光强度**只有线性的 0.214。

### 2.2 两套表示

| 名称 | 含义 | 物理意义 |
|------|------|---------|
| **线性空间(linear)** | 与光强度成正比的浮点值 | 物理真实 |
| **sRGB 空间(non-linear)** | 经过 EOTF 编码后的字节值 | 便于存储,人眼友好 |

二者之间的转换由 sRGB 传递函数定义。

---

## 3. sRGB 传递函数(IEC 61966-2-1)

### 3.1 解码:sRGB → linear(EOTF)

读 framebuffer 时硬件做(把存储的字节还原成线性光):

```
              ⎧  s / 12.92                  if s ≤ 0.04045
decode(s) =  ⎨
              ⎩  ((s + 0.055) / 1.055)^2.4  otherwise
```

其中 `s ∈ [0, 1]`,是字节值除以 255 后的归一化值。

### 3.2 编码:linear → sRGB(OETF⁻¹)

写 framebuffer 时硬件做(把线性光编码成字节存储):

```
                  ⎧  12.92 · L                  if L ≤ 0.0031308
encode(L) =      ⎨
                  ⎩  1.055 · L^(1/2.4) − 0.055  otherwise
```

### 3.3 字节 ↔ 浮点

```
byte = round(value × 255)
value = byte / 255
```

### 3.4 几个关键参考点

| sRGB byte | sRGB 归一化 | linear 值 | 含义 |
|-----------|------------|----------|------|
| 0 | 0.0 | 0.0 | 黑 |
| 8 | 0.0314 | 0.00243 | 极暗 |
| 51 | 0.2 | 0.0331 | 暗(常见深灰背景) |
| 128 | 0.502 | 0.214 | 中灰(50% 视觉灰) |
| 188 | 0.735 | 0.5 | 一半物理光强 |
| 255 | 1.0 | 1.0 | 白 |

**注意**:字节 128(sRGB 0.5)和线性 0.5(byte 188)是**两回事**,后者亮得多。

---

## 4. 为什么必须在线性空间做混合

### 4.1 物理正确性

光强度的混合是**线性的**。两个光源叠加,总光强度 = 各光源光强度之和(按比例)。如果在不正确的空间做加权平均,会产生:

- 颜色偏移(hue shift)
- 中间亮度过渡不自然
- 半透明边缘出现可见接缝

### 4.2 一个反例:在 sRGB 空间直接线性插值

假设要在黑色背景上叠加一个 α=0.5 的白色光源。物理上结果应该是线性 0.5(一半光强),编码后存为 byte 188。

- **线性空间插值**(正确):`0.5 × 1.0 + 0.5 × 0 = 0.5`(linear)→ `encode(0.5) = 0.735` → byte **188** ✓
- **sRGB 空间直接插值**(错误):`0.5 × 1.0 + 0.5 × 0 = 0.5`(sRGB)→ byte **128** ✗

byte 128 视觉上对应线性 0.214 —— 远比真实光强 0.5 暗得多。这就是为什么在不做线性化的路径上,半透明混合看起来过暗。

### 4.3 渐变带的视觉差异

如果把 0(黑)到 1(白)在中点取样:
- 线性空间的中点 = linear 0.5 = sRGB byte 188(亮灰)
- sRGB 空间的中点 = byte 128(中灰)

后者在视觉上并不"居中"——人眼会觉得它比真实中点更亮。这是为什么 photo editor 的"50% 灰"对应字节 128 而非 188:它们处理的是 sRGB 编码值,而非线性光强。

---

## 5. Vulkan 硬件 blend 对 sRGB 附件的处理

### 5.1 规范原文("Blend Operations" 节)

> If the numeric format of a framebuffer attachment uses sRGB encoding, the R, G, and B **destination** color values ... are considered to be encoded for the sRGB color space and hence are **linearized prior to their use in blending**.

> If the numeric format of a framebuffer attachment uses sRGB encoding, then **the final R, G, and B values are converted into the nonlinear sRGB representation before being written** to the framebuffer attachment.

> The value of A is never sRGB encoded.

### 5.2 三个角色,只对两个做转换

| 角色 | 是否做 sRGB 转换 | 说明 |
|------|----------------|------|
| **src**(fragment shader 输出) | ❌ **不转换** | shader 负责输出线性值,硬件原样使用 |
| **dst**(framebuffer 现有值) | ✅ **decode**(sRGB→linear) | 读出时硬件自动线性化 |
| **blend 结果** | ✅ **encode**(linear→sRGB) | 写入前硬件自动编码 |
| **alpha 通道** | ❌ 永不转换 | alpha 始终以线性方式存储 |

**关键**:src 不做转换。这意味着 shader 写什么,blend 单元就拿什么当线性值用。如果 shader 输出 `vec3(0.5)`,blend 单元认为这就是线性 0.5,**不会**做 `decode(0.5)` 把它再线性化成 0.214。

### 5.3 流程图

```
fragment shader 输出 Cs(linear)
       │
       │  (无 sRGB 转换)
       ▼
   ┌────────────────────────────────────────┐
   │ src_blend = Cs · SRC_ALPHA             │
   │                                        │
   │ dst_color = decode(dst_byte)           │  ← 仅此处对 dst 做 sRGB→linear
   │ dst_blend = dst_color ·                │
   │              ONE_MINUS_SRC_ALPHA        │
   │                                        │
   │ result_linear = src_blend + dst_blend  │
   └────────────────────────────────────────┘
       │
       │  encode(result_linear)               ← 对结果做 linear→sRGB
       ▼
   framebuffer 存盘 byte
```

---

## 6. 验证方法学

### 6.1 实验目标

判定硬件在 sRGB color attachment 上做的 blend 是:

- **假设 A(线性路径,规范)**:dst decode → 线性空间 blend → 结果 encode
- **假设 B(非线性路径,反例)**:不做任何 sRGB 转换,dst 当原值用,blend 结果原值存盘

### 6.2 测试设置

| 配置 | 说明 |
|------|------|
| Pipeline | alpha blend:`src=SRC_ALPHA, dst=ONE_MINUS_SRC_ALPHA, op=ADD` |
| Fragment shader | 全屏三角形,输出 push-constant 推入的 `vec4(rgb, α)` |
| 背景 draw | shader 输出 `(bg_linear, bg_linear, bg_linear, α=1)` —— 覆盖附件初值 |
| 测试 draw | shader 输出 `(1, 1, 1, α=0.5)` —— 白色半透明叠加 |
| 控制组 | 同一 pipeline、同一 shader、同一参数,但 attachment 用 UNORM 而非 sRGB |

### 6.3 三个 patch 的背景选择

为充分覆盖各种情况(尤其要区分"dst 线性化"和"不线性化"),选三个 sRGB 编码的背景字节:

| Patch | bg sRGB byte | 对应 linear | 在线性假设下的角色 |
|-------|-------------|------------|------------------|
| P1 | 0(黑) | 0 | 测"结果 encode"步骤(此时 dst=0,decode 不影响) |
| P2 | 51(0.2 sRGB) | 0.0331 | 测"dst decode"步骤(dst 非 0 非 1,decode 显著改变值) |
| P3 | 128(0.5 sRGB) | 0.214 | 同 P2,但 dst 在中段 |

### 6.4 两种假设下的预测字节

通用 blend 公式(`src=1, α=0.5`):

```
result = 0.5 + 0.5 · dst
```

**假设 A(线性)**:

```
dst_linear = decode(bg_sRGB_byte / 255)
result_linear = 0.5 + 0.5 · dst_linear
result_byte = round(encode(result_linear) · 255)
```

**假设 B(非线性)**:

```
dst_sRGB = bg_sRGB_byte / 255     (直接当原值,不 decode)
result_sRGB = 0.5 + 0.5 · dst_sRGB
result_byte = round(result_sRGB · 255)    (不 encode,原值存盘)
```

**UNORM 控制组**(无 sRGB 路径):

```
dst = round(decode(bg_sRGB_byte / 255) · 255) / 255   (背景存的是 linear 值的原字节)
result = 0.5 + 0.5 · dst
result_byte = round(result · 255)
```

### 6.5 数值预测表

| Patch | bg sRGB byte | 假设 A(线性) | 假设 B(非线性) | UNORM 控制 |
|-------|-------------|--------------|-----------------|-----------|
| P1 | 0 | **188** | 128 | 128 |
| P2 | 51 | **190** | 153 | 132 |
| P3 | 128 | **205** | 192 | 155 |

P1 单独就是判据:差 60 级,不可能混淆。

---

## 7. 实测结果

### 7.1 sRGB attachment

| Patch | 实测 byte | 线性预期 | 非线性预期 | 与线性 Δ | 与非线性 Δ | 判定 |
|-------|----------|---------|----------|---------|----------|------|
| P1 | **187** | 188 | 128 | 1 | 59 | **LINEAR** |
| P2 | **190** | 190 | 153 | 0 | 37 | **LINEAR** |
| P3 | **204** | 205 | 192 | 1 | 12 | **LINEAR** |

三个 patch 全部命中线性假设(误差 ≤1,来自厂商 sRGB encode 步骤的取整策略),非线性假设相差 12~59 级,完全可判别。

### 7.2 UNORM 控制组

| Patch | 实测 byte | 预期 | Δ |
|-------|----------|------|---|
| P1 | 127 | 128 | 1 |
| P2 | 131 | 132 | 1 |
| P3 | 155 | 155 | 0 |

控制组全部命中预测,数学和管道都正确。

### 7.3 综合判定

**假设 A(线性路径)成立**:硬件在 sRGB color attachment 上确实做了 dst 线性化 → 线性空间 blend → 结果 sRGB 编码 三步处理。

---

## 8. 关键的派生证据:src 不做转换

P2 是个干净的判据。因为 P2 的背景 draw 输出的是 `bg_linear = 0.0331`(不是 0 或 1 这种 decode 后不变的值),两种假设给出**不同的预测字节**:

### 8.1 假设 src 不转换(规范实际行为,与实测吻合)

**背景 draw**(α=1,shader 输出 0.0331):
```
src = 0.0331              ← 原值,不 decode
dst = decode(0/255) = 0
blend = 0.0331·1 + 0·0 = 0.0331
store = encode(0.0331) = 0.2  →  byte 51     ✓(背景正确存为 byte 51)
```

**测试 draw**(α=0.5,shader 输出 1):
```
src = 1.0                  ← 原值,不 decode
dst = decode(51/255) = 0.0331
blend = 1·0.5 + 0.0331·0.5 = 0.5166
store = encode(0.5166) = 0.746  →  byte 190   ✓ 实测 190
```

### 8.2 反例:src 也 decode(规范没说,只是假设)

**背景 draw**:
```
src = decode(0.0331) = 0.0331/12.92 = 0.00256
dst = decode(0) = 0
blend = 0.00256·1 + 0 = 0.00256
store = encode(0.00256) = 0.0331  →  byte 8     ✗(不是 51)
```

**测试 draw**:
```
src = decode(1.0) = 1.0    (1.0 处 EOTF 恒等)
dst = decode(8/255) = decode(0.0314) = 0.00243   (走线性段,因为 0.0314 < 0.04045)
blend = 1·0.5 + 0.00243·0.5 = 0.5012
store = encode(0.5012) = 0.7361  →  byte 188    ✗ 实测是 190,不是 188
```

### 8.3 判定

| | 背景存盘 byte | 测试 blend byte | 实测 |
|------|-------------|----------------|------|
| **src 不转换(规范)** | 51 | **190** | 190 ✓ |
| **src 也 decode(反例)** | 8 | 188 | ✗ |

差 2 字节,足以判定。**硬件不做 src 的 sRGB→linear 转换**,与规范语义一致。

---

## 9. RenderDoc 抓帧分析说明

用 RenderDoc 抓帧查看本示例的两个 attachment(sRGB 和 UNORM)时,初学者常会困惑:为什么两个 blend 方式"看起来一样"?本节系统说明 RenderDoc 在 sRGB 纹理上的显示约定、查看正确字节的方法,以及如何用 RenderDoc 复现本报告的判定。

### 9.1 RenderDoc 的纹理显示约定(关键背景)

RenderDoc 在纹理查看器(Texture Viewer)中对不同格式的纹理采取不同的显示处理:

| 纹理格式 | RenderDoc 显示处理 | 显示值的含义 |
|---------|---------------------|-------------|
| `*_SRGB` 系列 | **自动应用 sRGB EOTF decode** | 显示的是线性光强度(linear) |
| `*_UNORM` / `*_SFLOAT` 等非 sRGB | 原值显示(归一化字节 / 255) | 显示的就是字节归一化值 |
| Alpha 通道 | 永不 decode(任何格式) | 与规范一致,alpha 不参与 sRGB |

这是 RenderDoc 的正确行为 —— sRGB 编码的字节本来就需要 decode 后才能正确显示其代表的真实光强度。

### 9.2 现象:两种 attachment 显示"一样"

在假设 A(线性路径,实测已确认)成立的前提下,两个 attachment 存**不同的字节**,但表达**相同的线性光强度**:

| Attachment | 存盘字节 | 字节的含义 | RenderDoc 处理 | 显示的亮度 |
|-----------|---------|----------|----------------|-----------|
| sRGB(P2) | **190** | encode(0.5166) ≈ 线性 0.5166 的 sRGB 编码形式 | decode(190/255) | **≈ 0.515** |
| UNORM(P2) | **131** | round(0.5166·255) ≈ 线性 0.5166 的原值 | 原值 / 255 | **≈ 0.514** |

两者显示亮度差约 0.001(0.2%),**肉眼不可分辨 → 看起来一样**。

这是正确行为,不是 bug。字节不同但表达的线性光几乎相同(差异来自 UNORM 路径在背景存盘时的字节舍入),正是因为 sRGB 字节是同一线性光的编码形式。

> 注:严格的 `decode(encode(x))` 是恒等(回到原值 x),但实际硬件在 encode 后取整成字节(0.7457 → 190 表示 0.7451),decode 时用字节归一化值 0.7451 而非 0.7457,因此显示值与原始线性 blend 结果有 ±0.002 的微小差异。

### 9.3 三个 patch 的完整 RenderDoc 显示对照

下面表格列出每个 patch 在两个 attachment 上的存盘字节、RenderDoc 显示亮度,以及二者的视觉差异:

| Patch | sRGB byte | RenderDoc 显示 sRGB(decode 后) | UNORM byte | RenderDoc 显示 UNORM(原值) | 显示亮度差 |
|-------|----------|--------------------------------|-----------|----------------------------|-----------|
| P1 | 187 | decode(0.733) ≈ **0.497** | 127 | 127/255 ≈ **0.498** | 0.001(0.2%) |
| P2 | 190 | decode(0.745) ≈ **0.515** | 131 | 131/255 ≈ **0.514** | 0.001(0.2%) |
| P3 | 204 | decode(0.800) ≈ **0.603** | 155 | 155/255 ≈ **0.608** | 0.005(0.8%) |

三个 patch 的显示亮度差都 < 1%,肉眼完全看不出区别 —— 这就是用户在 RenderDoc 里看到"两种 blend 方式一样"的原因。

**关键洞察**:字节差异很大(P1 差 60 级、P2 差 59 级、P3 差 49 级),但因为 sRGB 字节经过 decode 还原成线性光后,与 UNORM 原值存储的线性光几乎相同(都是线性 blend 结果 0.5+0.5·bg_linear 的近似),所以显示亮度看起来一样。

**为何这两个值"几乎相等"而不是完全相等**:UNORM 路径在背景存盘时多了一次舍入(bg_linear=0.0331 存为 byte 8,丢失 0.44 字节的精度),所以最终 blend 结果比 sRGB 路径多了一点点误差,但都在 1 个字节以内。

### 9.4 在 RenderDoc 中查看原始字节的正确方法

不能只看预览颜色,要看**原始字节**才能区分两种 attachment。在 RenderDoc 里:

#### 方法 1:鼠标悬停查看像素信息
- 打开 **Texture Viewer**
- 选择要查看的纹理(如 `srgb_image` 或 `unorm_image`)
- 鼠标悬停在 patch 中心像素
- 窗口底部或侧边的 **Pixel Detail** / **Hover** 面板会显示该像素的原始 RGBA 值
- P2 中心像素:sRGB 附件应显示 `R: 190`(或 0.745),UNORM 附件应显示 `R: 132`(或 0.517)

#### 方法 2:切换到字节视图
- Texture Viewer 工具栏有 **Format / Range / Custom** 等选项
- 把 **Resource Format** 切换到 **Bytes** / **R8G8B8A8** / 显示原始字节
- 此时不再应用 sRGB decode,直接显示字节值
- sRGB 附件 P2 像素:190,UNORM 附件 P2 像素:132 —— 字节不同

#### 方法 3:关闭 sRGB 显示
- Texture Viewer 默认对 sRGB 纹理应用 decode
- 可以找到 **sRGB** toggle(有时在工具栏,有时在 **Range** 设置里)
- 关闭后,sRGB 纹理也按原值显示
- 此时 sRGB 附件 P2 显示为 0.745(亮灰),UNORM 附件 P2 显示为 0.517(中灰) —— 视觉上明显不同

### 9.5 完整 RenderDoc 验证流程(以 P2 为例)

下面是逐步操作流程,可在 RenderDoc 中复现本报告的判定:

**步骤 1:抓帧**
- 启动 RenderDoc,配置可执行文件路径指向 `vulkan_samples.exe`
- 命令行参数:`sample srgb_blend_verification --data-path <repo_root>`
- 启动应用,按 **F12** 或 **Capture** 抓一帧

**步骤 2:定位 offscreen 纹理**
- 打开 **Texture Viewer**
- 在纹理列表中找以下几张(由 `create_offscreen_image` 创建):
  - `srgb_image` —— 格式 `R8G8B8A8_SRGB`,768×256,包含 P1/P2/P3 三个 patch
  - `unorm_image` —— 格式 `R8G8B8A8_UNORM`,768×256,同样三个 patch
  - `display_image` —— 格式 `R8G8B8A8_SRGB`,是 `unorm_image` 字节的 raw copy,用于显示
  - 三张图都是 768×256,P1 占 x∈[0,256)、P2 占 x∈[256,512)、P3 占 x∈[512,768)

**步骤 3:查看 sRGB attachment 的字节**
- 选中 `srgb_image`
- 悬停在 P2 中心(x≈384, y≈128)
- Pixel Detail 面板应显示 `R: 190`(`G`、`B` 同值)

**步骤 4:查看 UNORM attachment 的字节**
- 选中 `unorm_image`
- 悬停在 P2 中心(x≈384, y≈128)
- Pixel Detail 面板应显示 `R: 131`(接近 132,差 1 来自硬件取整)

**步骤 5:对照判定**
- sRGB 字节 190 = `encode(0.5166)` —— 与线性假设预测的 190 吻合
- 若是非线性假设,sRGB 字节应为 153(`0.5 + 0.5·0.2 = 0.6 → byte 153`)
- 实际看到 190 ≠ 153 → **线性假设成立**

### 9.6 RenderDoc 的常见陷阱

#### 陷阱 1:只看预览颜色,不看字节

RenderDoc 对 sRGB 纹理自动 decode,导致 sRGB 字节 190 显示为线性 ≈0.515;UNORM 字节 131 显示为 ≈0.514。两者亮度几乎相同,容易误以为"两种 blend 结果一样"。

**正确做法**:看 Pixel Detail 面板的原始 RGBA 值,而不是预览颜色。

#### 陷阱 2:RenderDoc 可能覆盖 swapchain 格式

RenderDoc 抓帧时可能把 swapchain 格式从 sRGB 强制改为 UNORM(工具行为,便于自身抓取/回放)。这会影响**直接渲染到 swapchain 的 blend 行为**,但**不影响本示例的 offscreen attachment**(srgb_image 和 unorm_image 的格式是程序自己创建的,RenderDoc 不会改)。

本示例的判定**完全基于 offscreen attachment 的字节读回**,与 swapchain 格式无关,因此 RenderDoc 的 swapchain 覆盖不影响判定。

#### 陷阱 3:混淆 display_image 和 unorm_image

本示例有三张 offscreen 图:
- `srgb_image` (sRGB 格式):真实 sRGB blend 结果,字节 190(P2)
- `unorm_image` (UNORM 格式):控制组,blend 结果字节 131(P2)
- `display_image` (sRGB 格式):用 `vkCmdCopyImage` 把 `unorm_image` 字节 raw-copy 过来,字节也是 131(P2),只是格式标记为 sRGB

`display_image` 的字节与 `unorm_image` **完全相同**(都是 131),只是格式不同。它的作用是让 UNORM 控制组的结果能正确显示在 sRGB swapchain 上(详见第 6 节方法学)。

#### 陷阱 4:在 swapchain 上对比字节

不要用 swapchain 上的字节做判定 —— swapchain 上的字节经过 blit 操作,涉及 src/dst 格式配对,容易混淆。

**正确做法**:在 offscreen 的 `srgb_image` 和 `unorm_image` 上读字节。

### 9.7 视觉等价性本身是一种证明

如果硬件走的是**非线性假设(假设 B)**,sRGB attachment 应存 byte 153(P2 上 `0.5 + 0.5·0.2 = 0.6`)。

此时 RenderDoc 显示:
- sRGB byte 153 → `decode(0.6) ≈ 0.3185`(明显暗)
- UNORM byte 131 → `131/255 ≈ 0.5137`(中灰)

两者亮度差约 0.20(38%),**肉眼可见明显区别**。

但你实际看到的是两者亮度相同 —— 这恰恰证明 sRGB attachment 的字节确实经过了 encode(把线性 blend 结果 0.5166 编码成 byte 190),而非 sRGB 空间直接 blend 给出的 byte 153。**视觉等价性是线性路径成立的副作用**。

### 9.8 总结:RenderDoc 看到的是对的,只是看错了层面

| 观测层面 | sRGB attachment | UNORM attachment | 是否相同 |
|---------|-----------------|------------------|---------|
| 存盘字节 | 190(P2) | 131(P2) | ❌ 不同(差 59 级) |
| RenderDoc 显示亮度 | ≈ 0.515 | ≈ 0.514 | ✅ 几乎相同(差 0.2%) |
| 表达的线性光 | 0.5166(线性 blend 结果) | 0.5166(同一线性 blend 结果) | ✅ 相同(差异来自字节舍入) |

**结论**:RenderDoc 显示一样 ≠ 字节一样。本示例的判定**基于字节**,而非显示亮度。字节明显不同(190 vs 131),证明 sRGB attachment 走了 encode 路径,即线性 blend 路径。

---

## 10. 工程实践指南

### 10.1 shader 端:全程线性计算

- 在 fragment shader 里**始终按线性空间处理颜色**(光照、混合、求平均等)
- 输出值即视为线性,attach 到 sRGB 附件,硬件自动负责写回时的编码
- 不要在 shader 里手动做 `pow(c, 1/2.2)` 之类的编码,除非附件不是 sRGB 格式

### 10.2 资源端:sRGB 格式纹理自动 decode

- 用 `VK_FORMAT_R8G8B8A8_SRGB` 等格式创建颜色纹理,采样时硬件自动 decode 到线性
- sampler 不需要任何特殊设置,采样结果直接是线性值,shader 内可以直接做线性运算
- 注意:**只对 RGB 做 decode,alpha 通道不做**(见规范第 5 节引用)

### 10.3 附件端:sRGB 格式附件自动 encode

- color attachment 用 `VK_FORMAT_R8G8B8A8_SRGB`,blend 自动走线性路径
- attachment 用 `VK_FORMAT_R8G8B8A8_UNORM`,blend 走原始空间路径(无 encode/decode)
- 切换 attachment 格式即可改变 blend 行为,无需改 shader

### 10.4 swapchain 端

- 推荐用 sRGB swapchain(`VK_FORMAT_B8G8R8A8_SRGB` 等)
- 这样往 swapchain 写时自动编码,显示出来颜色正确
- 注意 RenderDoc 可能强制覆盖 swapchain 格式为 UNORM,这是工具行为不是程序问题

### 10.5 中间渲染目标

- 用 sRGB 附件:blend 自动线性,但每次 blend 都有一次 decode + encode 的开销
- 用 UNORM 附件 + shader 内手动 decode/encode:开销类似但灵活,适合自定义混合
- 对于自定义混合(如 PBR 加色光照),input attachment + shader 实现:用 `VK_EXT_rasterization_order_attachment_access` 保证顺序

### 10.6 性能注意事项

- 硬件 sRGB encode/decode 在大多数 GPU 上是免费的(专用硬件单元)
- Tiled GPU 上,sRGB 附件会触发 framebuffer 在 tile 内存中存储 decoded 形式,减少重复 decode
- 但如果混合了 sRGB 附件 + UNORM 附件 + shader 内手动 decode,可能引入不必要的转换开销

---

## 11. 常见误区澄清

### 误区 1:"sRGB 格式只是字节排列方式"

错。sRGB 格式触发了**采样时 decode + 写入时 encode + blend 时 dst decode/result encode** 这一系列硬件行为。把 sRGB 附件换成 UNORM,blend 结果会**不同**(本实验已证明)。

### 误区 2:"shader 输出的 0.5 会被硬件 decode 成 0.214"

错。src 不做转换(本实验已证明,P2 实测 190 而非 188)。shader 输出的就是线性值。

### 误区 3:"alpha 通道也走 sRGB"

错。规范明确:"The value of A is never sRGB encoded." alpha 始终是线性的。

### 误区 4:"sRGB 和 UNORM 字节相同"

不一定。本实验 P2:sRGB 存 190,UNORM 存 132。**字节不同,但表达相同的线性光**(因为 sRGB 字节 190 decode 后 = 线性 0.5166,UNORM 字节 132 / 255 = 0.5176)。

### 误区 5:"在 sRGB 空间做 blend 没什么区别"

大错。P2 上,线性 blend 给 byte 190(线性 0.5166),sRGB 空间 blend 给 byte 153(显示亮度 decode(0.6)= 0.3185)。两者显示亮度差 60%以上,视觉上很明显。

---

## 12. 完整字节级数据(可复现验证)

### 12.1 实测读回字节(sRGB 与 UNORM attachment 各一份)

```
=== sRGB Hardware Blend Verification ===
Patch  bg   sRGB-act  lin-exp  nonlin-exp  UNORM-act  unorm-exp  verdict
  P1    0   187       188      128         127       128       LINEAR
  P2   51   190       190      153         131       132       LINEAR
  P3  128   204       205      192         155       155       LINEAR
=========================================
```

### 12.2 复现命令

```powershell
$work = "E:\z00450253\code\Vulkan-Samples\build\app\bin\release\AMD64"
& "$work\vulkan_samples.exe" sample srgb_blend_verification `
    --data-path "E:\z00450253\code\Vulkan-Samples" `
    --stop-after-frame 3
```

交互运行(去掉 `--stop-after-frame`):
- 窗口上半显示 sRGB attachment 的三个 patch(真实 sRGB blend)
- 窗口下半显示 UNORM attachment 的三个 patch(控制组)
- UI overlay 显示上表 + 总判定
- 离线运行结果写入 `$work/srgb_blend_verdict.txt`

### 12.3 各 patch 的完整推导(以 P2 为例)

**P2:bg sRGB byte = 51**

```
bg_linear = decode(51/255) = decode(0.2)
         = ((0.2 + 0.055) / 1.055)^2.4
         = (0.2417)^2.4
         = 0.0331
```

**sRGB attachment**

| 步骤 | 计算 | 结果 |
|------|------|------|
| 背景存盘 byte | `encode(0.0331)·255` = `0.2·255` | **51** ✓ |
| dst_linear | `decode(0.2)` = 0.0331 | 0.0331 |
| 线性 blend 结果(linear) | `0.5 + 0.5·0.0331` = 0.5166 | 0.5166 |
| → encode → byte | `encode(0.5166)·255` = `1.055·0.5166^(1/2.4) − 0.055 = 0.746` → `round(190.2)` | **190** |
| 非线性 blend(byte/255 直算) | `0.5 + 0.5·0.2` = 0.6 → `round(153.0)` | **153** |

**UNORM 控制**

| 步骤 | 计算 | 结果 |
|------|------|------|
| 背景存盘 byte | `0.0331·255` = 8.44 → `round` | 8 |
| dst | `8/255` ≈ 0.0314(或直接用 bg_linear = 0.0331) | 0.0331 |
| blend 结果 | `0.5 + 0.5·0.0331` = 0.5166 → `round(131.7)` | **132** |

**实测**:`sRGB=190, lin=190, nonlin=153, UNORM=131, unorm=132` → ✅ LINEAR

---

## 13. 结论

1. **当 color attachment 为 sRGB 格式时,Vulkan 硬件 blend 单元执行以下三步处理:**
   - dst 字节 → decode → 线性值
   - src(linear)与 dst(linear)在线性空间执行 blend 运算
   - 结果 → encode → sRGB 字节存盘

2. **src(fragment shader 输出)不做任何 sRGB 转换** —— shader 自己保证输出线性值。

3. **alpha 通道永不参与 sRGB 转换**。

4. **UNORM attachment 没有任何 sRGB 处理**,blend 在原始字节空间进行,与 sRGB attachment 在数学上明显不同。

5. **同一组线性光在 sRGB 和 UNORM attachment 中以不同字节存储,但视觉等价**(前提是 sRGB 路径正确执行)。这种等价性是线性路径成立的副产物,而非矛盾。

6. **工程实践中,正确做法是**:shader 全程线性计算 + sRGB 格式纹理(自动 decode)+ sRGB 格式 color attachment(自动 encode + 线性 blend)+ sRGB 格式 swapchain。整个管道在硬件层面自动管理 sRGB 转换,无需任何 shader 内手动 pow 运算。

---

## 附录 A:相关规范章节

- Vulkan Specification, "The Framebuffer" 章节, "Blend Operations" 小节
  - URL: https://docs.vulkan.org/spec/latest/chapters/framebuffer.html#framebuffer-blendoperations
- IEC 61966-2-1(sRGB 标准)
- Khronos Data Format Specification,"sRGB EOTF" 节

## 附录 B:相关代码位置

| 文件 | 用途 |
|------|------|
| `samples/api/srgb_blend_verification/srgb_blend_verification.{h,cpp}` | 示例实现 |
| `samples/api/srgb_blend_verification/CMakeLists.txt` | 构建配置 |
| `samples/api/srgb_blend_verification/README.adoc` | 用户文档 |
| `shaders/srgb_blend_verification/glsl/quad.vert` | 全屏三角形顶点着色器 |
| `shaders/srgb_blend_verification/glsl/quad.frag` | push-constant 纯色片段着色器 |
