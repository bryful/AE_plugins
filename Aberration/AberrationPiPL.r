#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifdef AE_OS_MAC
#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "Aberration" },
        Category { "MyPlugins" },
#ifdef AE_OS_WIN
        CodeWin64X86 { "EffectMain" },
#elif defined(AE_OS_MAC)
        CodeMacIntel64 { "EffectMain" },
        CodeMacARM64 { "EffectMain" },
#endif
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        AE_Effect_Version { 524289 },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { 0x02000000 },
        AE_Effect_Global_OutFlags_2 { 0x08001480 },
        AE_Effect_Match_Name { "Aberration" },
        AE_Reserved_Info { 0 }
    }
};
