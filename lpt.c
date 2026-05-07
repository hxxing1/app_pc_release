#include "lpt.h"
#define NULL ((void *)0)
static lpt_t *s_task_head = NULL;
static lpt_t *s_task_tail = NULL;
static volatile uint8_t s_sleep_lock_count = 0; // 休眠锁计数器
static volatile bool s_pending_notify = false;  // 挂起的唤醒事件标志

// 注册协程任务
bool lpt_register(lpt_t *pt, void (*func)(lpt_t *)) {
    if (!pt || !func) return false;
    
    // 检查是否已经注册过，避免成环
    lpt_t *curr = s_task_head;
    while (curr) {
        if (curr == pt) return false;
        curr = curr->next;
    }

    pt->lc = 0;
    pt->status = LPT_STATE_READY;
    pt->func = func;
    pt->next = NULL;

    if (s_task_head == NULL) {
        s_task_head = pt;
        s_task_tail = pt;
    } else {
        s_task_tail->next = pt;
        s_task_tail = pt;
    }
    
    return true;
}

// ISR 唤醒通知
void lpt_notify(lpt_t *pt) {
    if (pt->status != LPT_STATE_READY) {
        pt->status = LPT_STATE_READY;
        s_pending_notify = true;
    }
}

// 获取休眠锁（阻止进入深度休眠）
void lpt_sleep_lock(void) {
    lpt_port_critical_enter();
    if (s_sleep_lock_count < 255) {
        s_sleep_lock_count++;
    }
    lpt_port_critical_exit();
}

// 释放休眠锁（允许进入深度休眠）
void lpt_sleep_unlock(void) {
    lpt_port_critical_enter();
    if (s_sleep_lock_count > 0) {
        s_sleep_lock_count--;
    }
    lpt_port_critical_exit();
}

// 核心调度器引擎 (Tickless 架构)
void lpt_run(void) {
    uint32_t min_sleep = 0xFFFFFFFF;
    uint32_t current_tick = sys_tick_get();
    bool can_sleep = true;

    lpt_t *pt = s_task_head;
    while (pt != NULL) {
        // 过滤已停止的任务
        if (pt->status == LPT_STATE_STOP) {
            pt = pt->next;
            continue;
        }

        // 1. 处理睡眠/超时状态
        if (pt->status == LPT_STATE_SLEEP || pt->status == LPT_STATE_WAIT_NOTI) {
            uint32_t elapsed = current_tick - pt->start_tick;
            if (elapsed >= pt->delay_tick) {
                pt->status = LPT_STATE_READY; // 已超时，唤醒
            } else {
                uint32_t remain = pt->delay_tick - elapsed;
                if (remain < min_sleep) min_sleep = remain;
            }
        }

        // 2. 执行就绪任务
        if (pt->status == LPT_STATE_READY) {
            pt->func(pt); // 调用用户协程逻辑
            
            // 如果执行完依然是 READY (遇到 Yield，或者没有被挂起)，则不能休眠
            if (pt->status == LPT_STATE_READY) {
                can_sleep = false; 
            }
        }

        pt = pt->next;
    }

    // 3. 原子化执行低功耗休眠
    //    WFI 在中断关闭时仍能唤醒 CPU，ISR 在开中断后立即执行，
    //    因此关中断→检查→WFI 是一个闭合的竞态窗口。
    lpt_port_critical_enter();
    
    // 检查在计算 can_sleep 后、关中断前，是否发生了中断唤醒事件
    if (s_pending_notify) {
        s_pending_notify = false;
        can_sleep = false; 
    }

    if (can_sleep && min_sleep > 0 && min_sleep != 0xFFFFFFFF) {
        bool is_locked = (s_sleep_lock_count > 0);
        lpt_port_enter_sleep(min_sleep, is_locked);
    }
    lpt_port_critical_exit();
}

