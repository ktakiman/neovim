#include "nvim/keidebug.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include <nvim/globals.h>
#include <nvim/normal.h>
#include <nvim/types.h>
#include <nvim/ex_cmds_defs.h>

#include <nvim/keitypecopy.h>

#define DUMP_FILE "keidump.txt"
#define BUF_DUMP_FILE "keibufdump.txt"
#define STATE_DUMP_FILE "keistatedump.txt"


// - not sure which strategy works best
//    1. append each dump ~ this is better if need to track history
//    2. overwrite at each dump ~ this is better to check the current state quickly
//    3. why don't we create both files???? am i smart? :)
//

static void DumpToMainLogOnly(char* file_no_ext, char* format, ...) {
  char buf[256];

  // regular log file, overwrites the previous log
  sprintf(buf, "keilog/%s.log", file_no_ext);
  FILE* fp = fopen(buf, "w");
  fprintf(fp, "what's going on");
  va_list args;
  va_start(args, format);
  vfprintf(fp, format, args);
  va_end(args);
  fclose(fp);
}

static void DumpToFile(char* file_no_ext, int* dump_ct, char* format, ...) {
  char buf[256];
  FILE* fp = 0;

  // ** calling another method above does not work. why ??? can I fixt it ??? **
  // regular log file, overwrites the previous log
  // va_list args;
  // va_start(args, format);
  // DumpToMainLogOnly(file_no_ext, format, args);
  // va_end(args);
  
  sprintf(buf, "keilog/%s.log", file_no_ext);
  fp = fopen(buf, "w");
  va_list args;
  va_start(args, format);
  vfprintf(fp, format, args);
  va_end(args);
  fclose(fp);

  // history log file, appends a new log
  sprintf(buf, "keilog/%s_hist.log", file_no_ext);
  if (*dump_ct == 0) {
    fp = fopen(buf, "w");
  } else {
    fp = fopen(buf, "a");
    fprintf(fp, "\n");
  }

  fprintf(fp, "# %i\n", *dump_ct);

  va_start(args, format);
  vfprintf(fp, format, args);
  va_end(args);
  fclose(fp);

  ++(*dump_ct);
}

static void DumpBytes(const char* file_no_ext, int file_index, void* bytes, unsigned int len, unsigned int width,
    unsigned int skip_from, unsigned int skip_to) {
  char buf[PATH_MAX];
  getcwd(buf, PATH_MAX);
  sprintf(buf, "keilog/%s_%i.txt", file_no_ext, file_index);

  FILE* fp = fopen(buf, "w");

  char* p = (char*)bytes;
  int buf_pos = 0;
  for (unsigned int i = 0; i < len; ++i) {
    if (skip_from <= i && i < skip_to) {
      if (i == skip_from) {
        fprintf(fp, "...\n");
      }
      continue;
    }

    if (i % width == 0) {
      fprintf(fp, "0x%08llx: ", (unsigned long long)(p + i));
    } else if (i % 8 == 0) {
      fprintf(fp, " ");
      buf_pos += sprintf(buf + buf_pos, " ");
    }

    fprintf(fp, "%02x ", (unsigned char)p[i]);

    if (32 <= p[i] && p[i] <= 127) {
      buf_pos += sprintf(buf + buf_pos, "%c", p[i]);
    } else {
      buf_pos += sprintf(buf + buf_pos, ".");
    }

    if ((i + 1) % width == 0) {
      fprintf(fp, " %s\n", buf);
      buf_pos = 0;
    }
  }

  fclose(fp);
}

static void DumpRepeatedChar(char* tmp, int* pos, char ch, int repeat, bool endl) {
  // any better way than this ????
  for (int i = 0; i < repeat; ++i) {
    *pos += sprintf(tmp + *pos, "%c", ch);
  }

  if (endl) {
    *pos += sprintf(tmp + *pos, "\n");
  }
}

static void DumpFlags(char* tmp, int* pos, int value, va_list args) {
  char* flag_name = 0;

  *pos += sprintf(tmp + *pos, "%i", value);

  if (value > 0) {
    *pos += sprintf(tmp + *pos, " = ");
  }
    

  while (value) {
    flag_name = va_arg(args, char*);
    if (value & 1) {
      *pos += sprintf(tmp + *pos, "%s ", flag_name);
    }
    value = value >> 1;
  }

  // assuming I want edl always
  *pos += sprintf(tmp + *pos, "\n");
}

// Dump a line

static void DLImp(char* tmp, int* pos, int indent, int header_len, const char* header, const char* format, va_list args) {
  *pos += sprintf(tmp + *pos, "%*s%-*s", indent, "", header_len, header);
  *pos += vsprintf(tmp + *pos, format, args);
  *pos += sprintf(tmp + *pos, "\n");
}

static void DL(char* tmp, int* pos, int indent, int header_len, const char* header, const char* format, ...) {
  va_list args;
  va_start(args, format);
  DLImp(tmp, pos, indent, header_len, header, format, args);
  va_end(args);
}

