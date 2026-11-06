# 第 17 课 · Android JNI 外壳：第二个前端 / 演化链终点
> 分类：跨平台代码

> 演化链第 17 步的完整可编译快照：`tutorial/mini/steps/step17_android/`
> 源码讲义：[`docs/teaching/17_android.md`](../../../../docs/teaching/17_android.md)
> 同号独立小程序：`android/（真实产品工程，无 tutorial 小程序）`
> 真实工程落点：`android/app/src/main/cpp/skedaddle_jni.cpp`、`src/utils/plat.c`

**这一课新增**：mini_jni_render_frame 写入 jintArray 等价物并补 alpha；打印 MINI-FINAL-OK 指向 MAP.md。

**怎么跑**（在本目录下）：

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror -lm -o mini main.c && ./mini
```

跑完会打印 `MINI-STEP-00-OK` … `MINI-STEP-17-OK`，写出 `mini_out.bmp`，
末行 `MINI-CHAIN-OK steps=18 ink=… gates=…/0`。

与上一步的源码差异：[`16_to_17.diff`](../../diffs/16_to_17.diff)（只含源码文件，`NOTE.md` 说明页不计入）。

**累加不变量**：本目录 = 上一步的全部文件（逐字不变） + 新增
`step17.h` + `main.c` 在两个锚点前各插入的一行。任何旧文件都不删改 ——
由 `scripts/check_mini_chain.py` 的「行子序列」判据独立验收。
