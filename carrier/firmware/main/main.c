// Pick the application or ONE test, then: idf.py build flash
#include "app.h"
#include "tests.h"

#define RUN_TEST app_start   // the blinds application; or a test: test_switches, test_ir, test_rtc, test_motor, test_stallguard, test_spreadcycle

void app_main(void)
{
    RUN_TEST();
}