// Dump a line with extra 'delta' arg
static void DLD(char* tmp, int* pos, int indent, int header_len, int delta, const char* header, const char* format, ...) {
  va_list args;
  va_start(args, format);
  DLImp(tmp, pos, indent + delta, header_len - delta, header, format, args);
  va_end(args);
}

// Dump header only
static void DH(char* tmp, int* pos, int indent, int header_len, const char* header, bool endl) {
  *pos += sprintf(tmp + *pos, "%*s%-*s", indent, "", header_len, header);
  if (endl) {
    *pos += sprintf(tmp + *pos, "\n");
  }
}

// Dump a line with flag
static void DF(char* tmp, int* pos, int indent, int header_len, const char* header, int value, ...) {
  DH(tmp, pos, indent, header_len, header, false);

  va_list args;
  va_start(args, value);
  DumpFlags(tmp, pos, value, args);
  va_end(args);
}

// Dump horizontal line
static void DHR(char* tmp, int* pos, int indent, int len) {
  *pos += sprintf(tmp + *pos, "%*s", indent, "");
  while (len-- > 0) {
    *pos += sprintf(tmp + *pos, "-");
  }

  *pos += sprintf(tmp + *pos, "\n");
}

// ----------------------------------------------------------------------------
// buffer dump
// ----------------------------------------------------------------------------
static void KeiDumpHashItem(mf_hashitem_T* hi, char* tmp, int* pos, int idt) {
  int hw = 16;
  DL(tmp, pos, idt, hw, "mhi_next:", "%p", hi->mhi_next);
  DL(tmp, pos, idt, hw, "mhi_prev:", "%p", hi->mhi_prev);
  DL(tmp, pos, idt, hw, "mhi_key:",  "%li", hi->mhi_key);
}

static void KeiDumpDataBlock(void* data, char* tmp, int* pos, int idt) {

  int hw = 16;

  data_block_T* db = data;
  DL(tmp, pos, idt, hw, "db_id:", "%u", db->db_id);
  DL(tmp, pos, idt, hw, "db_free:", "%u", db->db_free);
  DL(tmp, pos, idt, hw, "db_txt_start:", "%u", db->db_txt_start);
  DL(tmp, pos, idt, hw, "db_txt_end:", "%u", db->db_txt_end);
  DL(tmp, pos, idt, hw, "db_line_count:", "%li", db->db_line_count);
  DH(tmp, pos, idt, hw, "db_index:", false);

  for (int i = 0; i < db->db_line_count && i < 5; ++i) {
    *pos += sprintf(tmp + *pos, " %u", db->db_index[i]);
  }

  *pos += sprintf(tmp + *pos, "\n");
}

// 'bh_index' is something I made up so I can make multiple dump files for bh data
static void KeiDumpBlockHeader(bhdr_T* bh, char* tmp, int* pos, int bh_index, int idt) {
  KeiDumpHashItem((mf_hashitem_T*)bh, tmp, pos, idt);
  int hw = 18;
  DL(tmp, pos, idt, hw, "bh_next:", "%p", bh->bh_next);
  DL(tmp, pos, idt, hw, "bh_prev:", "%p", bh->bh_prev);
  DL(tmp, pos, idt, hw, "bh_page_count:", "%u", bh->bh_page_count);
  DL(tmp, pos, idt, hw, "bh_data:", "%p", bh->bh_data);

  KeiDumpDataBlock(bh->bh_data, tmp, pos, idt + 2);

  DumpBytes("page_data", bh_index, bh->bh_data, 0x1000 * bh->bh_page_count, 8, 0x80, 0xf80);
}

static void KeiDumpMemFile(memfile_T* memfile, char* tmp, int* pos, int idt) {
  int hw = 18;
  DL(tmp, pos, idt, hw, "mf_page size:", "%u", memfile->mf_page_size);
  DL(tmp, pos, idt, hw, "mf_free_first:", "%p", memfile->mf_free_first);

  DL(tmp, pos, idt, hw, "mf_used_first:", "%p", memfile->mf_used_first);
  KeiDumpBlockHeader(memfile->mf_used_first, tmp, pos, 0, idt + 2);

  DL(tmp, pos, idt, hw, "mf_used_last:", "%p", memfile->mf_used_last);

  /*
  bhdr_T* bh = memfile->mf_used_first;

  int bh_index = 0;
  while (bh) {
    *pos += sprintf(tmp + *pos, "    bh:          0x%08llx\n", (unsigned long long)bh);
    KeiDumpBlockHeader(bh, tmp, pos, bh_index);
    bh = bh->bh_next;
    ++bh_index;
  }
  */


  /*
  *pos += sprintf(tmp + *pos, "  used_last:   0x%08llx\n", (unsigned long long)memfile->mf_used_last);
  KeiDumpBlockHeader(memfile->mf_used_last, tmp, pos);
  *pos += sprintf(tmp + *pos, "\n");
  */
}

