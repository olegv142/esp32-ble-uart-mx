#include "watchdog.h"
#include "debug.h"
#include "mx_config.h"
#include <esp_task_wdt.h>

void watchdog_init(void)
{
  esp_task_wdt_config_t wdt_cfg = {.timeout_ms = WDT_TIMEOUT, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg); // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);             // add current thread to WDT watch
}

void watchdog_reset(void)
{
  esp_task_wdt_reset();
}

void esp_task_wdt_isr_user_handler(void)
{
  reset_self();
}
