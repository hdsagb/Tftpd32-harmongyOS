#include <cstring>
#include <cstdlib>
#include <new>
#include "napi/native_api.h"
extern "C" {
#include "pal_compat.h"     /* BOOL 等类型与 PAL 声明 */
}

#ifdef __cplusplus
extern "C" {
#endif
/* ---- 由 PAL 提供（settings_pal.c / service_main.c / console_pal.c）---- */
void   Pal_SetEmitter      (void (*fn)(const char *, const char *));
void   Pal_SetConfigPath   (const char *szPath);
int    TftpdServiceStart   (void);
void   TftpdServiceStop    (void);
BOOL   Tftpd32ReadSettings (void);
BOOL   Tftpd32SaveSettings (void);
BOOL   Pal_SetSettingInt   (const char *key, unsigned val);
BOOL   Pal_SetSettingStr   (const char *key, const char *val);
BOOL   Pal_GetSettingInt   (const char *key, unsigned *pVal);
BOOL   Pal_GetSettingStr   (const char *szKey, char *szOut, int nOut);
#ifdef __cplusplus
}
#endif

/* ---------- 线程安全发射器：worker 线程 → ArkTS 主线程 ---------- */
static napi_threadsafe_function g_tsf = nullptr;

struct EmitMsg { char type[16]; char json[1024]; };

static void EmitterTramp (const char *szType, const char *szJson)
{
    if (g_tsf == nullptr)  return;
    EmitMsg *m = new (std::nothrow) EmitMsg ();
    if (m == nullptr)  return;
    memset (m, 0, sizeof *m);
    strncpy (m->type, szType, sizeof m->type - 1);
    strncpy (m->json, szJson, sizeof m->json - 1);
    napi_call_threadsafe_function (g_tsf, m, napi_tsfn_nonblocking);
}

static void CallJs (napi_env env, napi_value jsCb, void * /*ctx*/, void *data)
{
    EmitMsg *m = (EmitMsg *) data;
    if (m != nullptr)
    {
        napi_value undef, argv[2];
        napi_get_undefined (env, & undef);
        napi_create_string_utf8 (env, m->type, NAPI_AUTO_LENGTH, & argv[0]);
        napi_create_string_utf8 (env, m->json, NAPI_AUTO_LENGTH, & argv[1]);
        napi_call_function (env, undef, jsCb, 2, argv, nullptr);
        delete m;
    }
}

/* ---------- 导出函数 ---------- */
static napi_value SetEventCb (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    if (g_tsf != nullptr)  napi_release_threadsafe_function (g_tsf, napi_tsfn_abort);
    napi_value name;
    napi_create_string_utf8 (env, "tftpdEvent", NAPI_AUTO_LENGTH, & name);
    napi_create_threadsafe_function (env, argv[0], nullptr, name, 0, 1, nullptr,
                                     nullptr, nullptr, CallJs, & g_tsf);
    Pal_SetEmitter (EmitterTramp);
    return nullptr;
}

static napi_value SetConfigPath (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  char buf[512];  size_t n = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], buf, sizeof buf, & n);
    Pal_SetConfigPath (buf);
    return nullptr;
}

static napi_value StartService (napi_env env, napi_callback_info /*info*/)
{
    napi_value result;
    Tftpd32ReadSettings ();
    napi_get_boolean (env, TftpdServiceStart () ? true : false, & result);
    return result;
}

static napi_value StopService (napi_env env, napi_callback_info /*info*/)
{
    napi_value result;
    TftpdServiceStop ();
    napi_get_boolean (env, true, & result);
    return result;
}

static napi_value SetSettingInt (napi_env env, napi_callback_info info)
{
    size_t argc = 2;  napi_value argv[2];  char key[64];  size_t n = 0;  double v = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n);
    napi_get_value_double (env, argv[1], & v);
    napi_value r;
    napi_get_boolean (env, Pal_SetSettingInt (key, (unsigned) v) ? true : false, & r);
    return r;
}

static napi_value SetSettingStr (napi_env env, napi_callback_info info)
{
    size_t argc = 2;  napi_value argv[2];  char key[64], val[512];  size_t n1 = 0, n2 = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n1);
    napi_get_value_string_utf8 (env, argv[1], val, sizeof val, & n2);
    napi_value r;
    napi_get_boolean (env, Pal_SetSettingStr (key, val) ? true : false, & r);
    return r;
}

static napi_value SaveSettings (napi_env env, napi_callback_info /*info*/)
{
    napi_value r;
    napi_get_boolean (env, Tftpd32SaveSettings () ? true : false, & r);
    return r;
}

