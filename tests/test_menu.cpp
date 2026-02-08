#include "graph.h"
#include "test_trace.h"
#include "test_framework.h"
#include "button.h"
#include "test_interact.h"

extern screen* myscreen;

void test_mainmenu_buttons() {
    // Create a simple button array using the button struct constructor
    button test_buttons[3] = {
        button("begin", "BEGIN",   SDLK_b, 80, 60,  80, 20, 0, 0, MenuNav::None()),
        button("options", "OPTIONS", SDLK_o, 80, 90,  80, 20, 0, 0, MenuNav::None()),
        button("quit", "QUIT",    SDLK_q, 80, 120, 80, 20, 0, 0, MenuNav::None()),
    };

    trace_clear();
    vbutton* result = init_buttons(test_buttons, 3);
    TEST_ASSERT(result != NULL, "init_buttons should return non-NULL");
    TEST_ASSERT(trace_contains("menu", "init_buttons"), "init_buttons trace should be logged");
    TEST_ASSERT(trace_contains("menu", "count=3"), "button count should be in trace");

    // Verify IDs are propagated through init_buttons to allbuttons/vbuttons
    TEST_ASSERT(has_interactable("begin"), "BEGIN should be interactable");
    TEST_ASSERT(has_interactable("options"), "OPTIONS should be interactable");
    TEST_ASSERT(has_interactable("quit"), "QUIT should be interactable");

    // Clean up allocated vbuttons to avoid leaking into other tests
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (allbuttons[i]) { delete allbuttons[i]; allbuttons[i] = NULL; }
    }
}
REGISTER_TEST(test_mainmenu_buttons);
