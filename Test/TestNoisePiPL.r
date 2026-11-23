#include "AEConfig.h"
#include "AE_EffectVers.h"
// AEがプラグインを検出するためのメタデータ。ビルド時にPiPLリソースへ変換する。
resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "Noise" },
        Category { "Test" },
        // Windows x64版が公開する描画エントリーポイント。
        CodeWin64X86 { "EffectMain" },
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        // C++側のPF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1)に対応する値。
        AE_Effect_Version { 524289 },
        AE_Effect_Info_Flags { 0 },
        // TestNoise.cppのFlags / Flags2と一致させる。
        AE_Effect_Global_OutFlags { 0x02000404 },
        AE_Effect_Global_OutFlags_2 { 0x08001400 },
        // 表示名とは別の識別名。保存済みプロジェクトとの互換性のため維持する。
        AE_Effect_Match_Name { "Test TestNoise" },
        AE_Reserved_Info { 0 }
    }
};
