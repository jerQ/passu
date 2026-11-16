"""Exercise the actual app_main startup flow with host C platform stubs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define ESP_ERROR_CHECK(expr) assert((expr) == 0)
#define CONFIG_DW_BUTTON_GPIO 0
#define CONFIG_DW_GROUPS 8
static jmp_buf stopped;
static int failure, tasks, button, ready, device_error;
static void *s_triggers;
static struct { struct { const char *fail_reason; } health; } s_checked;
static int s_cond;
static void report_previous_crash(void) {}
static void wordlist_load_or_halt(void) {}
static int entropy_radio_start(void) { return 0; }
static int entropy_radio_rng(void) { return 0; }
static void checked_rng_init(void *p, int rng) { (void)p; (void)rng; }
static bool checked_rng_startup(void *p) { (void)p; return true; }
static int checked_rng_as_rng(void *p) { (void)p; return 0; }
static void sha256_psa(void) {}
static void conditioned_rng_init(void *p, int rng, void (*hash)(void)) {
    (void)p; (void)rng; (void)hash;
}
static void *xQueueCreate(unsigned n, unsigned size) {
    (void)n; (void)size; return failure == 1 ? NULL : &s_cond;
}
typedef int trigger_t;
static void generator_task(void *p) { (void)p; }
#if CONFIG_DW_SERIAL_TRIGGER
static void serial_task(void *p) { (void)p; }
#endif
static int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, void *handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    tasks++;
    return failure == tasks + 1 ? 0 : pdPASS;
}
static void button_init(void) {
    assert(tasks == (CONFIG_DW_SERIAL_TRIGGER ? 2 : 1)); button++;
}
static int test_printf(const char *fmt, ...) {
    if (strstr(fmt, "Press the button")) ready++;
    if (strstr(fmt, "DEVICE-ERROR:")) device_error++;
    return 0;
}
#define printf test_printf
#define ESP_LOGE(...) ((void)0)
static void vTaskDelay(unsigned ticks) { (void)ticks; longjmp(stopped, 1); }
'''

CHECKS = r'''
int main(void) {
    for (failure = 0; failure <= (CONFIG_DW_SERIAL_TRIGGER ? 3 : 2); failure++) {
        tasks = button = ready = device_error = 0;
        int halted = setjmp(stopped);
        if (!halted) app_main();
        if (failure) {
            assert(halted && device_error == 1 && ready == 0 && button == 0);
            assert(tasks == failure - 1);
        } else {
            assert(!halted && ready == 1 && button == 1 && device_error == 0);
        }
    }
    return 0;
}
'''


class StartupTests(unittest.TestCase):
    def test_allocation_failures_and_success(self):
        source = (Path(__file__).parents[1] / "main/main.c").read_text()
        startup = source[source.index("static void device_halt("):]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "startup.c").write_text(STUBS + startup + CHECKS)
            for serial in (0, 1):
                with self.subTest(serial=serial):
                    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall",
                                    "-Wextra", "-Werror", f"-DCONFIG_DW_SERIAL_TRIGGER={serial}",
                                    str(path / "startup.c"), "-o", str(path / "startup")], check=True)
                    subprocess.run([str(path / "startup")], check=True)


if __name__ == "__main__":
    unittest.main()
