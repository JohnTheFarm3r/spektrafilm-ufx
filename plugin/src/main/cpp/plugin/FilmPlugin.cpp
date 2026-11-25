// SPDX-License-Identifier: GPL-3.0-or-later
//
// UFX plug-in wrapper around the spektrafilm Vulkan renderer.

#include <ufx/ufx_plugin.h>

#include "SpektraVulkanRenderer.h"

#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr const char* kPluginVersion = "1.0.0";

// Photo exports at or above this pixel count render in tiles to bound GPU memory.
constexpr uint64_t kTiledPhotoExportMinPixels = 16000001ull;

#define OPTION(id, label, legacy) {sizeof(ufx_choice_option), id, label, legacy}

const ufx_choice_option kFilmStocks[] = {
    OPTION("kodak_ektar_100", "Kodak Ektar 100", 0),
    OPTION("kodak_portra_160", "Kodak Portra 160", 1),
    OPTION("kodak_portra_400", "Kodak Portra 400", 2),
    OPTION("kodak_portra_800", "Kodak Portra 800", 3),
    OPTION("kodak_portra_800_push1", "Kodak Portra 800 +1", 4),
    OPTION("kodak_portra_800_push2", "Kodak Portra 800 +2", 5),
    OPTION("kodak_gold_200", "Kodak Gold 200", 6),
    OPTION("kodak_ultramax_400", "Kodak Ultramax 400", 7),
    OPTION("kodak_vision3_50d", "Kodak Vision3 50D", 8),
    OPTION("kodak_vision3_250d", "Kodak Vision3 250D", 9),
    OPTION("kodak_verita_200d", "Kodak Verita 200D", 10),
    OPTION("kodak_vision3_200t", "Kodak Vision3 200T", 11),
    OPTION("kodak_vision3_500t", "Kodak Vision3 500T", 12),
    OPTION("fujifilm_pro_400h", "Fujifilm Pro 400H", 13),
    OPTION("fujifilm_c200", "Fujifilm C200", 14),
    OPTION("fujifilm_xtra_400", "Fujifilm X-TRA 400", 15),
    OPTION("kodak_ektachrome_100", "Kodak Ektachrome 100", 16),
    OPTION("kodak_kodachrome_64", "Kodak Kodachrome 64", 17),
    OPTION("fujifilm_velvia_100", "Fujifilm Velvia 100", 18),
    OPTION("fujifilm_provia_100f", "Fujifilm Provia 100F", 19),
};

const ufx_choice_option kPaperStocks[] = {
    OPTION("kodak_endura_premier", "Kodak Endura Premier", 0),
    OPTION("kodak_ultra_endura", "Kodak Ultra Endura", 1),
    OPTION("kodak_ektacolor_edge", "Kodak Ektacolor Edge", 2),
    OPTION("kodak_supra_endura", "Kodak Supra Endura", 3),
    OPTION("kodak_portra_endura", "Kodak Portra Endura", 4),
    OPTION("fujifilm_crystal_archive_typeii", "Fujifilm Crystal Archive Type II", 5),
    OPTION("kodak_2383", "Kodak 2383", 6),
    OPTION("kodak_2393", "Kodak 2393", 7),
};

const ufx_choice_option kProcessModes[] = {
    OPTION("print_simulation", "Print simulation", 0),
    OPTION("scan_negative", "Scan negative", 1),
    OPTION("process_negative", "Process negative", 2),
};

const ufx_choice_option kRenderOutputs[] = {
    OPTION("final_preview", "Final preview", 0),
    OPTION("film_density_cmy", "Film density CMY", 1),
    OPTION("film_density_cmy_grain", "Film density CMY + grain", 2),
    OPTION("film_log_raw", "Film log raw", 3),
    OPTION("print_log_raw", "Print log raw", 4),
    OPTION("print_density_cmy", "Print density CMY", 5),
};

const ufx_choice_option kRgbToRaw[] = {
    OPTION("hanatos_2025", "Hanatos 2025", 0),
    OPTION("mallett_2019", "Mallett 2019", 1),
    OPTION("hanatos_2026", "Hanatos 2026", 2),
};

