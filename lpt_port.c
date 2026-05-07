#include "lpt.h"

// 假设这是你包含的硬件库，例如：#include "stm32g0xx_hal.h"

/*===========================================================================*
 * 系统全局滴答及临界区保护
 *===========================================================================*/
volatile uint32_t g_sys_tick = 0; // 必须放在中断里自增 (如 SysTick_Handler)

// 调度引擎获取系统节拍
uint32_t sys_tick_get(void) {
    return g_sys_tick;
}

// 进入临界区 (关闭全局中断)
void lpt_port_critical_enter(void) {
    __disable_irq(); // CMSIS 标准指令
}

// 退出临界区 (开启全局中断)
void lpt_port_critical_exit(void) {
    __enable_irq();
}

/*===========================================================================*
 * 核心低功耗休眠与补偿
 * 说明：此函数由调度器自动调用，开发者需根据具体的 MCU 填入对应的睡眠代码
 *===========================================================================*/
void lpt_port_enter_sleep(uint32_t sleep_ms, bool is_locked) {
    
    // 【情况 A】有外设上了休眠锁，或者睡眠时间极短
    if (is_locked || sleep_ms < 5) {
        // 只暂停 CPU，外设和 SysTick 照常工作 (IDLE / SLEEP 模式)
        __WFI(); 
        return;
    }

    // 【情况 B】安全无锁，且时间足够长，进入深度休眠 (STOP / DEEP SLEEP 模式)
    // ----------------------------------------------------------------------
    // 伪代码流程，请替换为你自己 MCU 的低功耗/定时器 API
    
    // 1. 关掉系统滴答，防止被 1ms 唤醒
    // SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; 

    // 2. 配置并启动低功耗定时器 (如 LPTIM / RTC)，定时 sleep_ms 毫秒
    // LPTIM_SetTimeout(sleep_ms);
    // LPTIM_Start();

    // 3. 进入深度睡眠模式，等待硬件被唤醒
    // HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    // --- MCU 在此停机，功耗极低，被定时器或外部中断唤醒后继续往下走 ---

    // 4. 醒来后，计算真实睡了多久
    // LPTIM_Stop();
    // uint32_t actual_sleep_ms = LPTIM_GetElapsed(); 

    // 5. 将这缺失的时间补偿给全局系统节拍
    // lpt_port_critical_enter();
    // g_sys_tick += actual_sleep_ms;
    // lpt_port_critical_exit();

    // 6. 恢复常规系统滴答
    // SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

