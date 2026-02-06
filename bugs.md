## bug 1:
### when:
06/02/2026, introduced when developing window mode switching and integrating swap chain recreate
### description:
static hang (no crash or assert) when switching to borderless from windowed mode. the visual process completes as going from a window to borderless fullscreen and filling the screen but the program becomes unresponsive. the triangle stops moving and the imgui becomes non-responsive, and alt+f4 is unresponsive as well.
### reproduce:
switch to borderless from windowed mode.