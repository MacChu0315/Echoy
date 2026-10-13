#pragma once
#include <stdbool.h>
void producer_test_hold_suspend(bool hold);
void producer_test_wait_suspend_pending(void);
void producer_test_external_check_fail(unsigned nth);