/* #define ML_EMPTY        1       // empty buffer */
/* #define ML_LINE_DIRTY   2       // cached line was changed and allocated */
/* #define ML_LOCKED_DIRTY 4       // ml_locked was changed */
/* #define ML_LOCKED_POS   8       // ml_locked needs positive block number */

static void KeiDumpMemLine(memline_T* memline, char* tmp, int* pos, int idt) {
  // unsigned long long p_line_ptr = (unsigned long long)memline->ml_line_ptr;
  int hw = 20;
  DL(tmp, pos, idt, hw, "ml_line_ct:", "%i", memline->ml_line_count);
  DL(tmp, pos, idt, hw, "ml_line_lnum:", "%i", memline->ml_line_lnum);
  DL(tmp, pos, idt, hw, "ml_line_offset:", "%li", memline->ml_line_offset);
  // DF(tmp, pos, idt, hw, "ml_flags:", memline->ml_flags, "ML_EMPTY", "ML_LINE_DIRTY", "ML_LOCKED_DIRTY", "ML_LOCKED_POS");
  DL(tmp, pos, idt, hw, "ml_stack_top:", "%i", memline->ml_stack_top);
  DL(tmp, pos, idt, hw, "ml_stack_size:", "%i", memline->ml_stack_size);
  DL(tmp, pos, idt, hw, "ml_line_ptr:", "%p", memline->ml_line_ptr);

  DL(tmp, pos, idt, hw, "ml_mfp:", "%p", memline->ml_mfp);
  KeiDumpMemFile(memline->ml_mfp, tmp, pos, idt + 2);
  
  // DL(tmp, pos, idt, hw, "ml_locked:", "%p", memline->ml_locked);
  // DL(tmp, pos, idt, hw, "ml_locked_low:", "%li", memline->ml_locked_low);
  // DL(tmp, pos, idt, hw, "ml_locked_high:", "%li", memline->ml_locked_high);
  // DL(tmp, pos, idt, hw, "ml_locked_lineadd:", "%i", memline->ml_locked_lineadd);

  DHR(tmp, pos, idt, hw);
  DH(tmp, pos, idt, 6, "line:", false);
  *pos += sprintf(tmp + *pos, "%-.40s", memline->ml_line_ptr);
}

static int KeiDumpBufCore(buf_T* buf, char* tmp) {
  int pos = 0;
  int idt = 2;   // indent
  int hw = 20;   // header width

  DL(tmp, &pos, 0, idt + hw, "BUFFER:", "%p", buf);
  DL(tmp, &pos, idt, hw, "handle:", "%i", buf->handle);
  DHR(tmp, &pos, idt, hw);
  /* pos = sprintf(tmp + pos, "  handle:            %i\n", buf->handle); */
  /* pos += sprintf(tmp + pos, "  --------------------\n"); */

  KeiDumpMemLine(&buf->b_ml, tmp, &pos, idt);

  /* pos += sprintf(tmp + pos, "  ml_stack_top:         %i\n", buf->b_ml.ml_stack_top); */
  /* pos += sprintf(tmp + pos, "  ml_stack_size:        %i\n", buf->b_ml.ml_stack_size); */
  /* pos += sprintf(tmp + pos, "  ml_stack:ip_bnum:     %li\n", buf->b_ml.ml_stack->ip_bnum); */
  /* pos += sprintf(tmp + pos, "  ml_stack:ip_low:      %li\n", buf->b_ml.ml_stack->ip_low); */
  /* pos += sprintf(tmp + pos, "  ml_stack:ip_high:     %li\n", buf->b_ml.ml_stack->ip_high); */
  /* pos += sprintf(tmp + pos, "  ml_stack:ip_index:    %i\n", buf->b_ml.ml_stack->ip_index); */

  return pos;
}

#define LOCAL_BUF_SIZE 0x4000

static int dump_buf_ct = 0;

void KeiDumpBuf(buf_T* buf) {

  KeiDump();
  return;

  char tmp[LOCAL_BUF_SIZE];

  int pos = KeiDumpBufCore(buf, tmp);

  sprintf(tmp + pos, "\n(about %i / %i bytes left in local buffer)", LOCAL_BUF_SIZE - (pos + 50), LOCAL_BUF_SIZE);
  DumpToFile("buffer_dump", &dump_buf_ct, "%s", tmp);
}

