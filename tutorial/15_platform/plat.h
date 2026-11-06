/* tutorial/15_platform/plat.h — 平台抽象层（对外接口，零平台细节） */
#ifndef TUT_PLAT_H
#define TUT_PLAT_H

/* 跨平台休眠（毫秒）与单调时钟（毫秒）。业务代码只调这两个，不碰任何 #ifdef。 */
void     sk_sleep_ms(int ms);
long long sk_now_ms(void);

#endif
