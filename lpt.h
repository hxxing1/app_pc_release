#ifndef LPT_H_
#define LPT_H_

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================*
 * 配置项
 *===========================================================================*/
// LPT_MAX_TASKS 宏已废弃，现采用单向队列动态管理协程

/*===========================================================================*
 * 协程状态定义
 *===========================================================================*/
#define LPT_STATE_READY      0   // 就绪态：可以执行
#define LPT_STATE_SLEEP      1   // 睡眠态：延时中，允许系统低功耗休眠
#define LPT_STATE_WAIT_NOTI  2   // 等待态：等待外部中断通知，允许系统低功耗休眠
#define LPT_STATE_STOP       3   // 终止态：协程已结束运行

/*===========================================================================*
 * 数据结构
 *===========================================================================*/
// 协程控制块 (CCB) - 极简设计，无上下文指针，压榨每一字节 RAM
typedef struct lpt_t {
    uint16_t lc;           // 行号记录器 (Line Counter)
    uint8_t  status;       // 协程当前状态
    uint32_t start_tick;   // 计时的起始系统节拍
    uint32_t delay_tick;   // 目标的延时/超时节拍数
    void (*func)(struct lpt_t *pt); // 任务函数指针
    struct lpt_t *next;    // 链表指针，用于单向队列组织
} lpt_t;

/*===========================================================================*
 * API 声明
 *===========================================================================*/
// 初始化与注册
bool lpt_register(lpt_t *pt, void (*func)(lpt_t *));
// 核心调度器，放在 main 的 while(1) 中
void lpt_run(void);
// 中断通知，用于在 ISR 中瞬间唤醒协程
void lpt_notify(lpt_t *pt);
// 休眠锁机制：保护关键外设运行期不被深度休眠打断
void lpt_sleep_lock(void);
void lpt_sleep_unlock(void);

// 需要移植层提供的接口
extern uint32_t sys_tick_get(void);
extern void lpt_port_critical_enter(void);
extern void lpt_port_critical_exit(void);
extern void lpt_port_enter_sleep(uint32_t sleep_ms, bool is_locked);

/*===========================================================================*
 * 核心宏定义 (语法糖)
 *===========================================================================*/
// 协程开始与结束 (必须成对出现)
#define LPT_BEGIN(pt)    switch((pt)->lc) { case 0:
#define LPT_END(pt)      } (pt)->status = LPT_STATE_STOP; (pt)->lc = 0; return;

// 主动交出 CPU 控制权，下一轮轮询立刻继续
#define LPT_YIELD(pt) \
    do { \
        (pt)->lc = __LINE__; return; case __LINE__:; \
    } while(0)

// 相对延时，自动计算休眠并挂起协程
#define LPT_DELAY(pt, ms) \
    do { \
        (pt)->start_tick = sys_tick_get(); \
        (pt)->delay_tick = (ms); \
        (pt)->status = LPT_STATE_SLEEP; \
        (pt)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (pt)->start_tick) < (pt)->delay_tick) { \
            return; \
        } \
        (pt)->status = LPT_STATE_READY; \
    } while(0)

// 绝对周期延时，严格防漂移 (参数 static_last_tick 必须是 static 局部变量)
#define LPT_DELAY_UNTIL(pt, static_last_tick, interval_ms) \
    do { \
        (pt)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (static_last_tick)) < (interval_ms)) { \
            (pt)->start_tick = sys_tick_get(); \
            (pt)->delay_tick = (interval_ms) - (uint32_t)((pt)->start_tick - (static_last_tick)); \
            (pt)->status = LPT_STATE_SLEEP; \
            return; \
        } \
        (static_last_tick) += (interval_ms); \
        (pt)->status = LPT_STATE_READY; \
    } while(0)

// 等待中断/外部事件通知，并带超时功能
#define LPT_WAIT_NOTIFY_TIMEOUT(pt, timeout_ms, out_is_timeout_ptr) \
    do { \
        (pt)->start_tick = sys_tick_get(); \
        (pt)->delay_tick = (timeout_ms); \
        (pt)->status = LPT_STATE_WAIT_NOTI; \
        (pt)->lc = __LINE__; case __LINE__: \
        if ((uint32_t)(sys_tick_get() - (pt)->start_tick) >= (pt)->delay_tick) { \
            *(out_is_timeout_ptr) = true; \
            (pt)->status = LPT_STATE_READY; \
        } else if ((pt)->status == LPT_STATE_READY) { \
            *(out_is_timeout_ptr) = false; \
        } else { \
            return; \
        } \
    } while(0)

// 子协程调用：父协程挂起等待子协程完成
// child 需用 static lpt_t child = {0} 初始化为零
#define LPT_CALL(pt, child, child_func) \
    do { \
        (pt)->lc = __LINE__; \
        if ((child)->status == LPT_STATE_STOP) { \
            (child)->lc = 0; \
            (child)->status = LPT_STATE_READY; \
        } \
        case __LINE__: \
        if ((child)->status != LPT_STATE_STOP) { \
            child_func(child); \
            if ((child)->status == LPT_STATE_SLEEP || (child)->status == LPT_STATE_WAIT_NOTI) { \
                (pt)->status = (child)->status; \
                (pt)->start_tick = (child)->start_tick; \
                (pt)->delay_tick = (child)->delay_tick; \
            } \
            if ((child)->status != LPT_STATE_STOP) { \
                return; \
            } \
        } \
    } while(0)

#endif // LPT_H_