static void DumpBuf(buf_T* buf, char* tmp, int* pos) {
  int hw = 20;
  DL(tmp, pos, 0, hw, "buf", "%p", buf);
  DHR(tmp, pos, 0, hw);

  memline_T* ml = &buf->b_ml;
  DH(tmp, pos, 0, hw, "memline:", true);
  DL(tmp, pos, 2, hw, "ml_line_count:", "%li", ml->ml_line_count);
  DL(tmp, pos, 2, hw, "ml_line_lnum:", "%li", ml->ml_line_lnum);
  DL(tmp, pos, 2, hw, "ml_line_ptr:", "%.20s", ml->ml_line_ptr);
  DL(tmp, pos, 2, hw, "ml_locked:", "%p", ml->ml_locked);
  DL(tmp, pos, 2, hw, "ml_locked_low:", "%li", ml->ml_locked_low);
  DL(tmp, pos, 2, hw, "ml_locked_high:", "%li", ml->ml_locked_high);
  DL(tmp, pos, 2, hw, "ml_locked_lineadd:", "%i", ml->ml_locked_lineadd);
  DL(tmp, pos, 2, hw, "ml_stack:", "top=%i, size=%i", ml->ml_stack_top, ml->ml_stack_size);
  for (int i = 0; i < ml->ml_stack_top; ++i) {
    infoptr_T* info = ml->ml_stack + i;
    DL(tmp, pos, 4, hw, "ip_bnum:", "%i", info->ip_bnum);     // block number of a pointer block
    DL(tmp, pos, 4, hw, "ip_index:", "%i", info->ip_index);   // index of pointer block entry for the current line
    DL(tmp, pos, 4, hw, "ip_low:", "%i", info->ip_low);       // low line number of a pointer block
    DL(tmp, pos, 4, hw, "ip_high:", "%i", info->ip_high);     // high line number of a pointer block
  }
  DL(tmp, pos, 2, hw, "ml_chunksize:", "num=%i, used=%i", ml->ml_numchunks, ml->ml_usedchunks);
  for (int i = 0; i < ml->ml_usedchunks && i < 3; ++i) {      // limit number of chunk data
    chunksize_T* cs = ml->ml_chunksize + i;
    DL(tmp, pos, 4, hw, "mlcs_numlines", "%i", cs->mlcs_numlines);     // block number of a pointer block
    DL(tmp, pos, 4, hw, "mlcs_totalsize", "%i", cs->mlcs_totalsize);     // block number of a pointer block
  }
  DHR(tmp, pos, 0, hw);

  // shoud those be in another function?
  memfile_T* mf = ml->ml_mfp;
  DL(tmp, pos, 0, hw, "memfile", "%p", mf);
  DL(tmp, pos, 2, hw, "mf_fname", "%.20s", mf->mf_fname);    // swapfile?
  DL(tmp, pos, 2, hw, "mf_ffname", "%.20s", mf->mf_ffname);  // swapfile?
  DL(tmp, pos, 2, hw, "mf_infile_count", "%u", mf->mf_infile_count);
  DL(tmp, pos, 2, hw, "mf_page_size", "%i", mf->mf_page_size);
  DL(tmp, pos, 2, hw, "mf_dirty", "%i", mf->mf_dirty);
  DL(tmp, pos, 2, hw, "mf_blocknr_min", "%i", mf->mf_blocknr_min);
  DL(tmp, pos, 2, hw, "mf_blocknr_max", "%i", mf->mf_blocknr_max);
  DL(tmp, pos, 2, hw, "mf_neg_count", "%i", mf->mf_neg_count);

  // bhdr_T *mf_free_first;             /// first block header in free list
  // bhdr_T *mf_used_first;             /// mru block header in used list
  // bhdr_T *mf_used_last;              /// lru block header in used list
  //
  // mf_hashtab_T mf_hash;              /// hash lists
  // mf_hashtab_T mf_trans;             /// trans lists
}

#define DATA_ID        (('d' << 8) + 'a')   // data block id
#define PTR_ID         (('p' << 8) + 't')   // pointer block id
#define BLOCK0_ID      (('0' << 8) + 'b')   // block0 id0 & id1
static void CountBlockHeaders(bhdr_T* bh, char* tmp, int* pos, int idt, int hw, const char* tag) {
  int data0_ct = 0;
  int ptr_ct = 0;
  int data_ct = 0;
  int unknown_ct = 0;
  while (bh) {
    uint16_t* p_dataid = bh->bh_data;  // look at first two bytes of data buffer
    switch (*p_dataid) {
      case DATA_ID:
        ++data_ct;
        break;
      case BLOCK0_ID:
        ++data0_ct;
        break;
      case PTR_ID:
        ++ptr_ct;
        break;
      default:
        ++unknown_ct;
        break;
    }
    bh = bh->bh_next;
  }
  
  DL(tmp, pos, idt, hw, tag, "d0=%i, ptr=%i, data=%i, unk=%i", data0_ct, ptr_ct, data_ct, unknown_ct);
}

