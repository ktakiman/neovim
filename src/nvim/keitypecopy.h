#ifndef NVIM_KEITYPECOPY_H
#define NVIM_KEITYPECOPY_H

#include <stdbool.h>

#include "nvim/ex_cmds_defs.h"
#include "nvim/normal.h"
#include "nvim/state.h"
#include "nvim/types.h"

//------------------------------------------------------------------------------
// buffer
//------------------------------------------------------------------------------
typedef struct data_block {
  uint16_t db_id;               /* ID for data block: DATA_ID */
  unsigned db_free;             /* free space available */
  unsigned db_txt_start;        /* byte where text starts */
  unsigned db_txt_end;          /* byte just after data block */
  linenr_T db_line_count;       /* number of lines in this block */
  unsigned db_index[1];         /* index for start of line (actually bigger)
                                 * followed by empty space upto db_txt_start
                                 * followed by the text in the lines until
                                 * end of page */
} data_block_T;

#define B0_UNAME_SIZE           40
#define B0_HNAME_SIZE           40
#define B0_FNAME_SIZE_ORG       900     // what it was in older versions

typedef struct block0 {
  char_u b0_id[2];              ///< ID for block 0: BLOCK0_ID0 and BLOCK0_ID1.
  char_u b0_version[10];        // Vim version string
  char_u b0_page_size[4];       // number of bytes per page
  char_u b0_mtime[4];           // last modification time of file
  char_u b0_ino[4];             // inode of b0_fname
  char_u b0_pid[4];             // process id of creator (or 0)
  char_u b0_uname[B0_UNAME_SIZE];        // name of user (uid if no name)
  char_u b0_hname[B0_HNAME_SIZE];        // host name (if it has a name)
  char_u b0_fname[B0_FNAME_SIZE_ORG];        // name of file being edited
  long b0_magic_long;           // check for byte order of long
  int b0_magic_int;             // check for byte order of int
  short b0_magic_short;         // check for byte order of short
  char_u b0_magic_char;         // check for last char
} block0_T;

typedef struct pointer_entry {
  blocknr_T pe_bnum;            // block number
  linenr_T pe_line_count;       // number of lines in this branch
  linenr_T pe_old_lnum;         // lnum for this block (for recovery)
  int pe_page_count;            // number of pages in block pe_bnum
} pointer_entry_T;

typedef struct pointer_block {
  uint16_t pb_id;               // ID for pointer block: PTR_ID
  uint16_t pb_count;            // number of pointers in this block
  uint16_t pb_count_max;        // maximum value for pb_count
  pointer_entry_T pb_pointer[1];         // list of pointers to blocks (actually longer)
                                // followed by empty space until end of page
} pointer_block_T;

//------------------------------------------------------------------------------
// normal mode state
//------------------------------------------------------------------------------
typedef struct normal_state {
  VimState state;
  bool command_finished;
  bool ctrl_w;
  bool need_flushbuf;
  bool set_prevcount;
  bool previous_got_int;             // `got_int` was true
  bool cmdwin;                       // command-line window normal mode
  bool noexmode;                     // true if the normal mode was pushed from
                                     // ex mode(:global or :visual for example)
  bool toplevel;                     // top-level normal mode
  oparg_T oa;                        // operator arguments
  cmdarg_T ca;                       // command arguments
  int mapped_len;
  int old_mapped_len;
  int idx;
  int c;
  int old_col;
  pos_T old_pos;
} NormalState;

//------------------------------------------------------------------------------
// insert mode state
//------------------------------------------------------------------------------
typedef struct insert_state {
  VimState state;
  cmdarg_T *ca;
  int mincol;
  int cmdchar;
  int startln;
  long count;
  int c;
  int lastc;
  int i;
  bool did_backspace;                // previous char was backspace
  bool line_is_white;                // line is empty before insert
  linenr_T old_topline;              // topline before insertion
  int old_topfill;
  int inserted_space;                // just inserted a space
  int replaceState;
  int did_restart_edit;              // remember if insert mode was restarted
                                     // after a ctrl+o
  bool nomove;
  char_u *ptr;
} InsertState;

//------------------------------------------------------------------------------
// commandline state
//------------------------------------------------------------------------------
typedef struct {
  colnr_T   vs_curswant;
  colnr_T   vs_leftcol;
  linenr_T  vs_topline;
  int       vs_topfill;
  linenr_T  vs_botline;
  int       vs_empty_rows;
} viewstate_T;

typedef struct {
  pos_T       search_start;   // where 'incsearch' starts searching
  pos_T       save_cursor;
  viewstate_T init_viewstate;
  viewstate_T old_viewstate;
  pos_T       match_start;
  pos_T       match_end;
  bool        did_incsearch;
  bool        incsearch_postponed;
  int         magic_save;
} incsearch_state_T;

typedef struct command_line_state {
  VimState state;
  int firstc;
  long count;
  int indent;
  int c;
  int gotesc;                           // TRUE when <ESC> just typed
  int do_abbr;                          // when TRUE check for abbr.
  char_u *lookfor;                      // string to match
  int hiscnt;                           // current history line in use
  int save_hiscnt;                      // history line before attempting
                                        // to jump to next match
  int histype;                          // history type to be used
  incsearch_state_T is_state;
  int did_wild_list;                    // did wild_list() recently
  int wim_index;                        // index in wim_flags[]
  int res;
  int       save_msg_scroll;
  int       save_State;                 // remember State when called
  char_u   *save_p_icm;
  int some_key_typed;                   // one of the keys was typed
  // mouse drag and release events are ignored, unless they are
  // preceded with a mouse down event
  int ignore_drag_release;
  int break_ctrl_c;
  expand_T xpc;
  long *b_im_ptr;
} CommandLineState;

#endif // NVIM_KEITYPECOPY_H
