## bug 1: [FIXED]
### when:
06/02/2026, introduced when developing window mode switching and integrating swap chain recreate
### description:
static hang (no crash or assert) when windowe mode. the visual process completes as going from a window to borderless fullscreen and filling the screen but the program becomes unresponsive. the triangle stops moving and the imgui becomes non-responsive, and alt+f4 is unresponsive as well. This seems to be related to timing because if you wait 5+ seconds in between switches it seems to be fine and continue as always but if you take under 5 seconds between switches or seconds since program running, this issue doesn't happen.
### reproduce:
switch to window mode.
### root cause:
The original code tries to signal fence values that might already be completed. If GetCompletedValue() returns a value equal to or greater than fenceValue, the SetEventOnCompletion() call returns immediately (since the fence is already at that value), but the event might not be in a signaled state. This can cause WaitForSingleObjectEx() to wait forever.

The fix ensures we always signal a new fence value that the GPU hasn't reached yet, then wait for it properly.
### fix:
Change WaitForAllFrames() to wait for all pending GPU work without signaling