static void DumpBlockHeaders(buf_T* buf, char* tmp) {
  memfile_T* mf = buf->b_ml.ml_mfp; 

  int pos = 0;
  int hw = 20;

  bhdr_T* cur = mf->mf_used_first;

  DH(tmp, &pos, 0, hw, "block headers:", true);

  int idt = 2;
  CountBlockHeaders(cur, tmp, &pos, idt, hw, "bh used ct");
  CountBlockHeaders(mf->mf_free_first, tmp, &pos, idt, hw, "bh free ct");
  DL(tmp, &pos, idt, hw, "used first", "%p", cur);
  DL(tmp, &pos, idt, hw, "used last", "%p", mf->mf_used_last);
  DHR(tmp, &pos, idt, hw);

  int ct = 0;

  bool show_data_block = true;   // limit number of data blocks to show
                               //
  while (cur) {
    uint16_t* p_dataid = cur->bh_data;  // look at first two bytes of data buffer
                                       
    if (*p_dataid != DATA_ID || show_data_block) {
      DL(tmp, &pos, idt, hw, "bhdr_T*:", "%p", cur);

      int dlt = 2; // indent delta
      DLD(tmp, &pos, idt, hw, dlt, "num/hashkey:", "%i", cur->bh_hashitem.mhi_key);
      // DL(tmp, &pos, 2, hw -2, "prev:", "%p", cur->bh_prev);
      // DL(tmp, &pos, 2, hw -2, "next:", "%p", cur->bh_next);
      // DL(tmp, &pos, 2, hw -2, "page count:", "%i", cur->bh_page_count);
    }

    dlt += 2;
    if (*p_dataid == BLOCK0_ID) {
      DLD(tmp, &pos, idt, hw, dlt, "data (block0):", "%p", cur->bh_data);
      block0_T* b0 = cur->bh_data;
      DLD(tmp, &pos, idt, hw, dlt, "uname:", "%s", b0->b0_uname);
      DLD(tmp, &pos, idt, hw, dlt, "fname:", "%.20s", b0->b0_fname);   // buffer file name if any
    } else if (*p_dataid == PTR_ID) {
      DLD(tmp, &pos, idt, hw, dlt, "data (ptr):", "%p", cur->bh_data);
      pointer_block_T* pb = cur->bh_data;
      DLD(tmp, &pos, idt, hw, dlt, "ct:", "%i", pb->pb_count);
      DLD(tmp, &pos, idt, hw, dlt, "max:", "%i", pb->pb_count_max);

      pointer_entry_T* pe = pb->pb_pointer;
      for (int i = 0; i < pb->pb_count && i < 5; ++i) {   // limit number of pointer entries to show
        DLD(tmp, &pos, idt, hw, dlt, "ptr:", "bnum=%i, lnct=%i, pgct=%i", pe->pe_bnum, pe->pe_line_count, pe->pe_page_count);
        ++pe;
      }
    } else if (*p_dataid == DATA_ID) {
      if (show_data_block) {
        DLD(tmp, &pos, idt, hw, dlt, "data:", "%p", cur->bh_data);
        data_block_T* data = cur->bh_data;
        DLD(tmp, &pos, idt, hw, dlt, "line ct:", "%i", data->db_line_count);
        DLD(tmp, &pos, idt, hw, dlt, "free:", "%i", data->db_free);
        show_data_block = false;
      }
    } else {
      // unexpected
    }

    cur = cur->bh_next;
    ++ct;
    if (ct > 5) {
      break;
    }
  }
}

// ----------------------------------------------------------------------------
// window dump
// ----------------------------------------------------------------------------
static void KeiDumpWinCore(win_T* win, char* tmp, int* pos) {
  int idt = 2;   // indent
  int hw = 20;   // header width

  DL(tmp, pos, 0, idt + hw, "window:", "%p", win);
  DL(tmp, pos, idt, hw, "handle:", "%i", win->handle);
  DL(tmp, pos, idt, hw, "buffer:", "%p", win->w_buffer);
  DL(tmp, pos, idt, hw, "w_width:", "%i", win->w_width);
  DL(tmp, pos, idt, hw, "w_height:", "%i", win->w_height);
  DH(tmp, pos, idt, hw, "w_cursor:", true);
  DL(tmp, pos, idt + 2, hw - 2, "lnum:", "%i", win->w_cursor.lnum);
  DL(tmp, pos, idt + 2, hw - 2, "col:", "%i", win->w_cursor.col);
  DL(tmp, pos, idt + 2, hw - 2, "coladd:", "%i", win->w_cursor.coladd);
}

static int dump_win_ct = 0;

void KeiDumpWin(win_T* win) {
  char tmp[LOCAL_BUF_SIZE];

  int pos = 0;
  KeiDumpWinCore(win, tmp, &pos);

  sprintf(tmp + pos, "\n(about %i / %i bytes left in local buffer)", LOCAL_BUF_SIZE - (pos + 50), LOCAL_BUF_SIZE);
  DumpToFile("window_dump", &dump_win_ct, "%s", tmp);
}
// ----------------------------------------------------------------------------
// full dump
// ----------------------------------------------------------------------------

static int GetMaxWidth(char* tmp, int* line_ct) {

  char* p = strtok(tmp, "\n");
  unsigned long max = strlen(p);
  int ct = 0;
  int total_read = 0;

  while (p != NULL) {
    ++ct;
    p = strtok(NULL, "\n");
    if (p != NULL) {
      unsigned long len = strlen(p);
      total_read += len + 1;
      if (len > max) {
        max = len;
      }
    }
    
  }

  *line_ct = ct;

  return (int)max;
}
 
