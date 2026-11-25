// SPDX-License-Identifier: MIT
//
// UFX: image effect plug-in interface.
//
// Copyright (c) 2026 The UFX authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// ---------------------------------------------------------------------------
// Overview
// ---------------------------------------------------------------------------
//
// A UFX plug-in is a native library that turns one GPU image into another,
// using the host's Vulkan device. The plug-in describes its own settings
// (pages, sliders, toggles, choice lists) and the host builds its controls
// from that description, saves the values, and passes them back on every
// render. The host has no knowledge of what any particular plug-in does.
//
// The interface is plain C. Nothing C++ (classes, std::string, exceptions,
// allocators) crosses it, so a plug-in should link its C++ runtime statically.
//
// Every struct starts with struct_size. Callers fill it with sizeof() of the
// struct they were compiled against, and readers ignore fields past that size,
// so either side can add fields at the end in later versions.
//
// ---------------------------------------------------------------------------
// Packaging and loading on Android
// ---------------------------------------------------------------------------
//
// A plug-in ships as its own APK. Its manifest declares a service that
// answers the "ufx.PLUGIN" action and names its entry class:
//
//   <application android:hasCode="true" ...>
//     <service android:name="org.example.myplugin.UfxPluginService"
//              android:exported="true">
//       <intent-filter>
//         <action android:name="ufx.PLUGIN" />
//       </intent-filter>
//       <meta-data android:name="ufx.ENTRY_CLASS"
//                  android:value="org.example.myplugin.UfxEntry" />
//     </service>
//   </application>
//
// The service only marks the APK as a plug-in. It is never started and its
// onBind returns null.
//
// ENTRY_CLASS names a Java/Kotlin class in the plug-in APK with a static
// method `long ufxEntryAddress()`. That method loads the plug-in's native
// library with System.loadLibrary() and returns the address of
// ufx_get_plugin() from it. Loading from the plug-in's own class keeps the
// library inside the plug-in's linker namespace, where its own lib folder is
// reachable.
//
// A host declares <queries><intent><action android:name="ufx.PLUGIN" />
// </intent></queries> in its manifest, finds plug-ins with
// PackageManager.queryIntentServices(), opens each one with
// createPackageContext(CONTEXT_INCLUDE_CODE | CONTEXT_IGNORE_SECURITY), calls
// ufxEntryAddress() and then ufx_get_plugin() through the returned address.
// Plug-ins are signed with their own keys. The host loads any installed
// plug-in that declares the service above, whoever signed it.
//
// ---------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------
//
// The host may create several instances of one plug-in (for example one for
// the editor preview and one for an export). Calls on one instance never
// overlap. Calls on different instances may run at the same time on
// different threads. Every vkQueueSubmit and every other use of the host
// queue must happen between lock_queue and unlock_queue.

#ifndef UFX_PLUGIN_H
#define UFX_PLUGIN_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define UFX_EXPORT __declspec(dllexport)
#else
#define UFX_EXPORT __attribute__((visibility("default")))
#endif

// Major version changes break compatibility. Minor versions only append
// fields and enum values.
#define UFX_API_VERSION_MAJOR 1u
#define UFX_API_VERSION_MINOR 0u
#define UFX_API_VERSION ((UFX_API_VERSION_MAJOR << 16) | UFX_API_VERSION_MINOR)
#define UFX_API_VERSION_GET_MAJOR(v) ((uint32_t)(v) >> 16)

typedef enum ufx_result {
  UFX_OK = 0,
  UFX_ERROR = 1,                // Generic failure. See the error message.
  UFX_ERROR_UNSUPPORTED = 2,    // The request uses something the plug-in cannot do.
  UFX_ERROR_OUT_OF_MEMORY = 3,  // Host may call release_transient and retry once.
  UFX_ERROR_DEVICE_LOST = 4     // The Vulkan device is gone. Destroy the instance.
} ufx_result;

typedef enum ufx_log_level {
  UFX_LOG_DEBUG = 0,
  UFX_LOG_INFO = 1,
  UFX_LOG_WARNING = 2,
  UFX_LOG_ERROR = 3
} ufx_log_level;

// ---------------------------------------------------------------------------
// What the host gives a plug-in instance
// ---------------------------------------------------------------------------

