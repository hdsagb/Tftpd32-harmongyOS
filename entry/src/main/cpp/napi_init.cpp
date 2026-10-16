#include "napi/native_api.h"

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, napi_default, nullptr } };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]) - 1, desc);
    return exports;
}
EXTERN_C_END

static napi_module g_module = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "tftpd32",
    .nm_priv = nullptr,
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterTftpd32Module(void) {
    napi_module_register(&g_module);
}
