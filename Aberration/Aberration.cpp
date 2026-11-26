#define PF_DEEP_COLOR_AWARE 1
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "SPBasic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace {
enum { Input, RedOffset, GreenOffset, BlueOffset, RedDir, GreenDir, BlueDir, Channel, Blend, ParamCount };
constexpr A_long InputCheckout = 1;
constexpr A_long Flags = PF_OutFlag_DEEP_COLOR_AWARE;
constexpr A_long Flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
    PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_REVEALS_ZERO_ALPHA;

struct Offset {
    A_long x, y;
    double fx, fy;
};
struct Settings {
    Offset sample[3];
    double blend;
    bool moveAlpha;
    bool identity;
};

double Scale(PF_RationalScale scale) {
    return static_cast<double>(scale.num) / scale.den;
}

Settings ReadSettings(const PF_InData* in, PF_ParamDef* const params[]) {
    Settings s{};
    s.blend = params[Blend]->u.fs_d.value / 100.0;
    s.moveAlpha = params[Channel]->u.pd.value == 2;
    s.identity = true;
    for (int i = 0; i < 3; ++i) {
        const double distance = params[RedOffset + i]->u.fs_d.value;
        const double radians = params[RedDir + i]->u.fs_d.value * (3.14159265358979323846 / 180.0);
        // Inverse mapping. Angle zero moves up, and positive angles turn clockwise.
        const double x = -distance * std::sin(radians) * Scale(in->downsample_x);
        const double y = distance * std::cos(radians) * Scale(in->downsample_y);
        const double sx = std::abs(x - std::round(x)) < 1e-10 ? std::round(x) : x;
        const double sy = std::abs(y - std::round(y)) < 1e-10 ? std::round(y) : y;
        const auto ix = static_cast<A_long>(std::floor(sx));
        const auto iy = static_cast<A_long>(std::floor(sy));
        s.sample[i] = {ix, iy, sx - ix, sy - iy};
        s.identity = s.identity && sx == 0 && sy == 0;
    }
    s.identity = s.identity || s.blend == 1.0;
    return s;
}

PF_Err CheckoutSettings(PF_InData* in, Settings& settings) {
    PF_ParamDef values[ParamCount]{};
    PF_ParamDef* params[ParamCount]{};
    PF_Err err = PF_Err_NONE;
    int index = RedOffset;
    for (; index < ParamCount; ++index) {
        err = PF_CHECKOUT_PARAM(in, index, in->current_time, in->time_step, in->time_scale, &values[index]);
        if (err) break;
        params[index] = &values[index];
    }
    if (!err) settings = ReadSettings(in, params);
    for (int i = RedOffset; i < index; ++i) {
        const PF_Err cleanup = PF_CHECKIN_PARAM(in, &values[i]);
        if (!err) err = cleanup;
    }
    return err;
}

PF_Err Params(PF_InData* in_data, PF_OutData* out_data) {
    PF_ParamDef def{};
    PF_ADD_FLOAT_SLIDERX("RedOffset", 0, 100, 0, 100, 2, 2, 0, 0, RedOffset);
    PF_ADD_FLOAT_SLIDERX("GreenOffset", 0, 100, 0, 100, 0, 2, 0, 0, GreenOffset);
    PF_ADD_FLOAT_SLIDERX("BlueOffset", 0, 100, 0, 100, 2, 2, 0, 0, BlueOffset);
    PF_ADD_FLOAT_SLIDERX("RedDir", -360, 360, -360, 360, -90, 2, 0, 0, RedDir);
    PF_ADD_FLOAT_SLIDERX("GreenDir", -360, 360, -360, 360, 0, 2, 0, 0, GreenDir);
    PF_ADD_FLOAT_SLIDERX("BlueDir", -360, 360, -360, 360, 90, 2, 0, 0, BlueDir);
    PF_ADD_POPUPX("Channel", 2, 1, "RGB|RGBA", 0, Channel);
    PF_ADD_FLOAT_SLIDERX("Blend With Original", 0, 100, 0, 100, 0, 2, PF_ValueDisplayFlag_PERCENT, 0, Blend);
    out_data->num_params = ParamCount;
    return PF_Err_NONE;
}