/* GUI 启动时先读配置，再按键名取值回填控件 */
static napi_value ReadSettings (napi_env env, napi_callback_info /*info*/)
{
    napi_value r;
    napi_get_boolean (env, Tftpd32ReadSettings () ? true : false, & r);
    return r;
}

static napi_value GetSettingInt (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  char key[64];  size_t n = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n);
    unsigned v = 0;
    napi_value r;
    if (Pal_GetSettingInt (key, & v))  napi_create_int32 (env, (int) v, & r);
    else                               napi_get_undefined (env, & r);
    return r;
}

static napi_value GetSettingStr (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  char key[128];  size_t n = 0;
    char val[_MAX_PATH] = "";
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n);
    Pal_GetSettingStr (key, val, sizeof val);
    napi_value r;
    napi_create_string_utf8 (env, val, NAPI_AUTO_LENGTH, & r);
    return r;
}

static napi_value StartServiceMask (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  double dMask = 0;  napi_value r;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_double (env, argv[0], & dMask);
    napi_get_boolean (env, PalServiceStartMask ((unsigned) dMask) > 0 ? true : false, & r);
    return r;
}

static napi_value StopServiceMask (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  double dMask = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_double (env, argv[0], & dMask);
    PalServiceStopMask ((unsigned) dMask);
    return nullptr;
}

static napi_value StartTftpClient (napi_env env, napi_callback_info info)
{
    size_t argc = 6;  napi_value argv[6];  napi_value r;
    char host[256] = "", remote[256] = "", local[1024] = "";
    double dOpcode = 1, dPort = 69, dBlk = 0;  size_t n = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    if (argc < 5)  { napi_get_boolean (env, false, & r);  return r; }
    napi_get_value_double (env, argv[0], & dOpcode);
    napi_get_value_string_utf8 (env, argv[1], host, sizeof host, & n);
    napi_get_value_double (env, argv[2], & dPort);
    napi_get_value_string_utf8 (env, argv[3], remote, sizeof remote, & n);
    napi_get_value_string_utf8 (env, argv[4], local, sizeof local, & n);
    if (argc > 5)  napi_get_value_double (env, argv[5], & dBlk);
    napi_get_boolean (env, PalClientStart ((int) dOpcode, host, (int) dPort,
                                           remote, local, (int) dBlk) ? true : false, & r);
    return r;
}

static napi_value ComputeMd5 (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  napi_value r;
    char path[1024] = "", out[40] = "";  size_t n = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], path, sizeof path, & n);
    if (PalComputeMd5 (path, out, sizeof out))
        napi_create_string_utf8 (env, out, NAPI_AUTO_LENGTH, & r);
    else
        napi_get_undefined (env, & r);
    return r;
}

static napi_value ListDirectory (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  napi_value r;
    char path[1024] = "";  size_t n = 0;
    char *json = (char *) malloc (32768);
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], path, sizeof path, & n);
    if (json == nullptr)
    { napi_create_string_utf8 (env, "[]", 2, & r);  return r; }
    PalListDirectory (path, json, 32768);
    napi_create_string_utf8 (env, json, NAPI_AUTO_LENGTH, & r);
    free (json);
    return r;
}

EXTERN_C_START
static napi_value Init (napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] =
    {
        { "setEventCallback", nullptr, SetEventCb,    nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setConfigPath",    nullptr, SetConfigPath, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "startService",     nullptr, StartService,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "stopService",      nullptr, StopService,   nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setSettingInt",    nullptr, SetSettingInt, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setSettingStr",    nullptr, SetSettingStr, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "saveSettings",     nullptr, SaveSettings,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "readSettings",     nullptr, ReadSettings,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getSettingInt",    nullptr, GetSettingInt, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "startServiceMask", nullptr, StartServiceMask, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "stopServiceMask",  nullptr, StopServiceMask,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "startTftpClient",  nullptr, StartTftpClient,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "computeMd5",       nullptr, ComputeMd5,       nullptr, nullptr, nullptr, napi_default, nullptr },
        { "listDirectory",    nullptr, ListDirectory,    nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getSettingStr",    nullptr, GetSettingStr,    nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties (env, exports, sizeof (desc) / sizeof (desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module g_module =
{ .nm_version = 1, .nm_flags = 0, .nm_filename = nullptr, .nm_register_func = Init,
  .nm_modname = "tftpd32", .nm_priv = nullptr, .reserved = { 0 } };

extern "C" __attribute__((constructor)) void RegisterTftpd32Module (void)
{ napi_module_register (& g_module); }
