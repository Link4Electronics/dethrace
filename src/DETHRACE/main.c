
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#include <windows.h>
#endif

#include "brender.h"

extern int Harness_Init(int* argc, char* argv[]);
extern int Harness_Quit(void);
extern int original_main(int pArgc, char* pArgv[]);

void BR_CALLBACK _BrBeginHook(void) {
    struct br_device* BR_EXPORT BrDrv1SoftPrimBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1SoftRendBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1VirtualFramebufferBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1GLBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1SDL3GPURENDBegin(char* arguments);

#if _MSC_VER != 1020
    BrDevAddStatic(NULL, BrDrv1SoftPrimBegin, NULL);
    BrDevAddStatic(NULL, BrDrv1SoftRendBegin, NULL);
    BrDevAddStatic(NULL, BrDrv1VirtualFramebufferBegin, NULL);
    BrDevAddStatic(NULL, BrDrv1GLBegin, NULL);
    BrDevAddStatic(NULL, BrDrv1SDL3GPURENDBegin, NULL);
#endif
}

void BR_CALLBACK _BrEndHook(void) {
}

#if _MSC_VER == 1020
// The original CARM95.EXE links wincrt0.obj: WinMainCRTStartup calls WinMain.
// This mirrors the original WinMain (command line flag parsing + GameMain).

// Added by dethrace. Windows-specific. Original variable names unknown.

// GLOBAL: CARM95 0x0053df28
HINSTANCE gWin32_hinstance;
// GLOBAL: CARM95 0x0053df20
int gWin32_cmd_show;
// Copied to gGraf_spec_index by PDInitScreenVars in the original
// GLOBAL: CARM95 0x0051d600
int gHires_specified;
// Returned by WinMain; never written by game code
// GLOBAL: CARM95 0x0051d5a4
int gWin32_exit_code;

extern char gNetwork_profile_fname[256];
extern int gNetwork_profile_file_exists;
extern int gCut_scene_override;
extern void GameMain(int pArgc, char** pArgv);

int __cdecl _CrtSetDbgFlag(int);

// IDA: int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
// Local variable names are meaningless strings chosen so MSVC 4.2 assigns each
// stack slot the same displacement as the original (allocation order depends on
// the identifier). tj=argc, jcrz0=argv_str, pb=argv, eiuz2=dbg_flag, xb81l=cur_dir_len.
// FUNCTION: CARM95 0x004a61ca
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    int tj;
    char* jcrz0;
    char** pb;
    int eiuz2;
    DWORD xb81l;

    tj = 1;
    jcrz0 = "Carmageddon";
    pb = &jcrz0;

    eiuz2 = _CrtSetDbgFlag(-1);
    eiuz2 |= 0x01;
    eiuz2 &= ~0x04;
    eiuz2 &= ~0x20;
    eiuz2 &= ~0x02;
    _CrtSetDbgFlag(eiuz2);

    if (strlen(lpCmdLine) > 0) {
        if (strstr(lpCmdLine, "-hires") != NULL) {
            gHires_specified = 1;
        }
    }
    if (strlen(lpCmdLine) > 0) {
        if (strstr(lpCmdLine, "-nocutscenes") != NULL) {
            gCut_scene_override = 1;
        }
    }

    gWin32_hinstance = hInstance;
    gWin32_cmd_show = nCmdShow;

    gNetwork_profile_fname[0] = '\0';
    xb81l = GetCurrentDirectoryA(240, gNetwork_profile_fname);
    if (xb81l != 0 && xb81l == strlen(gNetwork_profile_fname)) {
        gNetwork_profile_file_exists = 1;
        strcat(gNetwork_profile_fname, "\\");
        strcat(gNetwork_profile_fname, "NETWORK.INI");
    }

    GameMain(tj, pb);
    return gWin32_exit_code;
}
#endif

int main(int argc, char* argv[]) {
    int result;

#ifdef _WIN32
#if _MSC_VER != 1020
    /* Attach to the console that started us if any */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        /* We attached successfully, lets redirect IO to the consoles handles if not already redirected */
        if (_fileno(stdout) == -2 || _get_osfhandle(_fileno(stdout)) == -2) {
            freopen("CONOUT$", "w", stdout);
        }

        if (_fileno(stderr) == -2 || _get_osfhandle(_fileno(stderr)) == -2) {
            freopen("CONOUT$", "w", stderr);
        }

        if (_fileno(stdin) == -2 || _get_osfhandle(_fileno(stdin)) == -2) {
            freopen("CONIN$", "r", stdin);
        }
    }
#endif
#endif

    result = Harness_Init(&argc, argv);
    if (result != 0) {
        return result;
    }

    result = original_main(argc, argv);

    Harness_Quit();

    return result;
}
