## bug 0:
### when:
06/02/2026, introduced when developing window mode switching
### description:
assert pop when switching on MSAA from borderless mode after switching from a smaller windowed mode
### reproduce:
start windowed mode (MSAA off or on)-> switch borderless -> turn on MSAA ->assert pop (on command list close)