const ufx_choice_option kColorSpaces[] = {
    OPTION("arri_logc4", "ARRI LogC4", 0),
    OPTION("arri_logc3_ei800", "ARRI LogC3 EI800", 1),
    OPTION("bmdfilm_widegamut_gen5", "BMDFilm WideGamut Gen5", 2),
    OPTION("davinci_intermediate_widegamut", "DaVinci Intermediate WideGamut", 3),
    OPTION("red_log3g10_redwidegamutrgb", "RED Log3G10 REDWideGamutRGB", 4),
    OPTION("sony_slog3_sgamut3", "Sony S-Log3 S-Gamut3", 5),
    OPTION("sony_slog3_sgamut3_cine", "Sony S-Log3 S-Gamut3.Cine", 6),
    OPTION("canon_log2_cinemagamut_d55", "Canon Log2 CinemaGamut D55", 7),
    OPTION("canon_log3_cinemagamut_d55", "Canon Log3 CinemaGamut D55", 8),
    OPTION("panasonic_vlog_vgamut", "Panasonic V-Log V-Gamut", 9),
    OPTION("aces2065_1", "ACES2065-1", 10),
    OPTION("acescg", "ACEScg", 11),
    OPTION("acescct", "ACEScct", 12),
    OPTION("acescc", "ACEScc", 13),
    OPTION("linear_rec2020", "Linear Rec.2020", 14),
    OPTION("linear_rec709", "Linear Rec.709", 15),
    OPTION("linear_p3_d65", "Linear P3-D65", 16),
    OPTION("srgb", "sRGB", 17),
    OPTION("display_p3", "Display P3", 18),
    OPTION("prophoto_rgb", "ProPhoto RGB", 19),
    OPTION("adobe_rgb_1998", "Adobe RGB (1998)", 20),
    OPTION("dci_p3", "DCI-P3", 21),
    OPTION("p3_d65_gamma22", "P3-D65 Gamma 2.2", 22),
    OPTION("p3_d65_gamma26", "P3-D65 Gamma 2.6", 23),
    OPTION("rec709_gamma22", "Rec.709 Gamma 2.2", 24),
    OPTION("rec709_gamma24", "Rec.709 Gamma 2.4", 25),
};

const ufx_choice_option kOutputRoles[] = {
    OPTION("display_sdr", "Display Out SDR", 0),
    OPTION("display_hdr", "Display Out HDR", 1),
    OPTION("scene_handoff", "Scene Handoff", 2),
};

const ufx_choice_option kHdrPresets[] = {
    OPTION("pq_1000", "PQ 1000", 0),
    OPTION("pq_4000", "PQ 4000", 1),
    OPTION("hlg_1000", "HLG 1000", 2),
    OPTION("custom", "Custom", 3),
};

const ufx_choice_option kHdrTransfers[] = {
    OPTION("pq", "Rec.2100 ST2084 (PQ)", 0),
    OPTION("hlg", "Rec.2100 HLG", 1),
};

const ufx_choice_option kHdrToneMappings[] = {
    OPTION("soft_rolloff", "Soft Rolloff", 0),
    OPTION("hard_clip", "Hard Clip", 1),
};

const ufx_choice_option kAutoExposureMethods[] = {
    OPTION("center_weighted", "Center weighted", 0),
    OPTION("median", "Median", 1),
};

const ufx_choice_option kPushPullModes[] = {
    OPTION("standard", "Standard", 0),
    OPTION("experimental", "Experimental", 1),
};

const ufx_choice_option kPrintTimingModes[] = {
    OPTION("filtered_enlarger", "Filtered enlarger", 0),
    OPTION("apd_printer_density", "APD printer density", 1),
};

const ufx_choice_option kGrainModels[] = {
    OPTION("preview", "Preview", 0),
    OPTION("production", "Production", 1),
    OPTION("grain_synthesis", "Grain Synthesis", 2),
};

const ufx_choice_option kFilmFormats[] = {
    OPTION("standard_8", "Standard 8", 0),
    OPTION("super_8", "Super 8", 1),
    OPTION("standard_16", "Standard 16", 2),
    OPTION("super_16", "Super 16", 3),
    OPTION("standard_35", "Standard 35", 4),
    OPTION("super_35", "Super 35", 5),
    OPTION("standard_65", "Standard 65", 6),
    OPTION("imax_70", "IMAX 70", 7),
};

const ufx_choice_option kDiffusionFamilies[] = {
    OPTION("glimmerglass", "Glimmerglass", 0),
    OPTION("black_pro_mist", "Black Pro-Mist", 1),
    OPTION("pro_mist", "Pro-Mist", 2),
    OPTION("cinebloom", "CineBloom", 3),
};

#undef OPTION

#define PAGE(id, label, media) {sizeof(ufx_page_desc), id, label, nullptr, media}

