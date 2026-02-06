## bug 1:
### when:
06/02/2026, introduced when developing window mode switching and integrating swap chain recreate
### description:
static hang (no crash or assert) when switching to borderless from windowed mode
### reproduce:
switch to borderless from windowed mode.