typedef struct ufx_host {
  uint32_t struct_size;
  uint32_t api_version;          // UFX_API_VERSION the host was built with.
  const char* host_name;         // Shown in logs and diagnostics.
  const char* host_version;

  // The host's Vulkan device. The plug-in must not destroy any of these.
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue queue;                 // Supports compute and transfer.
  uint32_t queue_family_index;
  uint32_t vulkan_api_version;   // Version the device was created with.
  PFN_vkGetInstanceProcAddr get_instance_proc_addr;

  // Extensions and features the host enabled on the device. The plug-in may
  // only use what is listed here.
  uint32_t enabled_device_extension_count;
  const char* const* enabled_device_extensions;
  const VkPhysicalDeviceFeatures2* enabled_features;  // pNext chain included.

  // Optional pipeline cache owned and saved by the host. May be VK_NULL_HANDLE.
  // When set, the plug-in passes it to vkCreate*Pipelines.
  VkPipelineCache pipeline_cache;

  // Serialises access to the queue, which the host and plug-ins share.
  void (*lock_queue)(void* host_user);
  void (*unlock_queue)(void* host_user);

  // Optional. May be NULL.
  void (*log)(void* host_user, ufx_log_level level, const char* message);

  // A folder only this plug-in uses, kept between launches, that the host
  // may clear at any time. Absolute path. May be NULL.
  const char* cache_dir;

  void* host_user;
} ufx_host;

// ---------------------------------------------------------------------------
// Settings description
// ---------------------------------------------------------------------------

typedef enum ufx_param_type {
  UFX_PARAM_BOOL = 1,    // Toggle. Value 0 or 1.
  UFX_PARAM_INT = 2,     // Whole-number slider.
  UFX_PARAM_FLOAT = 3,   // Slider.
  UFX_PARAM_CHOICE = 4   // Pick one from a list. Value is the option index.
} ufx_param_type;

// Where a page or setting applies. The host hides it, and does not apply it,
// for the other kind of media.
typedef enum ufx_media_flags {
  UFX_MEDIA_PHOTO = 1u << 0,
  UFX_MEDIA_VIDEO = 1u << 1,
  UFX_MEDIA_ALL = UFX_MEDIA_PHOTO | UFX_MEDIA_VIDEO
} ufx_media_flags;

typedef enum ufx_param_flags {
  UFX_PARAM_FLAG_NONE = 0,
  UFX_PARAM_FLAG_SHOW_SIGN = 1u << 0,   // Show "+" in front of positive values.
  UFX_PARAM_FLAG_ADVANCED = 1u << 1     // Host may tuck it behind an "advanced" switch.
} ufx_param_flags;

typedef struct ufx_choice_option {
  uint32_t struct_size;
  // Stable identifier stored in saved projects and presets. Never reused
  // for a different meaning, never renamed.
  const char* id;
  const char* label;
  // A stable number for this option, used by settings formats that store
  // choices as numbers. Hosts may save it instead of id, and must accept
  // either when reading. Never reused for a different option. -1 when there
  // is none.
  int32_t legacy_value;
} ufx_choice_option;

typedef struct ufx_page_desc {
  uint32_t struct_size;
  const char* id;             // Stable identifier.
  const char* label;          // Tab title.
  const char* help;           // Shown in the host's Help, not under controls. May be NULL.
  uint32_t media;             // ufx_media_flags.
} ufx_page_desc;

typedef struct ufx_param_desc {
  uint32_t struct_size;

  // Stable identifier used as the key in saved projects and presets. Never
  // reused for a different meaning, never renamed.
  const char* id;
  const char* label;
  const char* help;           // Shown in the host's Help, not under controls. May be NULL.
  const char* page_id;        // Page this setting appears on.
  const char* section;        // Optional heading inside the page. May be NULL.

  ufx_param_type type;
  uint32_t media;             // ufx_media_flags.
  uint32_t flags;             // ufx_param_flags.

  // Range and default for BOOL, INT and FLOAT. For CHOICE, default_value is
  // the default option index and the range fields are ignored.
  double min_value;
  double max_value;
  double default_value;
  double step;                // Slider snapping. 0 means continuous.
  int32_t display_decimals;   // Decimal places the host shows.
  const char* unit;           // For example "EV" or "%". May be NULL.

  // CHOICE only.
  uint32_t option_count;
  const ufx_choice_option* options;

  // Optional visibility rule: show this setting only while the setting
  // with id show_if_param has the value show_if_value (option index for a
  // choice, 0 or 1 for a toggle). NULL means always shown. Hidden settings
  // keep their values and are still passed to render.
  const char* show_if_param;
  double show_if_value;
} ufx_param_desc;

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

typedef enum ufx_render_purpose {
  UFX_PURPOSE_PREVIEW = 0,             // Editor preview at rest.
  UFX_PURPOSE_PREVIEW_INTERACTIVE = 1, // Preview while a slider is dragged. Speed over quality.
  UFX_PURPOSE_EXPORT = 2               // Final quality.
} ufx_render_purpose;

