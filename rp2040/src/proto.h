// Line-based command protocol of the cpu8 loader (see ../README.md).
// Plain C; compiled into the RP2040 firmware and into the PC mock.
#pragma once

void proto_input(int c);  // feed one received character
void proto_poll(void);    // call often: sends monitor events (EV lines)
