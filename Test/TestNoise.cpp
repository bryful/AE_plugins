// SDKに16bpc対応を伝える。AE_Effect.hを読み込む前に定義する。
#define PF_DEEP_COLOR_AWARE 1
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "SPBasic.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <type_traits>

namespace {
// パラメータ配列の添字。0番はAEが用意する入力レイヤー。
enum { Input, Amount, Color, Blend, ParamCount };
// 16bpc対応、画素単位で独立した処理、パラメータ以外（時刻）による変化を宣言。
// PiPLリソースのフラグ値も、これらの宣言と一致させる。
constexpr A_long Flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_PIX_INDEPENDENT | PF_OutFlag_NON_PARAM_VARY;
// SmartFXによる32bpc処理と、複数フレームの並列レンダリングへの対応を宣言。
constexpr A_long Flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

// 入力値から乱数状の値を生成する。乱数の共有状態を持たないため、
// 画素の処理順序やスレッドの実行順序に結果が左右されない。
std::uint32_t Hash(std::uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15;
    v *= 0x846ca68bu; return v ^ (v >> 16);
}
// 強さ・ブレンドはUIの0〜100%を0〜1に換算して保持する。
struct Settings { double amount, blend; bool color; std::uint32_t seed; };
// 1回のレンダリングに必要な情報を、画素処理コールバックへ渡す。
struct RenderContext { Settings settings; PF_InData* in; };
Settings ReadSettings(PF_InData* in, PF_ParamDef* amount, PF_ParamDef* color, PF_ParamDef* blend) {
    // AEの時刻をマイクロ秒単位に換算し、時刻に応じた乱数の種を作る。
    // 浮動小数点から整数への変換で、マイクロ秒未満は切り捨てられる。
    const auto tick = static_cast<std::int64_t>(static_cast<double>(in->current_time) / in->time_scale * 1000000.0);
    return {amount->u.fs_d.value / 100.0, blend->u.fs_d.value / 100.0,
            color->u.bd.value != 0, Hash(static_cast<std::uint32_t>(tick) ^ static_cast<std::uint32_t>(tick >> 32))};
}
// 8/16/32bpcで共通の画素処理。型に応じた差分はコンパイル時に切り替える。
template<class Pixel> PF_Err PixelNoise(void* ref, A_long x, A_long y, Pixel* src, Pixel* dst) {
    const auto& ctx = *static_cast<RenderContext*>(ref);
    const auto& s = ctx.settings;
    const Pixel original = src ? *src : Pixel{};
    // 先に入力全体をコピーしてアルファを保持する。
    // 無加工になる設定では計算を省略し、入力値をそのまま返す。
    *dst = original;
    if (s.amount == 0 || s.blend == 1 || original.alpha == 0) return PF_Err_NONE;
    // バッファ原点の補正を加えた座標と時刻から、画素ごとの乱数を作る。
    const auto px = static_cast<std::uint32_t>(x - ctx.in->output_origin_x + ctx.in->pre_effect_source_origin_x);
    const auto py = static_cast<std::uint32_t>(y - ctx.in->output_origin_y + ctx.in->pre_effect_source_origin_y);
    const auto base = Hash(px ^ Hash(py) ^ s.seed);
    constexpr bool floating = std::is_same_v<Pixel, PF_PixelFloat>;
    // AEの16bpcの白は65535ではなくPF_MAX_CHAN16（32768）。
    // 32bpcは1.0を基準にするが、HDRの値域は0〜1に制限しない。
    constexpr double maximum = floating ? 1.0 : (std::is_same_v<Pixel, PF_Pixel16> ? PF_MAX_CHAN16 : PF_MAX_CHAN8);
    const double alpha = original.alpha / maximum;
    auto channel = [&](auto value, std::uint32_t salt) {
        // 符号付きの一様ノイズを作り、強さとアルファを掛けて加算する。
        // アルファを掛けるのは、乗算済みアルファのRGBとして扱うため。
        const double noise = (static_cast<double>(Hash(base ^ salt)) / 4294967295.0 * 2.0 - 1.0) * s.amount;
        const double source = value / maximum;
        double effected = source + noise * alpha;
        // 整数形式では有効範囲に収める。浮動小数点形式ではHDR値を保持する。
        if constexpr (!std::is_same_v<Pixel, PF_PixelFloat>) effected = std::clamp(effected, 0.0, alpha);
        // ブレンド0%はノイズ適用結果、100%は元画像になる。
        const double mixed = source + (effected - source) * (1.0 - s.blend);
        using Channel = decltype(value);
        if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) return static_cast<Channel>(mixed);
        // 整数形式へ戻す際は、0.5を加えて最も近い整数へ丸める。
        else return static_cast<Channel>(std::clamp(mixed * maximum, 0.0, maximum) + 0.5);
    };
    // カラーではRGBごとに異なる種、モノでは共通の種を使う。
    dst->red = channel(original.red, 0x12345678u);
    dst->green = channel(original.green, s.color ? 0x87654321u : 0x12345678u);
    dst->blue = channel(original.blue, s.color ? 0xfedcba98u : 0x12345678u);
    return PF_Err_NONE;
}

