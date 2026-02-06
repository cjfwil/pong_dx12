## bug 1:
### when:
06/02/2026, introduced when developing window mode switching and integrating swap chain recreate
### description:
static hang (no crash or assert) when windowe mode. the visual process completes as going from a window to borderless fullscreen and filling the screen but the program becomes unresponsive. the triangle stops moving and the imgui becomes non-responsive, and alt+f4 is unresponsive as well. This seems to be related to timing because if you wait 5+ seconds in between switches it seems to be fine and continue as always but if you take under 5 seconds between switches or seconds since program running, this issue doesn't happen.
### reproduce:
switch to window mode.