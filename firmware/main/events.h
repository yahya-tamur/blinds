// Commands from every input (switch, remote, web page, alarms) go through one queue to the control task.
#pragma once

typedef enum { CMD_UP = 1, CMD_DOWN, CMD_CANCEL,
               CMD_JOG_UP, CMD_JOG_DOWN,          // arg = Hall counts (30 degrees of the sprocket each)
               CMD_SET_STOP, CMD_CLEAR_STOP,      // arg = STOP_TOP or STOP_BOTTOM (travel.h)
               CMD_DETECT_ENDS, CMD_HOME } command_t;   // find both end stops / re-home on the bottom one
typedef enum { SRC_SWITCH, SRC_REMOTE, SRC_WEB, SRC_ALARM } source_t;

typedef struct {
    command_t cmd;
    source_t source;
    int arg;
} event_t;

// Safe to call from any task (not from an interrupt). Drops the event if the queue is full.
void event_post(command_t cmd, source_t source);
void event_post_arg(command_t cmd, source_t source, int arg);