#define MAX_BOX_WIDTH 10

static void Box(char* tmp_main, int* pos, int w, int h, int ct, ...) {

  // ToDo: this should be easier to put a specific box at the correct location in the main
  //       buffer instead of populating the main buffer line by line.

  if (w > MAX_BOX_WIDTH) {
    w = MAX_BOX_WIDTH;
  }

  int max_line_ct = 0;

  // ToDo: dynamically allocate instead??
  int max_widths[MAX_BOX_WIDTH];
  int line_cts[MAX_BOX_WIDTH];
  char* contents[MAX_BOX_WIDTH];
  int tmp_pos[MAX_BOX_WIDTH];

  va_list args;
  va_start(args, ct);

  for (int i = 0; i < w; ++i) {

    contents[i] = va_arg(args, char*);
    max_widths[i] = GetMaxWidth(contents[i], line_cts + i);
    tmp_pos[i] = 0;

    if (line_cts[i] > max_line_ct) {
      max_line_ct = line_cts[i];
    }

    if (i > 0) {
      *pos += sprintf(tmp_main + *pos, " ");
    }

    DumpRepeatedChar(tmp_main, pos, '-', max_widths[i] + 4, i == w - 1);
  }

  for (int i = 0; i <= max_line_ct; ++i) {
    for (int j = 0; j < w; ++j) {
      if (j > 0) {
        *pos += sprintf(tmp_main + *pos, " ");
      }
      if (i < line_cts[j]) {
        char* line = contents[j] + tmp_pos[j];
        *pos += sprintf(tmp_main + *pos, "| %-*s |", max_widths[j], line);
        tmp_pos[j] += strlen(line) + 1;
      } else if (i == line_cts[j]) {
        DumpRepeatedChar(tmp_main, pos, '-', max_widths[j] + 4, false);
      } else {
        *pos += sprintf(tmp_main + *pos, "%*s", max_widths[j] + 4, "");
      }
    }

    *pos += sprintf(tmp_main + *pos, "\n");
  }

  va_end(args);
}

static int dump_full_ct = 0;

void KeiDump(void) {
  char tmp_main[LOCAL_BUF_SIZE];

  char tmp_buf[LOCAL_BUF_SIZE];
  char tmp_bh[LOCAL_BUF_SIZE];

  int pos_buf = 0;
  // KeiDumpBufCore(curbuf, tmp_buf);
  //
  DumpBuf(curbuf, tmp_buf, &pos_buf);
  DumpBlockHeaders(curbuf, tmp_bh);

  //KeiDumpWinCore(curwin, tmp_win, &pos_win_buf);

  int pos = 0;
  // Box(tmp_main, &pos, 1, 1, 1, tmp_buf);
  Box(tmp_main, &pos, 2, 1, 2, tmp_buf, tmp_bh);
  /* int pos = Box(tmp_main, 0, 2, 1, 2, tmp_buf, tmp_win); */

  sprintf(tmp_main + pos, "\n(about %i / %i bytes left in local buffer)", LOCAL_BUF_SIZE - (pos + 50), LOCAL_BUF_SIZE);
  DumpToFile("full_dump", &dump_full_ct, "%s", tmp_main);
}

/* static int g_dump_ct = 0; */
/* void KeiDump(char* format, ...) { */
/*   FILE* fp = fopen(DUMP_FILE, g_dump_ct == 0 ? "w" : "a"); */
/*   va_list args; */
/*   va_start(args, format); */
/*   vfprintf(fp, format, args); */
/*   va_end(args); */

/*   ++g_dump_ct; */
/* } */

// ----------------------------------------------------------------------------
// state dump
// ----------------------------------------------------------------------------


static char* DumpKey(char* buf, int key) {
  if (key == K_EVENT) { 
    sprintf(buf, "K_EVENT"); 
  } else if (key == 27) {
    sprintf(buf, "ESC");
  } else {
    sprintf(buf, 0x20 <= key && key < 0x80 ? "'%c'" : "%i", key);
  }
  return buf;
}

static char* DumpBool(char* buf, bool b) {
  sprintf(buf, b ? "1" : "0");
  return buf;
}

static char* DumpInt(char* buf, int i) {
  sprintf(buf, "%i", i);
  return buf;
}

static char* DumpLong(char* buf, long l) {
  sprintf(buf, "%li", l);
  return buf;
}

static char* DumpPosT(char* buf, pos_T* pos) {
  sprintf(buf, "lnum: %i, col: %i, coladd: %i", pos->lnum, pos->col, pos->coladd);
  return buf;
}

static char* DumpMotionType(char* buf, MotionType mt) {
  sprintf(buf, mt == 0 ? "char" : (mt == 1 ? "line" : (mt == 2 ? "block" : "unknown")));
  return buf;
}

static int g_state_dump_ct = 1;
static state_check_callback g_normal_check = 0;
static state_check_callback g_insert_check = 0;
static state_check_callback g_cmnd_line_check = 0;