// Grain is shown for photos only. Video renders always run without it.
const ufx_page_desc kPages[] = {
    PAGE("film", "Film", UFX_MEDIA_ALL),
    PAGE("print", "Print", UFX_MEDIA_ALL),
    PAGE("grain", "Grain", UFX_MEDIA_PHOTO),
    PAGE("look", "Look", UFX_MEDIA_ALL),
    PAGE("optics", "Optics", UFX_MEDIA_ALL),
    PAGE("output", "Output", UFX_MEDIA_ALL),
    PAGE("advanced", "Advanced", UFX_MEDIA_ALL),
};

#undef PAGE

struct VisibilityRule {
  const char* param;
  const char* dependsOn;
  double value;
};

const VisibilityRule kVisibilityRules[] = {
    {"scanNegativeInvert", "process", 1.0},
    {"colorAdaptationInputCompression", "colorAdaptation", 1.0},
    {"colorAdaptationCurveSmoothing", "colorAdaptation", 1.0},
    {"colorAdaptationOutputLightnessCompression", "colorAdaptation", 1.0},
    {"colorAdaptationOutputChromaCompression", "colorAdaptation", 1.0},
};

uint32_t pageMedia(const char* pageId) {
  for (const ufx_page_desc& page : kPages) {
    if (std::strcmp(page.id, pageId) == 0) {
      return page.media;
    }
  }
  return UFX_MEDIA_ALL;
}

ufx_param_desc baseParam(const char* id, const char* page, const char* section,
                         const char* label, ufx_param_type type) {
  ufx_param_desc d{};
  d.struct_size = sizeof(ufx_param_desc);
  d.id = id;
  d.label = label;
  d.page_id = page;
  d.section = section;
  d.type = type;
  d.media = pageMedia(page);
  for (const VisibilityRule& rule : kVisibilityRules) {
    if (std::strcmp(rule.param, id) == 0) {
      d.show_if_param = rule.dependsOn;
      d.show_if_value = rule.value;
    }
  }
  return d;
}

ufx_param_desc floatParam(const char* id, const char* page, const char* section,
                          const char* label, double minValue, double maxValue,
                          double defaultValue, int32_t decimals, uint32_t flags) {
  ufx_param_desc d = baseParam(id, page, section, label, UFX_PARAM_FLOAT);
  d.min_value = minValue;
  d.max_value = maxValue;
  d.default_value = defaultValue;
  d.display_decimals = decimals;
  d.flags = flags;
  return d;
}

ufx_param_desc intParam(const char* id, const char* page, const char* section,
                        const char* label, double minValue, double maxValue,
                        double defaultValue) {
  ufx_param_desc d = baseParam(id, page, section, label, UFX_PARAM_INT);
  d.min_value = minValue;
  d.max_value = maxValue;
  d.default_value = defaultValue;
  d.step = 1.0;
  return d;
}

ufx_param_desc boolParam(const char* id, const char* page, const char* section,
                         const char* label, double defaultValue) {
  ufx_param_desc d = baseParam(id, page, section, label, UFX_PARAM_BOOL);
  d.min_value = 0.0;
  d.max_value = 1.0;
  d.default_value = defaultValue;
  d.step = 1.0;
  return d;
}

template <size_t N>
ufx_param_desc choiceParam(const char* id, const char* page, const char* section,
                           const char* label, double defaultIndex,
                           const ufx_choice_option (&options)[N]) {
  ufx_param_desc d = baseParam(id, page, section, label, UFX_PARAM_CHOICE);
  d.default_value = defaultIndex;
  d.option_count = static_cast<uint32_t>(N);
  d.options = options;
  return d;
}

