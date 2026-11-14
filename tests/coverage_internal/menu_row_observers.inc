// The production gate pass, for tests that drive a spec's draw hooks without
// the loop around them: the Disabled dim (live->color = GREY) and the inert
// myfun/myfunc are what this writes, and a test that painted them by hand
// would prove nothing about the engine.
void picker_testing_apply_row_states(const MenuScreenSpec& spec,
                                     button* buttons, int num_buttons)
{
    const SpecRowView spec_rows = materialized_spec_rows(spec);
    const MenuLabelContext context = build_label_context();
    RowState states[MAX_BUTTONS] = {};
    apply_row_states(spec_rows, buttons,
                     std::min(num_buttons, static_cast<int>(MAX_BUTTONS)),
                     context, states);
}
