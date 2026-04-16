/* Copyright (c) 2024, Huawei Technologies Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <napi/native_api.h>
#include <native_window/external_window.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>

#include "ohos_triangle.h"

#define LOG_TAG "OHOS_NAPI"
#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, LOG_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static std::unique_ptr<OHOSTriangle> g_triangle;
static std::thread                   g_render_thread;
static std::mutex                    g_mutex;
static std::atomic<bool>             g_running{false};
static OH_NativeXComponent          *g_xcomponent = nullptr;

// ---------------------------------------------------------------------------
// Render loop (runs on a dedicated thread)
// ---------------------------------------------------------------------------

static void render_loop()
{
	while (g_running)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_triangle)
		{
			g_triangle->render();
		}
		// Throttle slightly to avoid saturating CPU
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

// ---------------------------------------------------------------------------
// XComponent surface lifecycle callbacks
//
// Following the official NdkVulkan sample pattern:
//   - OnSurfaceCreated receives OHNativeWindow* as void *window
//   - This is the correct place to initialize Vulkan
// ---------------------------------------------------------------------------

static void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window)
{
	LOGI("OnSurfaceCreated: window=%{public}p", window);

	OHNativeWindow *native_window = static_cast<OHNativeWindow *>(window);
	if (!native_window)
	{
		LOGE("OnSurfaceCreated: null window!");
		return;
	}

	// Get surface size from XComponent
	uint64_t width  = 720;
	uint64_t height = 1280;
	if (component)
	{
		OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
	}
	LOGI("OnSurfaceCreated: size=%{public}llu x %{public}llu",
	     (unsigned long long)width, (unsigned long long)height);

	// Stop any existing render loop
	if (g_running)
	{
		g_running = false;
		if (g_render_thread.joinable())
		{
			g_render_thread.join();
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_triangle = std::make_unique<OHOSTriangle>();
		if (!g_triangle->init(native_window,
		                      static_cast<uint32_t>(width),
		                      static_cast<uint32_t>(height)))
		{
			LOGE("OnSurfaceCreated: OHOSTriangle init failed!");
			g_triangle.reset();
			return;
		}
	}

	g_running       = true;
	g_render_thread = std::thread(render_loop);
	LOGI("OnSurfaceCreated: render thread started.");
}

static void OnSurfaceChangedCB(OH_NativeXComponent *component, void *window)
{
	uint64_t width = 0, height = 0;
	OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
	LOGI("OnSurfaceChanged: %{public}llu x %{public}llu",
	     (unsigned long long)width, (unsigned long long)height);

	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_triangle)
	{
		g_triangle->resize(static_cast<uint32_t>(width),
		                   static_cast<uint32_t>(height));
	}
}

static void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
	LOGI("OnSurfaceDestroyed: stopping renderer.");

	g_running = false;
	if (g_render_thread.joinable())
	{
		g_render_thread.join();
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_triangle)
	{
		g_triangle->cleanup();
		g_triangle.reset();
	}
}

static void DispatchTouchEventCB(OH_NativeXComponent *component, void *window)
{
	// Touch events not needed for the triangle demo
}

// ---------------------------------------------------------------------------
// NAPI module registration
//
// Following the official NdkVulkan sample pattern:
//   1. Use napi_get_named_property(OH_NATIVE_XCOMPONENT_OBJ) to get the
//      OH_NativeXComponent handle from the NAPI exports
//   2. Use napi_unwrap to extract the raw pointer
//   3. Call OH_NativeXComponent_RegisterCallback to register surface
//      lifecycle callbacks — this is how we receive OHNativeWindow*
// ---------------------------------------------------------------------------

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
	// Step 1: Get the XComponent object from NAPI exports
	napi_value exportInstance = nullptr;
	napi_status status = napi_get_named_property(env, exports,
	                                             OH_NATIVE_XCOMPONENT_OBJ,
	                                             &exportInstance);
	if (status != napi_ok)
	{
		LOGE("Init: failed to get OH_NATIVE_XCOMPONENT_OBJ from exports");
		return exports;
	}

	// Step 2: Unwrap to get OH_NativeXComponent*
	OH_NativeXComponent *nativeXComponent = nullptr;
	status = napi_unwrap(env, exportInstance,
	                     reinterpret_cast<void **>(&nativeXComponent));
	if (status != napi_ok || !nativeXComponent)
	{
		LOGE("Init: failed to unwrap OH_NativeXComponent");
		return exports;
	}

	g_xcomponent = nativeXComponent;
	LOGI("Init: got OH_NativeXComponent=%{public}p", nativeXComponent);

	// Step 3: Register surface lifecycle callbacks
	static OH_NativeXComponent_Callback callback;
	callback.OnSurfaceCreated     = OnSurfaceCreatedCB;
	callback.OnSurfaceChanged     = OnSurfaceChangedCB;
	callback.OnSurfaceDestroyed   = OnSurfaceDestroyedCB;
	callback.DispatchTouchEvent   = DispatchTouchEventCB;

	int32_t ret = OH_NativeXComponent_RegisterCallback(nativeXComponent, &callback);
	if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
	{
		LOGE("Init: OH_NativeXComponent_RegisterCallback failed, ret=%{public}d", ret);
	}

	return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version       = 1,
    .nm_flags         = 0,
    .nm_filename      = nullptr,
    .nm_register_func = Init,
    .nm_modname       = "entry",
    .nm_priv          = ((void *) 0),
    .reserved         = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
	napi_module_register(&demoModule);
}
