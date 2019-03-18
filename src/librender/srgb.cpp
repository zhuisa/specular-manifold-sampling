#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/properties.h>
#include <mitsuba/render/spectrum.h>
#include <mitsuba/render/srgb.h>
#include <rgb2spec.h>
#include <tbb/tbb.h>

NAMESPACE_BEGIN(mitsuba)

template MTS_EXPORT_RENDER Spectrumf srgb_model_eval(const Vector3f &, const Spectrumf &);
template MTS_EXPORT_RENDER SpectrumfP srgb_model_eval(const Vector3fP &, const SpectrumfP &);

template MTS_EXPORT_RENDER Float srgb_model_mean(const Vector3f &);
template MTS_EXPORT_RENDER FloatP srgb_model_mean(const Vector3fP &);

static RGB2Spec *model = nullptr;
static tbb::spin_mutex model_mutex;

Vector3f srgb_model_fetch(const Color3f &c) {
    if (unlikely(model == nullptr)) {
        tbb::spin_mutex::scoped_lock sl(model_mutex);
        if (model == nullptr) {
            FileResolver *fr = Thread::thread()->file_resolver();
            std::string fname = fr->resolve("data/srgb.coeff").string();
            Log(EInfo, "Loading spectral upsampling model \"data/srgb.coeff\" .. ");
            model = rgb2spec_load(fname.c_str());
            if (model == nullptr)
                Throw("Could not load sRGB-to-spectrum upsampling model ('data/srgb.coeff')");
            atexit([]{ rgb2spec_free(model); });
        }
    }

    if (c == Vector3f(0.f))
        return Vector3f(0.f, 0.f, -math::Infinity);
    else if (c == Vector3f(1.f))
        return Vector3f(0.f, 0.f,  math::Infinity);

    float rgb[3] = { (float) c.r(), (float) c.g(), (float) c.b() };
    float out[3];
    rgb2spec_fetch(model, rgb, out);

    return Vector3f(out[0], out[1], out[2]);
}

template <typename Value> Color<Value, 3> srgb_model_eval_rgb(const Vector<Value, 3> &coeff) {
    ref<ContinuousSpectrum> d65 =
        PluginManager::instance()->create_object<ContinuousSpectrum>(
            Properties("d65"));
    auto expanded = d65->expand();
    if (expanded.size() == 1)
        d65 = (ContinuousSpectrum *) expanded[0].get();

    const size_t n_samples = ((MTS_CIE_SAMPLES - 1) * 3 + 1);

    Vector<Value, 3> accum = 0.f;
    Float h = (MTS_CIE_MAX - MTS_CIE_MIN) / (n_samples - 1);
    for (size_t i = 0; i < n_samples; ++i) {
        Float lambda = MTS_CIE_MIN + i * h;

        Float weight = 3.f / 8.f * h;
        if (i == 0 || i == n_samples - 1)
            ;
        else if ((i - 1) % 3 == 2)
            weight *= 2.f;
        else
            weight *= 3.f;

        accum += Vector<Value, 3>(weight * d65->eval(Spectrumf(lambda))[0] * cie1931_xyz(lambda)) *
                 srgb_model_eval(coeff, Value(lambda));
    }

    Matrix3f xyz_to_srgb(
         3.240479f, -1.537150f, -0.498535f,
        -0.969256f,  1.875991f,  0.041556f,
         0.055648f, -0.204043f,  1.057311f
    );

    return xyz_to_srgb * accum;
}

template MTS_EXPORT_RENDER Color<float, 3> srgb_model_eval_rgb(const Vector<float, 3> &coeff);

#if defined(MTS_ENABLE_AUTODIFF)
    template MTS_EXPORT_RENDER Color<FloatD, 3> srgb_model_eval_rgb(const Vector<FloatD, 3> &coeff);
#endif

NAMESPACE_END(mitsuba)
