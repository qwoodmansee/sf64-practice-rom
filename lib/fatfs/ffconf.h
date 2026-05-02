/*---------------------------------------------------------------------------/
/  Configurations of FatFs Module — N64 SF64 practice ROM
/---------------------------------------------------------------------------/
/  Authored locally from the upstream R0.15 template (see lib/fatfs/README.md
/  for upstream provenance). Every value below either matches the upstream
/  default OR has a `[CUSTOMIZED]` comment explaining WHY we changed it.
/  When pulling a new FatFs version, diff this file against the new template
/  and merge changes in carefully — these tuning choices are intentional.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Upstream R0.15 revision ID. */


/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
/* We need write — config persistence and save state files. */

#define FF_FS_MINIMIZE	0
/* Full API: f_open, f_read, f_write, f_close, f_opendir, f_readdir,
   f_unlink, f_mkdir, f_rename, f_stat. Phases 4-9 will use these. */

#define FF_USE_FIND		1
/* [CUSTOMIZED] Phase 6's file_browser uses f_findfirst/f_findnext. */

#define FF_USE_MKFS		0
/* We do not format SD cards from the ROM; user formats on PC. */

#define FF_USE_FASTSEEK	0
/* No large random-access files in our use case. */

#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0

#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	1
#define FF_PRINT_FLOAT	1
#define FF_STRF_ENCODE	3
/* No f_gets/f_putc/etc — we read/write binary save-state and config blobs. */


/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437
/* [CUSTOMIZED] US Latin (ASCII-compatible). Upstream default is 932 (Japanese
   DBCS) which would pull in a large code-page table. ASCII filenames only. */

#define FF_USE_LFN		1
#define FF_MAX_LFN		255
/* [CUSTOMIZED] LFN Mode 1: static working buffer in BSS. Single-threaded ROM
   so the "always not thread-safe" caveat doesn't apply. Avoids stack pressure
   on the N64's 4 KB thread stacks (Mode 2 would put 510 bytes on the stack
   per call) and avoids the malloc dependency Mode 3 would require. */

#define FF_LFN_UNICODE	0
/* [CUSTOMIZED] ANSI/OEM filenames (TCHAR = char). Saves the Unicode tables
   in ffunicode.c — they preprocess to mostly empty for code-page 437. */

#define FF_LFN_BUF		255
#define FF_SFN_BUF		12

#define FF_FS_RPATH		0
/* No current-directory tracking; absolute paths only. */


/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		1
/* One physical volume — one SD card per cart. */

#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"

#define FF_MULTI_PARTITION	0

#define FF_MIN_SS		512
#define FF_MAX_SS		512
/* Fixed 512-byte sectors. iodev_sd_* enforces this in its API contract. */

#define FF_LBA64		0
/* 32-bit sector addresses. 32 GB SD = ~62M sectors, fits in u32. exFAT
   support requires FF_LBA64=1; we don't support exFAT (see FF_FS_EXFAT). */

#define FF_MIN_GPT		0x10000000

#define FF_USE_TRIM		0


/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0

#define FF_FS_EXFAT		0
/* No exFAT — saves substantial code, and cards <32 GB use FAT32 by default.
   Users with >32 GB cards reformat to FAT32 on PC. */

#define FF_FS_NORTC		1
/* [CUSTOMIZED] No real-time clock on N64. All file timestamps will be the
   fixed value below. Phase 4+ may add a frame-counter-based clock if needed. */
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
/* [CUSTOMIZED] Default timestamp year. */

#define FF_FS_NOFSINFO	0

#define FF_FS_LOCK		0
/* No file locking — single-threaded I/O path. */

#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
/* No reentrancy — single-threaded. ff_mutex_* handlers therefore not needed. */


/*--- End of configuration options ---*/
