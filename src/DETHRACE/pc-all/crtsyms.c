// Annotations for CRT library internals referenced by library code.
// Declarations only - the definitions live inside the CRT.
// GLOBAL: CARM95 0x0052d130
extern int holdrand;

// GLOBAL: CARM95 0x0052d424
extern int _osver;
// GLOBAL: CARM95 0x0052d428
extern int _winver;
// GLOBAL: CARM95 0x0052d42c
extern int _winmajor;
// GLOBAL: CARM95 0x0052d430
extern int _winminor;
// GLOBAL: CARM95 0x0052d468
extern int _aenvptr;
// GLOBAL: CARM95 0x00553ba4
extern char* _acmdln;
// GLOBAL: CARM95 0x0052d440
extern char** _environ;
// GLOBAL: CARM95 0x0052d448
extern unsigned short** _wenviron;
// GLOBAL: CARM95 0x0052d9e8
extern int _newmode;
// GLOBAL: CARM95 0x0052e4a0
extern int ctrlc_action;
// GLOBAL: CARM95 0x0052e4a4
extern int ctrlbreak_action;
// GLOBAL: CARM95 0x0052e4a8
extern int abort_action;
// GLOBAL: CARM95 0x0052e4ac
extern int term_action;
// GLOBAL: CARM95 0x0052e6d8
extern int _First_FPE_Indx;
// GLOBAL: CARM95 0x0052e6dc
extern int _Num_FPE;
// GLOBAL: CARM95 0x0052e6e4
extern int _fpecode;
// GLOBAL: CARM95 0x0052e6e8
extern int* _pxcptinfoptrs;
// GLOBAL: CARM95 0x0052e648
extern int __lc_codepage;
// GLOBAL: CARM95 0x0052e9a4
extern double _HUGE;
// GLOBAL: CARM95 0x0052e9e8
extern long _timezone;
// GLOBAL: CARM95 0x0052e9ec
extern int _daylight;
// GLOBAL: CARM95 0x0052e9f0
extern long _dstbias;
// GLOBAL: CARM95 0x0052eb08
extern int _days[];
// GLOBAL: CARM95 0x0052ead0
extern int _lpdays[];
// GLOBAL: CARM95 0x0052eaa8
extern int tb[9];
// GLOBAL: CARM95 0x00544b10
extern char* nextoken;
// GLOBAL: CARM95 0x00544b18
extern int dstflag_cache;
// GLOBAL: CARM95 0x00544b20
extern long gmt_cache;
// GLOBAL: CARM95 0x00544b60
extern char buf[26];

// GLOBAL: CARM95 0x0052d510
extern void* __OP_ATANjmptab[];
// GLOBAL: CARM95 0x0052d120
extern int __adjust_fdiv;
// GLOBAL: CARM95 0x0052e9e0
extern int __matherr_flag;

int dethrace_crtsyms_anchor;