bool Empty(PF_LRect rect) {
    return rect.left >= rect.right || rect.top >= rect.bottom;
}

PF_LRect Intersect(PF_LRect a, PF_LRect b) {
    PF_LRect rect{std::max(a.left, b.left), std::max(a.top, b.top),
                  std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    return Empty(rect) ? PF_LRect{} : rect;
}

PF_LRect LayerBounds(const PF_InData* in) {
    return {0, 0, static_cast<A_long>(std::ceil(in->width * Scale(in->downsample_x))),
                  static_cast<A_long>(std::ceil(in->height * Scale(in->downsample_y)))};
}

PF_LRect OutputBounds(PF_LRect source, const Settings& s, bool preserveHidden) {
    if (Empty(source) || s.identity || (!s.moveAlpha && !preserveHidden)) return source;
    PF_LRect rect = source;
    for (const auto& offset : s.sample) {
        rect.left = std::min(rect.left, source.left - offset.x - (offset.fx != 0));
        rect.top = std::min(rect.top, source.top - offset.y - (offset.fy != 0));
        rect.right = std::max(rect.right, source.right - offset.x);
        rect.bottom = std::max(rect.bottom, source.bottom - offset.y);
    }
    return rect;
}

PF_Err PreRender(PF_InData* in, PF_PreRenderExtra* extra) {
    Settings s{};
    PF_Err err = CheckoutSettings(in, s);
    if (err) return err;
    PF_RenderRequest request = extra->input->output_request;
    const PF_LRect layer = LayerBounds(in);
    const PF_LRect target = Intersect(request.rect, layer);
    request.rect = target;
    if (!Empty(target) && !s.identity) {
        for (const auto& offset : s.sample) {
            request.rect.left = std::min(request.rect.left, target.left + offset.x);
            request.rect.top = std::min(request.rect.top, target.top + offset.y);
            request.rect.right = std::max(request.rect.right, target.right + offset.x + (offset.fx != 0));
            request.rect.bottom = std::max(request.rect.bottom, target.bottom + offset.y + (offset.fy != 0));
        }
        request.rect = Intersect(request.rect, layer);
        request.field = PF_Field_FRAME;
        request.channel_mask = PF_ChannelMask_ARGB;
    }
    PF_CheckoutResult result{};
    err = extra->cb->checkout_layer(in->effect_ref, Input, InputCheckout, &request,
        in->current_time, in->time_step, in->time_scale, &result);
    if (err) return err;
    extra->output->result_rect = Intersect(OutputBounds(result.result_rect, s, request.preserve_rgb_of_zero_alpha), target);
    extra->output->max_result_rect = Intersect(OutputBounds(result.max_result_rect, s, request.preserve_rgb_of_zero_alpha), layer);
    return PF_Err_NONE;
}

template<class Pixel>
const Pixel* Row(const PF_EffectWorld& world, A_long y) {
    return reinterpret_cast<const Pixel*>(static_cast<const char*>(static_cast<const void*>(world.data)) +
        static_cast<std::ptrdiff_t>(y) * world.rowbytes);
}

template<class Pixel>
class Source {
public:
    Source(const PF_EffectWorld* world, A_long x, A_long y, PF_LRect clip)
        : world_(world), x_(x), y_(y),
          bounds_(world && world->data ? Intersect(clip, {x, y, x + world->width, y + world->height}) : PF_LRect{}) {}

    Pixel At(A_long x, A_long y) const {
        if (x < bounds_.left || y < bounds_.top || x >= bounds_.right || y >= bounds_.bottom) return {};
        return Row<Pixel>(*world_, y - y_)[x - x_];
    }

private:
    const PF_EffectWorld* world_;
    A_long x_, y_;
    PF_LRect bounds_;
};

struct Sample {
    double color, alpha;
};

template<class Pixel>
Sample ReadSample(Pixel pixel, decltype(Pixel::red) Pixel::* channel) {
    // AE worlds already store premultiplied RGB. Ignore hidden RGB in the shifted samples.
    return {pixel.alpha != 0 ? static_cast<double>(pixel.*channel) : 0.0, static_cast<double>(pixel.alpha)};
}

double Mix(double a, double b, double fraction) {
    return a + (b - a) * fraction;
}

Sample MixSamples(Sample a, Sample b, double fraction) {
    return {Mix(a.color, b.color, fraction), Mix(a.alpha, b.alpha, fraction)};
}

template<class Pixel>
Sample Bilinear(const Source<Pixel>& source, A_long x, A_long y, const Offset& offset,
                decltype(Pixel::red) Pixel::* channel) {
    x += offset.x;
    y += offset.y;
    auto horizontal = [&](A_long row) {
        const Sample left = ReadSample(source.At(x, row), channel);
        return offset.fx == 0 ? left : MixSamples(left, ReadSample(source.At(x + 1, row), channel), offset.fx);
    };
    const Sample top = horizontal(y);
    return offset.fy == 0 ? top : MixSamples(top, horizontal(y + 1), offset.fy);
}

template<class Pixel>
void RenderPixels(const Source<Pixel>& source, PF_EffectWorld& dst, A_long x0, A_long y0,
                  PF_LRect clip, const Settings& s, bool preserveHidden) {
    using Component = decltype(Pixel::red);
    auto encode = [](double value) {
        if constexpr (std::is_same_v<Pixel, PF_PixelFloat>) {
            return static_cast<Component>(value);
        } else {
            constexpr double maximum = std::is_same_v<Pixel, PF_Pixel16> ? PF_MAX_CHAN16 : PF_MAX_CHAN8;
            return static_cast<Component>(std::clamp(value, 0.0, maximum) + 0.5);
        }
    };

    for (A_long row = 0; row < dst.height; ++row) {
        Pixel* output = const_cast<Pixel*>(Row<Pixel>(dst, row));
        const A_long y = y0 + row;
        for (A_long col = 0; col < dst.width; ++col) {
            const A_long x = x0 + col;
            const Pixel original = source.At(x, y);
            if (s.identity || x < clip.left || x >= clip.right || y < clip.top || y >= clip.bottom) {
                output[col] = original;
                continue;
            }
            if (!s.moveAlpha && original.alpha == 0) {
                output[col] = preserveHidden ? original : Pixel{};
                continue;
            }
            const Sample red = Bilinear(source, x, y, s.sample[0], &Pixel::red);
            const Sample green = Bilinear(source, x, y, s.sample[1], &Pixel::green);
            const Sample blue = Bilinear(source, x, y, s.sample[2], &Pixel::blue);
            const double alpha = s.moveAlpha ? (red.alpha + green.alpha + blue.alpha) / 3.0 : original.alpha;
            const double mixedAlpha = Mix(alpha, original.alpha, s.blend);
            const Component storedAlpha = encode(mixedAlpha);
            if (storedAlpha == 0) {
                output[col] = preserveHidden && original.alpha == 0 ? original : Pixel{};
                continue;
            }
            auto color = [&](Sample shifted, Component unshifted) {
                if (s.blend == 0) return encode(shifted.color * (storedAlpha / alpha));
                // Unmultiply before blending, then store the result in AE's premultiplied format.
                const double straightShifted = alpha != 0 ? shifted.color / alpha : 0.0;
                const double straightOriginal = original.alpha != 0 ? static_cast<double>(unshifted) / original.alpha : 0.0;
                return encode(Mix(straightShifted, straightOriginal, s.blend) * storedAlpha);
            };
            output[col] = {storedAlpha, color(red, original.red), color(green, original.green), color(blue, original.blue)};
        }
    }
}

template<class Pixel>
void RenderTyped(const PF_EffectWorld* src, PF_EffectWorld& dst, A_long srcX, A_long srcY,
                 A_long dstX, A_long dstY, PF_LRect clip, const Settings& s, bool preserveHidden) {
    const Source<Pixel> source(src, srcX, srcY, clip);
    RenderPixels(source, dst, dstX, dstY, clip, s, preserveHidden);
}

PF_Err Render(PF_InData* in, const PF_EffectWorld* src, PF_EffectWorld* dst, const Settings& s,
              bool smart, bool preserveHidden, int depth) {
    if (!dst || !dst->data) return PF_Err_NONE;
    const A_long srcX = smart ? (src ? src->origin_x : 0) : -in->pre_effect_source_origin_x;
    const A_long srcY = smart ? (src ? src->origin_y : 0) : -in->pre_effect_source_origin_y;
    const A_long dstX = smart ? dst->origin_x : srcX - in->output_origin_x;
    const A_long dstY = smart ? dst->origin_y : srcY - in->output_origin_y;
    const PF_LRect clip = LayerBounds(in);
    switch (depth) {
        case 8: RenderTyped<PF_Pixel8>(src, *dst, srcX, srcY, dstX, dstY, clip, s, preserveHidden); break;
        case 16: RenderTyped<PF_Pixel16>(src, *dst, srcX, srcY, dstX, dstY, clip, s, preserveHidden); break;
        case 32: RenderTyped<PF_PixelFloat>(src, *dst, srcX, srcY, dstX, dstY, clip, s, preserveHidden); break;
        default: return PF_Err_BAD_CALLBACK_PARAM;
    }
    return PF_Err_NONE;
}

PF_Err SmartRender(PF_InData* in, PF_SmartRenderExtra* extra) {
    Settings s{};
    PF_Err err = CheckoutSettings(in, s);
    if (err) return err;
    PF_EffectWorld *src = nullptr, *dst = nullptr;
    err = extra->cb->checkout_layer_pixels(in->effect_ref, InputCheckout, &src);
    if (err) return err;
    err = extra->cb->checkout_output(in->effect_ref, &dst);
    if (!err) err = Render(in, src, dst, s, true, extra->input->output_request.preserve_rgb_of_zero_alpha,
        extra->input->bitdepth);
    const PF_Err cleanup = extra->cb->checkin_layer_pixels(in->effect_ref, InputCheckout);
    return err ? err : cleanup;
}
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr ptr, PF_PluginDataCB2 cb,
    SPBasicSuite*, const char*, const char*) {
    PF_Err result = PF_Err_NONE;
    result = PF_REGISTER_EFFECT_EXT2(ptr, cb, "Aberration", "Aberration", "MyPlugins", AE_RESERVED_INFO, "EffectMain", "");
    return result;
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in, PF_OutData* out,
    PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    switch (cmd) {
        case PF_Cmd_ABOUT:
            std::snprintf(out->return_msg, sizeof(out->return_msg), "Aberration 1.0");
            break;
        case PF_Cmd_GLOBAL_SETUP:
            out->my_version = PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1);
            out->out_flags = Flags;
            out->out_flags2 = Flags2;
            break;
        case PF_Cmd_PARAMS_SETUP: return Params(in, out);
        case PF_Cmd_RENDER:
            return Render(in, &params[Input]->u.ld, output, ReadSettings(in, params), false, false,
                PF_WORLD_IS_DEEP(output) ? 16 : 8);
        case PF_Cmd_SMART_PRE_RENDER: return PreRender(in, static_cast<PF_PreRenderExtra*>(extra));
        case PF_Cmd_SMART_RENDER: return SmartRender(in, static_cast<PF_SmartRenderExtra*>(extra));
        default: break;
    }
    return PF_Err_NONE;
}
