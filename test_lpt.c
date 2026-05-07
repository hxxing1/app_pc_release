#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "lpt.h"

volatile uint32_t g_sys_tick = 0;
uint32_t sys_tick_get(void) {
    return g_sys_tick;
}

void lpt_port_critical_enter(void) { }
void lpt_port_critical_exit(void) { }

static bool enter_sleep_called = false;
static uint32_t enter_sleep_ms = 0;
static bool enter_sleep_locked = false;

void lpt_port_enter_sleep(uint32_t sleep_ms, bool is_locked) {
    enter_sleep_called = true;
    enter_sleep_ms = sleep_ms;
    enter_sleep_locked = is_locked;
}

static lpt_t task_yield = {0};
static int yield_count = 0;
static bool yield_task_done = false;

static lpt_t task_delay = {0};
static bool delay_task_done = false;

static lpt_t task_wait = {0};
static bool wait_task_done = false;
static bool wait_task_timeout = false;

static lpt_t task_parent = {0};
static lpt_t task_child = {0};
static bool call_task_done = false;
static int parent_steps = 0;
static int child_steps = 0;

void task_yield_func(lpt_t *cr) {
    LPT_BEGIN(cr);
    yield_count++;
    LPT_YIELD(cr);
    yield_count++;
    LPT_YIELD(cr);
    yield_count++;
    yield_task_done = true;
    LPT_END(cr);
}

void task_delay_func(lpt_t *cr) {
    LPT_BEGIN(cr);
    LPT_DELAY(cr, 10);
    delay_task_done = true;
    LPT_END(cr);
}

void task_wait_func(lpt_t *cr) {
    static bool timeout = false;
    LPT_BEGIN(cr);
    LPT_WAIT_NOTIFY_TIMEOUT(cr, 50, &timeout);
    wait_task_timeout = timeout;
    wait_task_done = true;
    LPT_END(cr);
}

void task_child_func(lpt_t *cr) {
    LPT_BEGIN(cr);
    child_steps++;
    LPT_YIELD(cr);
    child_steps++;
    LPT_END(cr);
}

void task_parent_func(lpt_t *cr) {
    LPT_BEGIN(cr);
    LPT_CALL(cr, &task_child, task_child_func);
    parent_steps++;
    call_task_done = true;
    LPT_END(cr);
}

static bool run_one_cycle(void) {
    enter_sleep_called = false;
    enter_sleep_ms = 0;
    enter_sleep_locked = false;
    lpt_run();
    return enter_sleep_called;
}
void task_lock_func(lpt_t *cr) {
        LPT_BEGIN(cr);
        LPT_DELAY(cr, 5);
        LPT_END(cr);
    }
int main(void) {
    bool ok = true;

    // 1. 测试 CR_YIELD
    lpt_register(&task_yield, task_yield_func);
    run_one_cycle();
    if (yield_count != 1 || task_yield.status != LPT_STATE_READY) {
        printf("FAIL: CR_YIELD first pass count=%d status=%u\n", yield_count, task_yield.status);
        ok = false;
    }
    run_one_cycle();
    if (yield_count != 2 || task_yield.status != LPT_STATE_READY) {
        printf("FAIL: CR_YIELD second pass count=%d status=%u\n", yield_count, task_yield.status);
        ok = false;
    }
    run_one_cycle();
    if (yield_count != 3 || task_yield.status != LPT_STATE_STOP || !yield_task_done) {
        printf("FAIL: CR_YIELD final pass count=%d status=%u done=%d\n", yield_count, task_yield.status, yield_task_done);
        ok = false;
    }

    // 2. 测试 CR_DELAY
    if (!lpt_register(&task_delay, task_delay_func)) {
        printf("FAIL: lpt_register delay task\n");
        ok = false;
    }
    g_sys_tick = 0;
    run_one_cycle();
    if (task_delay.status != LPT_STATE_SLEEP) {
        printf("FAIL: CR_DELAY initial status=%u\n", task_delay.status);
        ok = false;
    }
    // 任务执行时自己设为SLEEP，但同一周期不会进入睡眠
    // 第二个周期才会真正进入睡眠（此时min_sleep被正确计算）
    run_one_cycle();
    if (!enter_sleep_called || enter_sleep_ms != 10) {
        printf("FAIL: CR_DELAY did not enter sleep as expected locked=%d ms=%u\n", enter_sleep_locked, enter_sleep_ms);
        ok = false;
    }
    g_sys_tick = 9;
    run_one_cycle();
    if (task_delay.status != LPT_STATE_SLEEP) {
        printf("FAIL: CR_DELAY should still sleep at tick 9 status=%u\n", task_delay.status);
        ok = false;
    }
    g_sys_tick = 10;
    run_one_cycle();
    if (task_delay.status != LPT_STATE_STOP || !delay_task_done) {
        printf("FAIL: CR_DELAY wakeup status=%u done=%d\n", task_delay.status, delay_task_done);
        ok = false;
    }

    // 3. 测试 CR_NOTIFY + CR_WAIT_NOTIFY_TIMEOUT
    if (!lpt_register(&task_wait, task_wait_func)) {
        printf("FAIL: lpt_register wait task\n");
        ok = false;
    }
    g_sys_tick = 0;
    run_one_cycle();
    if (task_wait.status != LPT_STATE_WAIT_NOTI) {
        printf("FAIL: CR_WAIT_NOTIFY_TIMEOUT initial status=%u\n", task_wait.status);
        ok = false;
    }
    g_sys_tick = 20;
    lpt_notify(&task_wait);
    run_one_cycle();
    if (task_wait.status != LPT_STATE_STOP || !wait_task_done || wait_task_timeout) {
        printf("FAIL: CR_WAIT_NOTIFY_TIMEOUT notify path status=%u done=%d timeout=%d\n", task_wait.status, wait_task_done, wait_task_timeout);
        ok = false;
    }

    // 4. 测试 CR_CALL 子协程调用
    if (!lpt_register(&task_parent, task_parent_func)) {
        printf("FAIL: lpt_register parent task\n");
        ok = false;
    }
    run_one_cycle();
    if (task_parent.status != LPT_STATE_READY || task_child.status != LPT_STATE_READY || child_steps != 1) {
        printf("FAIL: CR_CALL first child pass parent_status=%u child_status=%u child_steps=%d\n", task_parent.status, task_child.status, child_steps);
        ok = false;
    }
    run_one_cycle();
    if (!call_task_done || task_parent.status != LPT_STATE_STOP || parent_steps != 1 || child_steps != 2) {
        printf("FAIL: CR_CALL completion parent_status=%u child_steps=%d call_done=%d parent_steps=%d\n", task_parent.status, child_steps, call_task_done, parent_steps);
        ok = false;
    }

    // 5. 测试 lpt_sleep_lock / lpt_sleep_unlock 影响低功耗
    lpt_sleep_lock();
    static lpt_t task_lock = {0};
    
    if (!lpt_register(&task_lock, task_lock_func)) {
        printf("FAIL: lpt_register lock task\n");
        ok = false;
    }
    g_sys_tick = 0;
    run_one_cycle();
    // 任务执行时自己设为SLEEP（延时5ms），但同一周期不会进入睡眠
    // 第二个周期才会真正进入睡眠（此时min_sleep被正确计算，is_locked也会被检查）
    run_one_cycle();
    if (!enter_sleep_called || !enter_sleep_locked) {
        printf("FAIL: lpt_sleep_lock did not lock sleep locked=%d\n", enter_sleep_locked);
        ok = false;
    }
    lpt_sleep_unlock();

    printf("%s\n", ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return ok ? 0 : 1;
}
