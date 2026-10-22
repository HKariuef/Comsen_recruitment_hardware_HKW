# HCalculate

HCalculate 是基于 `Comsen_recruitment_hardware/example` 重新建立的 STM32F103C8T6 计算器工程。它保留 example 已验证的硬件引脚、TTP229 触摸读取、滤波/组合识别、1602 显示和 FreeRTOS 五任务结构，因此 30 个实体按键仍按 example 的标定表连接到对应数字或字符。

## 已完成的招新任务

1. **硬件基础**：沿用 example 的 PB10–PB15 LCD、PB6/PB7 与 PB8/PB9 两组 TTP229、PA11/PA12 USB 配置；TIM3 为 1 MHz，PC13 作为心跳指示。
2. **串口与显示**：启动 USB CDC。电脑发来的可打印字符进入中断安全的环形缓冲，由 `StartDefaultTask` 消费并显示在 1602；退格、换行和超长输入都有处理。
3. **触摸按键**：使用 example 的 `ttp229`、`touch_filter` 和 `touch_model` 静态库，保留每 10 ms 扫描、滤波和按键组合识别。
4. **基本计算器**：使用 example 的计算任务实现数字、小数、四则运算、AC 清空和结果显示。
5. **函数扩展**：使用 example 的计算引擎处理括号、优先级、三角函数、开方等函数。
6. **扩展功能**：新增复数计算入口和一元线性最小二乘统计功能。

## 新增功能的使用方式

USB 串口中发送一整行命令并回车，结果会同时回传到终端并显示在 LCD 两行。开发板启动后会发送 `HCalculate READY`；普通字符在换行时以 `RX:` 回传，也会按原任务显示。

统计功能不是独立的串口开关，而是 MODE 菜单中追加在 example 原有项目后的 `STAT` 项：按 MODE，使用上下键移动到 `> STAT`，按 OK 或 EXE 进入；此时 LCD 显示 `STAT MODE`，串口统计命令才会执行。再次按 MODE 后选择 `COMP`、`CMPLX` 或 `ANGLE UNIT`，即可离开统计模式。开机画面和普通 COMP/CMPLX/ANGLE UNIT 界面仍由 example 原版应用显示。

```text
C sqrt(-1)+2
```

`C` 或 `COMPLEX` 前缀会以 `allow_complex=1` 调用 example 的表达式引擎，第一行显示 `Re=`，第二行显示 `Im=`。上一次成功结果会作为 `ANS` 传给下一条复数命令。

硬件上的 `MODE` 菜单仍保留 example 的 `COMP/CMPLX` 选择；选择 `CMPLX` 后可直接用原有数字和运算符按键输入复数表达式。USB 命令适合快速验证和输入较长表达式。

```text
STAT 1,2,3,4;2,4,5,8
```

分号左边是自变量 x，右边是等长的因变量 y。程序使用

```text
a = Σ((xi-x̄)(yi-ȳ)) / Σ((xi-x̄)²)
b = ȳ - a x̄
y = ax + b
R² = 1 - Σ(yi-(axi+b))² / Σ((yi-ȳ)²)
```

计算并显示拟合式和 R²。支持最多 16 组数据；数据数量不相等、x 无方差或格式错误会显示错误提示。

统计模式还提供五类常用分析：`DESC 1,2,3` 计算样本数、均值、标准差和最小值；`CORR 1,2,3;2,4,5` 计算 Pearson 相关系数和 R²；`PRED 1,2,3;2,4,5;4` 用线性拟合预测指定 x；`QUAD 1,2,3;1,4,9` 用正规方程求二次拟合；`ZSCORE 1,2,10` 找出绝对 z 分数最大的样本。所有命令都复用固定数组和浮点计算，最多支持 16 个数据点。

## 按键映射

按键识别沿用 example 的标定表：

```text
SHIFT BACK MODE ↑    OK
(     )    ←   ↓    →
7     8    9   DEL  AC
4     5    6   ×    ÷
1     2    3   +    -
0     .    ×10^ FMT EXE
```

硬件计算器的普通输入、括号和函数仍由 example 的 `calculator_app` 任务处理；新增命令通过 USB CDC 输入，避免占用已经固定的数字和运算符按键。

## 编译

在 WSL/Linux 中执行：

```bash
cd HCalculate
./build.sh Release
```

产物位于 `build/Release/HCalculate.hex`、`HCalculate.bin` 和 `HCalculate.elf`。Windows PowerShell 可运行 `pwsh -File .\build.ps1 Release`。烧录前确认 BOOT0=1，烧录后恢复 BOOT0=0 并复位。

## 目录中的自写代码

- `App/serial_text.c`：USB 环形缓冲、普通文本显示和命令触发。
- `App/hcalculate_math.c/.h`：复数命令、固定点显示格式化和最小二乘计算。
- `Core/Src/main.c`：USB CDC 初始化以及 example 五个任务入口。

`lib/` 中的六个静态库继续直接提供 example 的原版计算器应用、计算引擎、LCD、TTP229、滤波和触摸模型；`App/hcalculate_mode.c` 只对 MODE 菜单和 STAT 页面做外围扩展，不替换原版应用任务。