typedef enum ufx_media_kind {
  UFX_MEDIA_KIND_PHOTO = 0,
  UFX_MEDIA_KIND_VIDEO = 1
} ufx_media_kind;

// Colour encoding of an image handed across the interface. All encodings in
// version 1 are scene-linear.
typedef enum ufx_color_encoding {
  UFX_COLOR_LINEAR_REC709 = 0,
  UFX_COLOR_LINEAR_REC2020 = 1,
  UFX_COLOR_LINEAR_P3_D65 = 2
} ufx_color_encoding;

// What the image after the plug-in is going to be shown or saved as. The
// host applies the final display or file transform itself.
typedef enum ufx_output_target {
  UFX_OUTPUT_SDR = 0,
  UFX_OUTPUT_HDR_PQ = 1,
  UFX_OUTPUT_HDR_HLG = 2
} ufx_output_target;

typedef struct ufx_render_request {
  uint32_t struct_size;

  // Both images: VK_FORMAT_R16G16B16A16_SFLOAT, width x height, one mip
  // level, one layer, created with STORAGE, SAMPLED, TRANSFER_SRC and
  // TRANSFER_DST usage, exclusive to host.queue_family_index. They are in
  // VK_IMAGE_LAYOUT_GENERAL when render is called and must be in
  // VK_IMAGE_LAYOUT_GENERAL when it returns. source and destination are
  // different images. The plug-in must not change source.
  VkImage source;
  VkImage destination;
  uint32_t width;
  uint32_t height;
  ufx_color_encoding source_encoding;
  ufx_color_encoding destination_encoding;

  ufx_render_purpose purpose;
  ufx_media_kind media;
  ufx_output_target output_target;
  // Used when output_target is HDR. 0 means the plug-in uses its own settings.
  float hdr_reference_white_nits;
  float hdr_peak_nits;

  // Position in the clip in seconds, for effects that change over time
  // such as grain. 0 for photos.
  double time_seconds;

  // One value per entry of ufx_plugin.params, in the same order. The host
  // fills missing saved values with defaults before calling. Saved values may
  // lie outside the declared range, so the plug-in must accept any finite value.
  uint32_t value_count;
  const double* values;
} ufx_render_request;

// Opaque per-instance state owned by the plug-in.
typedef struct ufx_instance ufx_instance;

// ---------------------------------------------------------------------------
// The plug-in
// ---------------------------------------------------------------------------

typedef struct ufx_plugin_info {
  uint32_t struct_size;
  const char* id;            // Reverse-domain, for example "org.example.film". Never changes.
  const char* name;          // Shown in the host's UI.
  const char* version;       // Plug-in version, for display and saved projects.
  const char* copyright;
  const char* license;       // SPDX identifier, for example "GPL-3.0-or-later".
  const char* source_url;    // Where its source code can be obtained. May be NULL.
} ufx_plugin_info;

typedef struct ufx_plugin {
  uint32_t struct_size;
  uint32_t api_version;      // UFX_API_VERSION the plug-in was built with.
  const ufx_plugin_info* info;

  uint32_t page_count;
  const ufx_page_desc* pages;
  uint32_t param_count;
  const ufx_param_desc* params;

  // All error strings stay valid until the next call on the same instance,
  // or until the next create call when there is no instance.

  ufx_result (*create)(const ufx_host* host, ufx_instance** out_instance,
                       const char** out_error);

  // Waits for the plug-in's GPU work to finish and frees everything. The
  // host calls this before it destroys its Vulkan device.
  void (*destroy)(ufx_instance* instance);

  // Records, submits and waits for the work. On return with UFX_OK the
  // destination holds the result and no plug-in work on the queue is
  // still running.
  ufx_result (*render)(ufx_instance* instance, const ufx_render_request* request,
                       const char** out_error);

  // Frees caches and scratch memory that can be rebuilt. The next render
  // may be slower. May be NULL.
  void (*release_transient)(ufx_instance* instance);

  // Timings and other details from the last render as JSON, for the host's
  // logs. The string stays valid until the next call on the instance.
  // May be NULL.
  const char* (*diagnostics_json)(ufx_instance* instance);
} ufx_plugin;

// The one symbol a plug-in library exports. Returns NULL when the plug-in
// cannot work with a host of this major version. The returned struct and
// everything it points to stay valid while the library is loaded.
UFX_EXPORT const ufx_plugin* ufx_get_plugin(uint32_t host_api_version);

typedef const ufx_plugin* (*PFN_ufx_get_plugin)(uint32_t host_api_version);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UFX_PLUGIN_H