void KeiSetNormalCheck(state_check_callback callback) {
  g_normal_check = callback;
}

void KeiSetInsertCheck(state_check_callback callback) {
  g_insert_check = callback;
}

void KeiSetCmndLineCheck(state_check_callback callback) {
  g_cmnd_line_check = callback;
}


void KeiDumpVimState(VimState* state, int key) {
  return;

  FILE* fp = fopen(STATE_DUMP_FILE, g_state_dump_ct == 0 ? "w" : "a");
  char buf[256];

  if (state->check == g_normal_check) {
    NormalState* ns = (NormalState*)state; 

    fprintf(fp, "#%i:NORMAL, Key: %s\n", g_state_dump_ct++, DumpKey(buf, key));
    fprintf(fp, "  command_finished: %s", DumpBool(buf, ns->command_finished));
    fprintf(fp, "  |  ctrl_w          : %s", DumpBool(buf, ns->ctrl_w));
    fprintf(fp, "  |  need_flushbuf   : %s\n", DumpBool(buf, ns->need_flushbuf));
    fprintf(fp, "  set_prevcount   : %s", DumpBool(buf, ns->set_prevcount));
    fprintf(fp, "  |  previous_got_int: %s", DumpBool(buf, ns->previous_got_int));
    fprintf(fp, "  |  cmdwin          : %s\n", DumpBool(buf, ns->cmdwin));
    fprintf(fp, "  noexmode        : %s", DumpBool(buf, ns->noexmode));
    fprintf(fp, "  |  toplevel        : %s\n", DumpBool(buf, ns->toplevel));
    fprintf(fp, "  mapped_len      : %s", DumpInt(buf, ns->mapped_len));
    fprintf(fp, "  |  old_mapped_len  : %s", DumpInt(buf, ns->old_mapped_len));
    fprintf(fp, "  |  idx             : %s\n", DumpInt(buf, ns->idx));
    fprintf(fp, "  c               : %s", DumpInt(buf, ns->c));
    fprintf(fp, "  |  old_col         : %s\n", DumpInt(buf, ns->old_col));
    fprintf(fp, "  old_pos         : %s\n", DumpPosT(buf, &ns->old_pos));
    fprintf(fp, "  oa:\n");
    fprintf(fp, "    use_reg_one   : %s", DumpBool(buf, ns->oa.use_reg_one));
    fprintf(fp, "  |  inclusive     : %s", DumpBool(buf, ns->oa.inclusive));
    fprintf(fp, "  |  end_adjusted  : %s\n", DumpBool(buf, ns->oa.end_adjusted));
    fprintf(fp, "    empty         : %s", DumpBool(buf, ns->oa.empty));
    fprintf(fp, "  |  is_VIsual     : %s\n", DumpBool(buf, ns->oa.is_VIsual));
    fprintf(fp, "    op_type       : %s", DumpInt(buf, ns->oa.op_type));
    fprintf(fp, "  |  regname       : %s", DumpInt(buf, ns->oa.regname));
    fprintf(fp, "  |  motion_force  : %s\n", DumpInt(buf, ns->oa.motion_force));
    fprintf(fp, "    start_vcol    : %s", DumpInt(buf, ns->oa.start_vcol));
    fprintf(fp, "  |  end_vcol      : %s\n", DumpInt(buf, ns->oa.end_vcol));
    fprintf(fp, "    line_count    : %s", DumpLong(buf, ns->oa.line_count));
    fprintf(fp, "  |  prev_opcount  : %s", DumpLong(buf, ns->oa.prev_opcount));
    fprintf(fp, "  |  prev_count0   : %s\n", DumpLong(buf, ns->oa.prev_count0));
    fprintf(fp, "    start         : %s\n", DumpPosT(buf, &ns->oa.start));
    fprintf(fp, "    end           : %s\n", DumpPosT(buf, &ns->oa.end));
    fprintf(fp, "    cursor_start  : %s\n", DumpPosT(buf, &ns->oa.cursor_start));
    fprintf(fp, "    motion_type   : %s\n", DumpMotionType(buf, ns->oa.motion_type));
  } else if (state->check == g_insert_check) {
    InsertState* is = (InsertState*)state;
    fprintf(fp, "#%i:INSERT, Key: %s\n", g_state_dump_ct++, DumpKey(buf, key));
    fprintf(fp, "  did_backspace   : %s", DumpBool(buf, is->did_backspace));
    fprintf(fp, "  |  line_is_white   : %s", DumpBool(buf, is->line_is_white));
    fprintf(fp, "  |  nomove          : %s\n", DumpBool(buf, is->nomove));
    fprintf(fp, "  mincol          : %s", DumpInt(buf, is->mincol));
    fprintf(fp, "  |  cmdchar         : %s", DumpInt(buf, is->cmdchar));
    fprintf(fp, "  |  startln         : %s\n", DumpInt(buf, is->startln));
    fprintf(fp, "  c               : %s", DumpInt(buf, is->c));
    fprintf(fp, "  |  lastc           : %s", DumpInt(buf, is->lastc));
    fprintf(fp, "  |  i               : %s\n", DumpInt(buf, is->i));
    fprintf(fp, "  old_topfill     : %s", DumpInt(buf, is->old_topfill));
    fprintf(fp, "  |  inserted_space  : %s", DumpInt(buf, is->inserted_space));
    fprintf(fp, "  |  replaceState    : %s\n", DumpInt(buf, is->replaceState));
    fprintf(fp, "  did_restart_edit: %s\n", DumpInt(buf, is->did_restart_edit));
    fprintf(fp, "    old_topline   : %s", DumpLong(buf, is->old_topline));
    fprintf(fp, "  |  count         : %s\n", DumpLong(buf, is->count));
  } else if (state->check == g_cmnd_line_check) {
    CommandLineState* cls = (CommandLineState*)state;
    fprintf(fp, "#%i:COMMAND, Key: %s\n", g_state_dump_ct++, DumpKey(buf, key));
    fprintf(fp, "  |  firstc          : %s", DumpInt(buf, cls->firstc));
    fprintf(fp, "  |  indent          : %s\n", DumpInt(buf, cls->indent));
    fprintf(fp, "  c               : %s", DumpInt(buf, cls->c));
    fprintf(fp, "  |  gotesc          : %s", DumpInt(buf, cls->gotesc));
    fprintf(fp, "  |  do_abbr         : %s\n", DumpInt(buf, cls->do_abbr));
    fprintf(fp, "  hiscnt          : %s", DumpInt(buf, cls->hiscnt));
    fprintf(fp, "  |  save_hiscnt     : %s", DumpInt(buf, cls->save_hiscnt));
    fprintf(fp, "  |  histype         : %s\n", DumpInt(buf, cls->histype));
    fprintf(fp, "  save_hiscnt     : %s", DumpInt(buf, cls->save_hiscnt));
    fprintf(fp, "  |  histype         : %s", DumpInt(buf, cls->histype));
    fprintf(fp, "  |  did_wild_list   : %s\n", DumpInt(buf, cls->did_wild_list));
    fprintf(fp, "  wim_index       : %s", DumpInt(buf, cls->wim_index));
    fprintf(fp, "  |  res             : %s", DumpInt(buf, cls->res));
    fprintf(fp, "  |  save_msg_scroll : %s\n", DumpInt(buf, cls->save_msg_scroll));
    fprintf(fp, "  save_State      : %s", DumpInt(buf, cls->save_State));
    fprintf(fp, "  |  some_key_typed  : %s", DumpInt(buf, cls->some_key_typed));
    fprintf(fp, "  |ignore_drag_release: %s\n", DumpInt(buf, cls->ignore_drag_release));
    fprintf(fp, "  break_ctrl_c    : %s\n", DumpInt(buf, cls->break_ctrl_c));
  }


  /* incsearch_state_T is_state; */
  /* char_u *lookfor;                      // string to match */
  /* char_u   *save_p_icm; */

  // mouse drag and release events are ignored, unless they are
  // preceded with a mouse down event
  /* expand_T xpc; */
  /* long *b_im_ptr; */

  fprintf(fp, "\n");
  fclose(fp);
}