const std::vector<ufx_param_desc>& paramDescriptors() {
  static const std::vector<ufx_param_desc> params = [] {
    std::vector<ufx_param_desc> list;
#define FLOAT(id, page, section, label, lo, hi, def, decimals, flags) \
    list.push_back(floatParam(#id, page, section, label, lo, hi, def, decimals, flags));
#define INT(id, page, section, label, lo, hi, def) \
    list.push_back(intParam(#id, page, section, label, lo, hi, def));
#define BOOL(id, page, section, label, def) \
    list.push_back(boolParam(#id, page, section, label, def));
#define CHOICE(id, page, section, label, def, options) \
    list.push_back(choiceParam(#id, page, section, label, def, options));
#include "FilmParams.inc"
#undef FLOAT
#undef INT
#undef BOOL
#undef CHOICE
    return list;
  }();
  return params;
}

template <typename T>
void assignValue(T& field, double value) {
  if constexpr (std::is_same_v<T, bool>) {
    field = value != 0.0;
  } else if constexpr (std::is_enum_v<T>) {
    field = static_cast<T>(static_cast<std::underlying_type_t<T>>(std::lround(value)));
  } else if constexpr (std::is_integral_v<T>) {
    field = static_cast<T>(std::llround(value));
  } else {
    field = static_cast<T>(value);
  }
}

spektrafilm::RenderParams paramsFromValues(const double* values) {
  spektrafilm::RenderParams p{};
  size_t index = 0;
#define FLOAT(id, ...) assignValue(p.id, values[index++]);
#define INT(id, ...) assignValue(p.id, values[index++]);
#define BOOL(id, ...) assignValue(p.id, values[index++]);
#define CHOICE(id, ...) assignValue(p.id, values[index++]);
#include "FilmParams.inc"
#undef FLOAT
#undef INT
#undef BOOL
#undef CHOICE
  return p;
}

const ufx_plugin_info kInfo = {
    sizeof(ufx_plugin_info),
    "filmsim.ufx",
    "Film Simulation",
    kPluginVersion,
    "Film engine from spektrafilm OFX, Copyright (C) 2026 Aedan Diez. "
    "Film model and profiles from spektrafilm by Andrea Volpato.",
    "GPL-3.0-or-later AND CC-BY-SA-4.0",
    nullptr,
};

}  // namespace

struct ufx_instance {
  std::unique_ptr<spektrafilm::VulkanRenderer> renderer;
  std::string error;
  std::string diagnostics;
};

namespace {

ufx_result failWith(ufx_instance* instance, const std::string& message, const char** outError,
                    ufx_result result) {
  instance->error = message;
  if (outError) {
    *outError = instance->error.c_str();
  }
  return result;
}

bool isDeviceLost(const std::string& message) {
  return message.find("VK_ERROR_DEVICE_LOST") != std::string::npos ||
         message.find("device lost") != std::string::npos ||
         message.find("must be recreated") != std::string::npos;
}

std::string createError;

ufx_result pluginCreate(const ufx_host* host, ufx_instance** outInstance, const char** outError) {
  if (outError) {
    *outError = nullptr;
  }
  if (!outInstance) {
    return UFX_ERROR;
  }
  *outInstance = nullptr;
  if (!host || host->struct_size < sizeof(ufx_host) ||
      UFX_API_VERSION_GET_MAJOR(host->api_version) != UFX_API_VERSION_MAJOR) {
    createError = "Unsupported host interface version.";
    if (outError) *outError = createError.c_str();
    return UFX_ERROR_UNSUPPORTED;
  }

  spektrafilm::ExternalVulkanContext context{};
  context.instance = host->instance;
  context.physicalDevice = host->physical_device;
  context.device = host->device;
  context.computeQueue = host->queue;
  context.computeQueueFamily = host->queue_family_index;
  context.lockQueue = host->lock_queue;
  context.unlockQueue = host->unlock_queue;
  context.queueLockUser = host->host_user;
  if (!context.isValid()) {
    createError = "The host Vulkan context is incomplete.";
    if (outError) *outError = createError.c_str();
    return UFX_ERROR;
  }

  auto instance = std::make_unique<ufx_instance>();
  instance->renderer = std::make_unique<spektrafilm::VulkanRenderer>(context);
  if (!instance->renderer->isAvailable()) {
    createError = instance->renderer->lastError();
    if (createError.empty()) {
      createError = "The film renderer could not start on the host Vulkan device.";
    }
    if (outError) *outError = createError.c_str();
    return UFX_ERROR;
  }
  *outInstance = instance.release();
  return UFX_OK;
}

void pluginDestroy(ufx_instance* instance) {
  delete instance;
}

ufx_result pluginRender(ufx_instance* instance, const ufx_render_request* request,
                        const char** outError) {
  if (outError) {
    *outError = nullptr;
  }
  if (!instance) {
    return UFX_ERROR;
  }
  instance->error.clear();
  const std::vector<ufx_param_desc>& descriptors = paramDescriptors();
  if (!request || request->struct_size < sizeof(ufx_render_request) ||
      request->source == VK_NULL_HANDLE || request->destination == VK_NULL_HANDLE ||
      request->source == request->destination || request->width == 0 || request->height == 0 ||
      !request->values || request->value_count != descriptors.size()) {
    return failWith(instance, "Invalid render request.", outError, UFX_ERROR);
  }

  spektrafilm::RenderParams params = paramsFromValues(request->values);

  const bool video = request->media == UFX_MEDIA_KIND_VIDEO;
  if (video) {
    params.grainEnabled = false;
  }

  if (request->output_target == UFX_OUTPUT_HDR_PQ || request->output_target == UFX_OUTPUT_HDR_HLG) {
    params.outputRole = spektrafilm::OutputRole::DisplayHdr;
    params.hdrTransfer = request->output_target == UFX_OUTPUT_HDR_PQ
                             ? spektrafilm::HdrTransfer::Pq
                             : spektrafilm::HdrTransfer::Hlg;
    if (request->hdr_reference_white_nits > 0.0f) {
      params.hdrReferenceWhiteNits = request->hdr_reference_white_nits;
    }
    if (request->hdr_peak_nits > 0.0f) {
      params.hdrPeakNits = request->hdr_peak_nits;
    }
  }

  // Grain synthesis reads density outside tile borders, so it needs the whole frame.
  const bool grainSynthesis =
      params.grainEnabled && params.grainModel == spektrafilm::GrainModel::GrainSynthesis;
  const uint64_t pixels = static_cast<uint64_t>(request->width) * request->height;
  params.gpuRenderTiling = (request->purpose == UFX_PURPOSE_EXPORT && !video && !grainSynthesis &&
                            pixels >= kTiledPhotoExportMinPixels)
                               ? spektrafilm::GpuRenderTilingMode::Tiled
                               : spektrafilm::GpuRenderTilingMode::LegacyFullFrame;

  const bool videoExport = video && request->purpose == UFX_PURPOSE_EXPORT;
  const bool ok = instance->renderer->renderVulkanImages(
      request->source, request->destination, request->width, request->height, params,
      request->time_seconds, videoExport);
  if (!ok) {
    const std::string& message = instance->renderer->lastError();
    return failWith(instance, message.empty() ? "Film render failed." : message, outError,
                    isDeviceLost(message) ? UFX_ERROR_DEVICE_LOST : UFX_ERROR);
  }
  return UFX_OK;
}

void pluginReleaseTransient(ufx_instance* instance) {
  if (instance && instance->renderer) {
    instance->renderer->releaseTransientResources();
  }
}

const char* pluginDiagnosticsJson(ufx_instance* instance) {
  if (!instance || !instance->renderer) {
    return nullptr;
  }
  const spektrafilm::VulkanRenderDiagnostics& d = instance->renderer->lastDiagnostics();
  std::ostringstream json;
  json << "{\"cpuSetupMs\":" << d.cpuSetupMs
       << ",\"commandEncodingMs\":" << d.commandEncodingMs
       << ",\"commandBufferMs\":" << d.commandBufferMs
       << ",\"gpuCommandBufferMs\":" << d.gpuCommandBufferMs
       << ",\"passCount\":" << d.passCount
       << ",\"tiledRendering\":" << (d.tiledRendering ? "true" : "false")
       << ",\"tileCount\":" << d.tileCount
       << ",\"scratchAllocationBytes\":" << d.scratchAllocationBytes
       << ",\"staticAllocationBytes\":" << d.staticAllocationBytes
       << ",\"transientCachedBytes\":" << d.transientCachedBytes << "}";
  instance->diagnostics = json.str();
  return instance->diagnostics.c_str();
}

}  // namespace

extern "C" UFX_EXPORT const ufx_plugin* ufx_get_plugin(uint32_t host_api_version) {
  if (UFX_API_VERSION_GET_MAJOR(host_api_version) != UFX_API_VERSION_MAJOR) {
    return nullptr;
  }
  static const ufx_plugin plugin = [] {
    const std::vector<ufx_param_desc>& params = paramDescriptors();
    ufx_plugin p{};
    p.struct_size = sizeof(ufx_plugin);
    p.api_version = UFX_API_VERSION;
    p.info = &kInfo;
    p.page_count = static_cast<uint32_t>(sizeof(kPages) / sizeof(kPages[0]));
    p.pages = kPages;
    p.param_count = static_cast<uint32_t>(params.size());
    p.params = params.data();
    p.create = pluginCreate;
    p.destroy = pluginDestroy;
    p.render = pluginRender;
    p.release_transient = pluginReleaseTransient;
    p.diagnostics_json = pluginDiagnosticsJson;
    return p;
  }();
  return &plugin;
}

extern "C" JNIEXPORT jlong JNICALL Java_spektrafilm_ufx_UfxEntry_nativeEntryAddress(JNIEnv*, jclass) {
  return static_cast<jlong>(reinterpret_cast<intptr_t>(&ufx_get_plugin));
}
