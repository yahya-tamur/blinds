// The momentary switch and the IR remote. Both post commands with event_post().
#pragma once
#include <stdbool.h>
#include <stdint.h>

void switch_init(void);
void switch_poll(void);   // call every ~10 ms from the control task

// The remotes and their buttons are set up on the web page and saved (remote.c). A remote is its NEC address
// and a name; a button is a remote's address, a command code, and what the button does.
#define MAX_REMOTES        8
#define REMOTE_NAME_LENGTH 24  // with the terminating zero
#define MAX_REMOTE_BUTTONS 24
typedef struct {
    uint8_t addr;
    char name[REMOTE_NAME_LENGTH];
} remote_t;
typedef struct {
    uint8_t addr, cmd;
    int8_t action;        // 1 up, -1 down, 0 stop
} remote_button_t;

typedef struct {
    uint32_t id;          // the remote_learn() request it answers; 0 = none yet
    uint8_t addr, cmd;
} remote_capture_t;

void remote_start(void);  // loads the saved buttons and starts its own task, which waits on the IR receiver
int remote_remotes(remote_t *out);                       // out has MAX_REMOTES places; returns how many
int remote_list(remote_button_t *out);                    // out has MAX_REMOTE_BUTTONS places; returns how many
// A new remote named "Remote N" (the lowest N not in use); true if it is there afterwards (new or already set up)
bool remote_new(uint8_t addr);
bool remote_rename(uint8_t addr, const char *name);      // false if there is no such remote or the name is empty
void remote_delete(uint8_t addr);                        // the remote and all its buttons
// Sets up a button, adding its remote if new; replaces the button's action if already set up; false if full
bool remote_add(uint8_t addr, uint8_t cmd, int action);
void remote_remove(uint8_t addr, uint8_t cmd);
// Learn mode: the next press of any remote button is captured (and not acted on) instead. Lasts 15 s; asking
// again while it waits renews it under the same id. Returns the id the capture will carry. off ends it.
uint32_t remote_learn(bool on);
bool remote_learning(remote_capture_t *last);             // whether it is waiting; last = the latest capture
