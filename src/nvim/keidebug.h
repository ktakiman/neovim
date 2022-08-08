#ifndef NVIM_KEIDEBUG_H
#define NVIM_KEIDEBUG_H

#include <uv.h>

#include "nvim/state.h"
#include "nvim/keycodes.h"
#include "nvim/buffer_defs.h"


void KeiDump(void);

void KeiDumpBuf(buf_T* buf);
void KeiDumpWin(win_T* win);

// vim state dump
void KeiSetNormalCheck(state_check_callback callback);
void KeiSetInsertCheck(state_check_callback callback);
void KeiSetCmndLineCheck(state_check_callback callback);

void KeiDumpVimState(VimState* state, int key);

void KeiDumpTTYData(uv_buf_t* bufs, unsigned buf_ct);

bool KeiSteal(int key);

#endif  // NVIM_KEIDEBUG_H
