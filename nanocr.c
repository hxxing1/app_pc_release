#include "nanocr.h"

static cr_t *s_tasks[CR_MAX_TASKS];
static uint8_t s_task_count = 0;
static volatile uint8_t s_sleep_lock_count = 0; // 休眠锁计数器

// 注册协程任务
bool cr_register(cr_t *cr, void (*func)(cr_t *)) {
    if (s_task_count >= CR_MAX_TASKS) return false;
    cr->lc = 0;
    cr->status = CR_STATE_READY;
    cr->func = func;
    s_tasks[s_task_count++] = cr;
    return true;
}

// ISR 唤醒通知
void cr_notify(cr_t *cr) {
    if (cr->status != CR_STATE_READY) {
        cr->status = CR_STATE_READY;
    }
}

// 获取休眠锁（阻止进入深度休眠）
void cr_sleep_lock(void) {
    cr_port_critical_enter();
    if (s_sleep_lock_count < 255) {
        s_sleep_lock_count++;
    }
    cr_port_critical_exit();
}

// 释放休眠锁（允许进入深度休眠）
void cr_sleep_unlock(void) {
    cr_port_critical_enter();
    if (s_sleep_lock_count > 0) {
        s_sleep_lock_count--;
    }
    cr_port_critical_exit();
}

// 核心调度器引擎 (Tickless 架构)
void cr_run(void) {
    uint32_t min_sleep = 0xFFFFFFFF;
    uint32_t current_tick = sys_tick_get();
    bool can_sleep = true;

    for (int i = 0; i < s_task_count; i++) {
        cr_t *cr = s_tasks[i];

        // 过滤已停止的任务
        if (cr->status == CR_STATE_STOP) continue;

        // 1. 处理睡眠/超时状态
        if (cr->status == CR_STATE_SLEEP || cr->status == CR_STATE_WAIT_NOTI) {
            uint32_t elapsed = current_tick - cr->start_tick;
            if (elapsed >= cr->delay_tick) {
                cr->status = CR_STATE_READY; // 已超时，唤醒
            } else {
                uint32_t remain = cr->delay_tick - elapsed;
                if (remain < min_sleep) min_sleep = remain;
            }
        }

        // 2. 执行就绪任务
        if (cr->status == CR_STATE_READY) {
            cr->func(cr); // 调用用户协程逻辑

            if (cr->status == CR_STATE_READY) {
                can_sleep = false;
            } else if (cr->status == CR_STATE_SLEEP || cr->status == CR_STATE_WAIT_NOTI) {
                uint32_t elapsed = current_tick - cr->start_tick;
                if (elapsed >= cr->delay_tick) {
                    cr->status = CR_STATE_READY;
                    can_sleep = false;
                } else {
                    uint32_t remain = cr->delay_tick - elapsed;
                    if (remain < min_sleep) min_sleep = remain;
                }
            }
        }
    }

    // 3. 执行低功耗休眠
    if (can_sleep && min_sleep > 0 && min_sleep != 0xFFFFFFFF) {
        bool is_locked = (s_sleep_lock_count > 0);
        cr_port_enter_sleep(min_sleep, is_locked); 
    }
}

