# Hardware

硬件资料按光学、PCB 和结构件分开整理。

- `optics/zemax`: Zemax 光学设计文件。
- `pcb/schematics`: 两块电路板的 PDF 原理图。
- `pcb/eda-archives/G431图像数据处理板.zip`: STM32G431 图像数据处理板工程压缩包。
- `pcb/eda-archives/TCD1304驱动板.zip`: TCD1304 驱动板工程压缩包。
- `mechanical/step`: 3D 打印和结构装配用 STEP 文件。

当前 `pcb/eda-archives` 目录中的压缩包外层文件名已经按板卡用途整理；使用时建议与 `pcb/schematics` 中同名原理图 PDF 对照确认。

这些文件对应原型设计阶段资料，开板或加工前建议重新检查尺寸、公差、接口、连接器朝向和器件封装。
