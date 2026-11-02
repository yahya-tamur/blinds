// Pick the application or ONE test, then: idf.py build flash
#include "app.h"
#include "tests/tests.h"

#define RUN_TEST app_start    // the blinds application; or a test: test_bringup, test_stallguard, test_spreadcycle, test_hall_motor

void app_main(void)
{
    RUN_TEST();
}
