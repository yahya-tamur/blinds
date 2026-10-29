// The 3-position switch and the IR remote. Both post commands with event_post().
#pragma once

void switch_init(void);
void switch_poll(void);   // call every ~10 ms from the control task

void remote_start(void);  // starts its own task, which waits on the IR receiver