// 通常レンダリングとSmartFXから呼び出す、共通の描画処理。
PF_Err Render(PF_InData* in, PF_EffectWorld* src, PF_EffectWorld* dst, Settings settings, bool smart) {
    if (!dst || !dst->data) return PF_Err_NONE;
    const PF_WorldSuite2* world = nullptr;
    // SuiteはAEが提供する関数群。取得後は対応するReleaseSuiteで解放する。
    PF_Err err = in->pica_basicP->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, reinterpret_cast<const void**>(&world));
    if (err) return err;
    PF_PixelFormat format{};
    // プロジェクト設定の推測ではなく、実際の出力バッファ形式で処理を選ぶ。
    err = world->PF_GetPixelFormat(dst, &format);
    in->pica_basicP->ReleaseSuite(kPFWorldSuite, kPFWorldSuiteVersion2);
    if (err) return err;
    RenderContext ctx{settings, in};
    PF_Point origin{};
    origin.h = in->output_origin_x;
    origin.v = in->output_origin_y;
// 深度別のIterate Suiteを取得し、AEに画素の走査を任せる。
// nullptrの描画範囲は全画素を意味する。SmartFXではバッファ原点も渡す。
#define RUN_ITERATE(SUFFIX, VERSION, PIXEL) \
    { const PF_Iterate##SUFFIX##Suite2* suite = nullptr; \
      err = in->pica_basicP->AcquireSuite(kPFIterate##SUFFIX##Suite, VERSION, reinterpret_cast<const void**>(&suite)); \
      if (!err) { err = suite->iterate_origin(in, 0, dst->height, src, nullptr, smart ? &origin : nullptr, &ctx, PixelNoise<PIXEL>, dst); \
        in->pica_basicP->ReleaseSuite(kPFIterate##SUFFIX##Suite, VERSION); } }
    switch (format) {
        // ARGB32/64/128はアルファを含む1画素全体のビット数。
        case PF_PixelFormat_ARGB32: RUN_ITERATE(8, kPFIterate8SuiteVersion2, PF_Pixel8); break;
        case PF_PixelFormat_ARGB64: RUN_ITERATE(16, kPFIterate16SuiteVersion2, PF_Pixel16); break;
        case PF_PixelFormat_ARGB128: RUN_ITERATE(Float, kPFIterateFloatSuiteVersion2, PF_PixelFloat); break;
        default: err = PF_Err_BAD_CALLBACK_PARAM;
    }
#undef RUN_ITERATE
    return err;
}

// エフェクトコントロールに表示する項目を登録する。
// 末尾のID（1〜3）は保存済みプロジェクトとの対応に使うため、安易に変更しない。
PF_Err Params(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def{};
    // スライダーは有効範囲・表示範囲とも0〜100、表示精度は小数点以下2桁。
    PF_ADD_FLOAT_SLIDERX("ノイズの強さ", 0, 100, 0, 100, 10, 2, PF_ValueDisplayFlag_PERCENT, 0, 1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("カラーノイズ", "オン：カラー / オフ：モノ", true, 0, 2);
    PF_ADD_FLOAT_SLIDERX("オリジナルとブレンド", 0, 100, 0, 100, 0, 2, PF_ValueDisplayFlag_PERCENT, 0, 3);
    // 入力レイヤーも含めた総パラメータ数をAEに返す。
    out_data->num_params = ParamCount;
    return PF_Err_NONE;
}
// SmartFXの準備段階。出力要求に対応する入力画像の範囲をAEへ要求する。
// ノイズは画像の外側へ広がらないため、入力の描画範囲をそのまま返す。
PF_Err PreRender(PF_InData* in, PF_PreRenderExtra* extra) {
    PF_RenderRequest request = extra->input->output_request;
    // 完全透明画素のRGBも保持し、無加工時に元の値を残せるよう要求する。
    request.preserve_rgb_of_zero_alpha = true;
    PF_CheckoutResult result{};
    const PF_Err err = extra->cb->checkout_layer(in->effect_ref, Input, Input, &request,
        in->current_time, in->time_step, in->time_scale, &result);
    if (!err) { extra->output->result_rect = result.result_rect; extra->output->max_result_rect = result.max_result_rect; }
    return err;
}
// SmartFXの本描画。現在時刻のパラメータと入出力バッファを借り受ける。
PF_Err SmartRender(PF_InData* in, PF_SmartRenderExtra* extra) {
    PF_ParamDef values[3]{};
    // 途中で失敗しても、取得に成功した分だけを確実に返却する。
    int checked = 0;
    PF_Err err = PF_Err_NONE;
    for (int i = 0; i < 3 && !err; ++i) {
        err = PF_CHECKOUT_PARAM(in, Amount + i, in->current_time, in->time_step, in->time_scale, &values[i]);
        if (!err) ++checked;
    }
    PF_EffectWorld *src = nullptr, *dst = nullptr;
    bool pixels = false;
    if (!err) { err = extra->cb->checkout_layer_pixels(in->effect_ref, Input, &src); pixels = !err; }
    if (!err) err = extra->cb->checkout_output(in->effect_ref, &dst);
    if (!err) err = Render(in, src, dst, ReadSettings(in, &values[0], &values[1], &values[2]), true);
    // 描画の成否にかかわらず入力画素とパラメータを返却する。
    // 描画中のエラーがある場合は、後片付けのエラーで上書きしない。
    if (pixels) { const auto cleanup = extra->cb->checkin_layer_pixels(in->effect_ref, Input); if (!err) err = cleanup; }
    for (int i = 0; i < checked; ++i) { const auto cleanup = PF_CHECKIN_PARAM(in, &values[i]); if (!err) err = cleanup; }
    return err;
}
}

// AEへの登録用エントリーポイント。名前・Match Name・カテゴリはPiPLと揃える。
// extern "C"とDllExportで、AEが決められた関数名で呼び出せるようにする。
extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr ptr, PF_PluginDataCB2 cb,
    SPBasicSuite*, const char*, const char*) {
    PF_Err result = PF_Err_NONE;
    result = PF_REGISTER_EFFECT_EXT2(ptr, cb, "Noise", "Test TestNoise", "Test", AE_RESERVED_INFO, "EffectMain", "");
    return result;
}
// AEからのコマンドを振り分けるメイン関数。
// 初期化、パラメータ登録、描画は、それぞれ別のタイミングで呼ばれる。
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in, PF_OutData* out,
    PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    switch (cmd) {
        case PF_Cmd_ABOUT: std::snprintf(out->return_msg, sizeof(out->return_msg), "Noise 1.0 - TestNoise"); break;
        case PF_Cmd_GLOBAL_SETUP:
            out->my_version = PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1);
            out->out_flags = Flags; out->out_flags2 = Flags2; break;
        case PF_Cmd_PARAMS_SETUP: return Params(in, out);
        case PF_Cmd_RENDER: return Render(in, &params[Input]->u.ld, output, ReadSettings(in, params[Amount], params[Color], params[Blend]), false);
        case PF_Cmd_SMART_PRE_RENDER: return PreRender(in, static_cast<PF_PreRenderExtra*>(extra));
        case PF_Cmd_SMART_RENDER: return SmartRender(in, static_cast<PF_SmartRenderExtra*>(extra));
        default: break;
    }
    return PF_Err_NONE;
}
