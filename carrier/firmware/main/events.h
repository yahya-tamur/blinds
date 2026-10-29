// Commands from every input (switch, remote, web page, alarms) go through one queue to the control task.
#pragma once

typedef enum { CMD_UP = 1, CMD_DOWN, CMD_CANCEL } command_t;
typedef enum { SRC_SWITCH, SRC_REMOTE, SRC_WEB, SRC_ALARM } source_t;

typedef struct {
    command_t cmd;
    source_t source;
} event_t;

// Safe to call from any task (not from an interrupt). Drops the event if the queue is full.
void event_post(command_t cmd, source_t source);