static int gTTYDumpCt = 0;

void KeiDumpTTYData(uv_buf_t* bufs, unsigned buf_ct) {

  char filename[32];
  sprintf(filename, "dump/tty-dump-%04i.txt", gTTYDumpCt++);

  // need to create build/dump
  //
  FILE* fp = fopen(filename, "w");

  for (unsigned i = 0; i < buf_ct; ++i) {
    fwrite(bufs[i].base, 1, (unsigned long)bufs[i].len, fp);
  }

  fclose(fp);
}

// navigation
// -buffer
//  - [0 ~ N]
//    - memline
//      - 
//     
// -window

static int level = 0;
static int nav[5] = {0, 0, 0, 0, 0};

static void menu() {
  char tmp[LOCAL_BUF_SIZE];
  int pos = 0;
  if (level == 0) {
    pos += sprintf(tmp + pos, "%s buffer\n", nav[0] == 0 ? ">" : " ");
    pos += sprintf(tmp + pos, "%s window\n", nav[0] == 1 ? ">" : " ");

    nav[0] = (nav[0] + 1) % 2;
  }

  DumpToMainLogOnly("full_dump", "%s", tmp);
}


bool KeiSteal(int key) {
  switch (key) {
    case 155:
      // menu();
      //DumpToMainLogOnly("full_dump", "%s", "Up");
      return true;
    case 157:
      // DumpToMainLogOnly("full_dump", "%s", "Down");
      return true;
    case 159:
      // DumpToMainLogOnly("full_dump", "%s", "Left");
      return true;
    case 162:
      // DumpToMainLogOnly("full_dump", "%s", "Right");
      return true;
  }

  return false;
}
