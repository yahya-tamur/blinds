// Pick the application or ONE test, then: idf.py build flash
#include "app.h"
#include "tests/tests.h"

#define RUN_TEST test_bringup        // the blinds application; or a test: test_bringup, or a local one (local/tests/local_tests.h)

void app_main(void)
{
    RUN_TEST();
}
