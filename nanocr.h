#ifndef NANOCR_H_
#define NANOCR_H_

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================*
 * 配置项
 *===========================================================================*/
#define CR_MAX_TASKS    8       // 框架支持的最大协程数，根据项目需求修改即可

/*===========================================================================*
 * 协程状态定义
 *===========================================================================*/
#define CR_STATE_READY      0   // 就绪态：可以执行
#define CR_STATE_SLEEP      1   // 睡眠态：延时中，允许系统低功耗休眠
#define CR_STATE_WAIT_NOTI  2   // 等待态：等待外部中断通知，允许系统低功耗休眠
#define CR_STATE_STOP       3   // 终止态：协程已结束运行

/*===========================================================================*
 * 数据结构
 *===========================================================================*/
// 协程控制块 (CCB) - 极简设计，无上下文指针，压榨每一字节 RAM
typedef struct cr_t {
    uint16_t lc;           // 行号记录器 (Line Counter)
    uint8_t  status;       // 协程当前状态
    uint32_t start_tick;   // 计时的起始系统节拍
    uint32_t delay_tick;   // 目标的延时/超时节拍数
    void (*func)(struct cr_t *cr); // 任务函数指针
} cr_t;

/*===========================================================================*
 * API 声明
 *===========================================================================*/
// 初始化与注册
bool cr_register(cr_t *cr, void (*func)(cr_t *));
// 核心调度器，放在 main 的 while(1) 中
void cr_run(void);
// 中断通知，用于在 ISR 中瞬间唤醒协程
void cr_notify(cr_t *cr);
// 休眠锁机制：保护关键外设运行期不被深度休眠打断
void cr_sleep_lock(void);
void cr_sleep_unlock(void);

// 需要移植层提供的接口
extern uint32_t sys_tick_get(void);
extern void cr_port_critical_enter(void);
extern void cr_port_critical_exit(void);
extern void cr_port_enter_sleep(uint32_t sleep_ms, bool is_locked);

/*===========================================================================*
 * 核心宏定义 (语法糖)
 *===========================================================================*/
// 协程开始与结束 (必须成对出现)
#define CR_BEGIN(cr)    switch((cr)->lc) { case 0:
#define CR_END(cr)      } (cr)->status = CR_STATE_STOP; (cr)->lc = 0; return;

// 主动交出 CPU 控制权，下一轮轮询立刻继续
#define CR_YIELD(cr) \
    do { \
        (cr)->lc = __LINE__; return; case __LINE__:; \
    } while(0)

// 相对延时，自动计算休眠并挂起协程
#define CR_DELAY(cr, ms) \
    do { \
        (cr)->start_tick = sys_tick_get(); \
        (cr)->delay_tick = (ms); \
        (cr)->status = CR_STATE_SLEEP; \
        (cr)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (cr)->start_tick) < (cr)->delay_tick) { \
            return; \
        } \
        (cr)->status = CR_STATE_READY; \
    } while(0)

// 绝对周期延时，严格防漂移 (参数 static_last_tick 必须是 static 局部变量)
#define CR_DELAY_UNTIL(cr, static_last_tick, interval_ms) \
    do { \
        (cr)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (static_last_tick)) < (interval_ms)) { \
            (cr)->start_tick = sys_tick_get(); \
            (cr)->delay_tick = (interval_ms) - (uint32_t)((cr)->start_tick - (static_last_tick)); \
            (cr)->status = CR_STATE_SLEEP; \
            return; \
        } \
        (static_last_tick) += (interval_ms); \
        (cr)->status = CR_STATE_READY; \
    } while(0)

// 等待中断/外部事件通知，并带超时功能
#define CR_WAIT_NOTIFY_TIMEOUT(cr, timeout_ms, out_is_timeout_ptr) \
    do { \
        (cr)->start_tick = sys_tick_get(); \
        (cr)->delay_tick = (timeout_ms); \
        (cr)->status = CR_STATE_WAIT_NOTI; \
        (cr)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (cr)->start_tick) >= (cr)->delay_tick) { \
            *(out_is_timeout_ptr) = true; \
            (cr)->status = CR_STATE_READY; \
        } else if ((cr)->status == CR_STATE_READY) { \
            *(out_is_timeout_ptr) = false; \
        } else { \
            return; \
        } \
    } while(0)

// 子协程调用：父协程挂起等待子协程完成
// child 需用 static cr_t child = {0} 初始化为零
#define CR_CALL(cr, child, child_func) \
    do { \
        (cr)->lc = __LINE__; \
        if ((child)->status == CR_STATE_STOP) { \
            (child)->lc = 0; \
            (child)->status = CR_STATE_READY; \
        } \
        case __LINE__: \
        if ((child)->status != CR_STATE_STOP) { \
            child_func(child); \
            if ((child)->status == CR_STATE_SLEEP || (child)->status == CR_STATE_WAIT_NOTI) { \
                (cr)->status = (child)->status; \
                (cr)->start_tick = (child)->start_tick; \
                (cr)->delay_tick = (child)->delay_tick; \
            } \
            if ((child)->status != CR_STATE_STOP) { \
                return; \
            } \
        } \
    } while(0)

#endif // NANOCR_H_


