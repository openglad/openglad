namespace
{
// One-shot pass budget for the compiled-out wait loop: view_team consumes it
// to drive the poll callback exactly like real wait passes would.
int s_view_team_testing_poll_passes = 0;
} // namespace

void view_team_testing_set_poll_passes(int passes)
{
	s_view_team_testing_poll_passes = passes;
}
