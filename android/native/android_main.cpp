/*
 * PeepeeBox for Android - the funworld Photo Play cabinet (PeepeeBox) on Android.
 *
 *          Android front end, native half: what the desktop build's Qt main()
 *          and renderer do, behind a JNI interface for the Kotlin app
 *          (app/, class io.github.xeon3d.peepeebox.Native).
 *
 *          - nativeInit(dir): pc_init with -P <dir>/ -R <dir>/roms/ -L <dir>/86box.log
 *            (the app's external files directory: 86box.cfg, nvr/, images).
 *          - nativeStart / nativeStop: the machine and its emulation thread.
 *          - nativeSetSurface: where frames go.  A frame is copied into the
 *            surface's buffer at the guest's own size (ANativeWindow geometry);
 *            the compositor scales it to the view, which the app keeps 4:3.
 *          - nativeTouch: a touch on the picture, as the touchscreen's absolute
 *            position and button.
 *          - nativePulse: the funworld I/O card's money lines and Setup button.
 *
 * Authors: PeepeeBox contributors
 */
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/funworld_io.h>
#include <86box/mouse.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h> /* after thread.h and timer.h */
#include <86box/nvr.h>
#include <86box/photoplay.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>

extern bool fast_forward; /* sound.h declares it only when bool is a C macro */
extern int  nvr_dosave;
extern int  hard_reset_pending;
extern int  hdd_manager;
extern int  hdd_manager_asked;
extern void ack_pause(void);
extern volatile int android_speed_percent; /* android_ui.c */
}

#define LOG_TAG "PeepeeBox"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool cpu_thread_running = false;

static std::thread     emu_thread;
static std::thread     onesec_thread;
static std::atomic_bool onesec_run { false };

/* The surface, guarded: the app replaces or drops it (rotation, background)
   while the emulation thread blits. */
static std::mutex      surface_mtx;
static ANativeWindow  *surface     = nullptr;
static int             surface_w   = 0;
static int             surface_h   = 0;

/* 86Box's startblit/endblit: kept for the core, which brackets its own buffer
   changes with them. */
static std::recursive_mutex blit_mtx;
extern "C" void startblit(void) { blit_mtx.lock(); }
extern "C" void endblit(void) { blit_mtx.unlock(); }

/* ---- frames ---------------------------------------------------------------- */

/* A finished frame: the visible part of monitor 0's buffer, x/y/w/h, as XRGB
   words (B, G, R, X in memory).  The surface is RGBX (R, G, B, X): swap R and B. */
static void
android_blit(int x, int y, int w, int h, int monitor_index)
{
    if ((monitor_index != 0) || (w <= 0) || (h <= 0) || (monitors[0].target_buffer == NULL)) {
        video_blit_complete_monitor(monitor_index);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(surface_mtx);
        if (surface) {
            if ((w != surface_w) || (h != surface_h)) {
                ANativeWindow_setBuffersGeometry(surface, w, h, WINDOW_FORMAT_RGBX_8888);
                surface_w = w;
                surface_h = h;
            }
            ANativeWindow_Buffer buf;
            if (ANativeWindow_lock(surface, &buf, nullptr) == 0) {
                const int rows = (h < buf.height) ? h : buf.height;
                const int cols = (w < buf.width) ? w : buf.width;
                for (int r = 0; r < rows; r++) {
                    const uint32_t *src = &(monitors[0].target_buffer->line[y + r][x]);
                    uint32_t       *dst = (uint32_t *) buf.bits + ((size_t) r * buf.stride);
                    for (int c = 0; c < cols; c++) {
                        const uint32_t p = src[c];
                        dst[c]           = 0xff000000 | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
                    }
                }
                ANativeWindow_unlockAndPost(surface);
            }
        }
    }
    video_blit_complete_monitor(monitor_index);
}

/* ---- the emulation thread (qt_main.cpp's main_thread_fn) ------------------- */

static void
emu_thread_fn()
{
    using clock = std::chrono::steady_clock;
    plat_set_thread_name(nullptr, "main_thread");
    is_cpu_thread = 1;

    const int64_t quantum_ns  = force_10ms ? 10000000LL : 1000000LL;
    const int64_t max_debt_ns = 50000000LL;
    int64_t       debt_ns     = 0;
    int           frames      = 0;
    auto          old_t       = clock::now();

    while (!is_quit && cpu_thread_run) {
        const auto now = clock::now();
        debt_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(now - old_t).count();
        old_t = now;
        if (debt_ns > max_debt_ns)
            debt_ns = max_debt_ns;

        if (((debt_ns >= quantum_ns) || fast_forward) && !dopause) {
            pc_run();

            /* Every 2 emulated seconds the machine status is saved. */
            if ((++frames >= (force_10ms ? 200 : 2000)) && nvr_dosave) {
                nvr_save();
                nvr_dosave = 0;
                frames     = 0;
            }

            if (!fast_forward && (debt_ns >= quantum_ns))
                debt_ns -= quantum_ns;
            else
                debt_ns = 0;
        } else {
            if (hard_reset_pending) {
                hard_reset_pending = 0;
                pc_reset_hard_close();
                pc_reset_hard_init();
            }
            if (dopause)
                ack_pause();
            plat_delay_ms(1);
        }
    }

    cpu_thread_running = false;
    is_quit            = 1;
}

/* ---- JNI --------------------------------------------------------------------- */

#define JNI(name) Java_io_github_xeon3d_peepeebox_Native_##name

static std::string
jstr(JNIEnv *env, jstring s)
{
    if (!s)
        return std::string();
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    env->ReleaseStringUTFChars(s, c);
    return r;
}

static jobjectArray
strings(JNIEnv *env, const std::vector<std::string> &v)
{
    jobjectArray out = env->NewObjectArray((jsize) v.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < v.size(); i++)
        env->SetObjectArrayElement(out, (jsize) i, env->NewStringUTF(v[i].c_str()));
    return out;
}

/* A setting changed that the cabinet only picks up at power-on (a part fitted or
   taken out, the disk swapped): save, and restart the running machine on it, as
   the desktop's dialogs do. */
static void
restart_cabinet(void)
{
    config_save();
    if (cpu_thread_running) {
        const int was = dopause;
        plat_pause(1);
        config_changed = 2;
        pc_reset_hard();
        plat_pause(was);
    }
}

extern "C" JNIEXPORT jint JNICALL
JNI(nativeInit)(JNIEnv *env, jclass, jstring jdir)
{
    std::string dir = jstr(env, jdir);
    if (dir.empty() || (dir.back() != '/'))
        dir += '/';

    static std::string usr, roms, log;
    usr  = dir;
    roms = dir + "roms/";
    log  = dir + "86box.log";
    char *argv[] = { (char *) "PeepeeBox", (char *) "-P", (char *) usr.c_str(), (char *) "-R", (char *) roms.c_str(),
                     (char *) "-L", (char *) log.c_str(), nullptr };

    if (!pc_init(7, argv)) {
        LOGE("pc_init failed");
        return 1;
    }
    if (!pc_init_roms()) {
        LOGE("no ROMs in %s", roms.c_str());
        return 2;
    }
    pc_init_modules();

    /* The app's Machine Manager is the only way an image reaches the drive: the
       desktop's "run the HardDisk.img next to the executable" has no meaning in an
       APK, so the manager is always on and its first-start question never put. */
    hdd_manager       = 1;
    hdd_manager_asked = 1;

    /* The touchscreen is the cabinet's only pointer: absolute input from the start
       (mouse_reset restores this at every hard reset).  On the desktop the Qt main
       window chooses it; without it, presses would go to the relative mouse. */
    mouse_input_mode_initial = 1;
    mouse_input_mode         = 1;
    video_setblit(android_blit);
    LOGI("initialised in %s", usr.c_str());
    return 0;
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeStart)(JNIEnv *, jclass)
{
    if (cpu_thread_running)
        return;
    pc_reset_hard_init();
    plat_pause(0);

    cpu_thread_run     = 1;
    cpu_thread_running = true;
    emu_thread         = std::thread(emu_thread_fn);

    onesec_run    = true;
    onesec_thread = std::thread([]() {
        plat_set_thread_name(nullptr, "onesec");
        auto next = std::chrono::steady_clock::now();
        while (onesec_run) {
            next += std::chrono::seconds(1);
            std::this_thread::sleep_until(next);
            if (onesec_run)
                pc_onesec();
        }
    });
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeStop)(JNIEnv *, jclass)
{
    onesec_run = false;
    if (onesec_thread.joinable())
        onesec_thread.join();
    cpu_thread_run = 0;
    if (emu_thread.joinable())
        emu_thread.join();
    nvr_save();
    config_save();
    pc_close(nullptr);
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeSetSurface)(JNIEnv *env, jclass, jobject jsurface)
{
    std::lock_guard<std::mutex> lock(surface_mtx);
    if (surface) {
        ANativeWindow_release(surface);
        surface = nullptr;
    }
    if (jsurface) {
        surface   = ANativeWindow_fromSurface(env, jsurface);
        surface_w = surface_h = 0; /* set the geometry on the next frame */
    }
}

/* Background / foreground.  Android may end a background app without notice, so
   a pause also saves what the machine keeps (the disk is written through). */
extern "C" JNIEXPORT void JNICALL
JNI(nativePause)(JNIEnv *, jclass, jboolean paused)
{
    plat_pause(paused ? 1 : 0);
    if (paused) {
        nvr_save();
        config_save();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
JNI(nativeIsPaused)(JNIEnv *, jclass)
{
    return dopause ? JNI_TRUE : JNI_FALSE;
}

/* x, y: 0..1 across the picture; down: finger on the glass. */
extern "C" JNIEXPORT void JNICALL
JNI(nativeTouch)(JNIEnv *, jclass, jfloat x, jfloat y, jboolean down)
{
    mouse_x_abs               = (x < 0.0f) ? 0.0 : ((x > 1.0f) ? 1.0 : x);
    mouse_y_abs               = (y < 0.0f) ? 0.0 : ((y > 1.0f) ? 1.0 : y);
    mouse_tablet_in_proximity = 1;
    if (mouse_input_mode == 0)
        mouse_input_mode = 1; /* the buttons go to the tablet only in absolute mode */
    mouse_set_buttons_ex(down ? 1 : 0);
}

/* A line on the funworld I/O card (FWIO_LINE_*): a coin or note accepted, or the
   Setup button behind the door.  The card holds it as long as the real part. */
extern "C" JNIEXPORT void JNICALL
JNI(nativePulse)(JNIEnv *, jclass, jint line)
{
    funworld_io_pulse(line);
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeHardReset)(JNIEnv *, jclass)
{
    hard_reset_pending = 1;
}

/* For the app bar: the speed (percent of real time), the frame size, whether the
   machine still runs (the guest can power it off), and what it is. */
extern "C" JNIEXPORT jint JNICALL
JNI(nativeSpeed)(JNIEnv *, jclass)
{
    return android_speed_percent;
}

extern "C" JNIEXPORT jintArray JNICALL
JNI(nativeFrameSize)(JNIEnv *env, jclass)
{
    jint       wh[2] = { surface_w, surface_h };
    jintArray  out   = env->NewIntArray(2);
    env->SetIntArrayRegion(out, 0, 2, wh);
    return out;
}

extern "C" JNIEXPORT jboolean JNICALL
JNI(nativeIsRunning)(JNIEnv *, jclass)
{
    return cpu_thread_running ? JNI_TRUE : JNI_FALSE;
}

/* { title, detail }: what the image says it is ("IGO 5 PT - NSB: MB001", as the
   desktop's window title), and the dongle banner the cabinet is answered with. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeCabinetInfo)(JNIEnv *env, jclass)
{
    char banner[64] = "";
    char terr[16]   = "";
    std::string detail;
    if (photoplay_selected_image()[0]) {
        if (photoplay_image_ident(banner, sizeof(banner), terr, sizeof(terr)))
            detail = banner;
        else if (photoplay_image_is_pp20())
            detail = "Photo Play 2.0 (no dongle)";
    }
    return strings(env, { vm_name, detail });
}

/* ---- the Machine Manager (ManagerActivity) -------------------------------- */

/* Photo Play 2.0 carries no dongle: its games key on the physical layout of the
   disk (Microcosm CopyControl), which copying an image file by file destroys.
   PeepeeBox's ppfix puts it back and stamps LBA 1 with this marker; an image
   without it boots, but its games refuse to start (photoplay.c,
   pp_check_copycontrol). */
static bool
pp20_repaired(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char sec[8] = { 0 };
    const bool ok = (fseeko(f, 512, SEEK_SET) == 0) && (fread(sec, 1, 8, f) == 8) && !memcmp(sec, "PPBOXCC1", 8);
    fclose(f);
    return ok;
}

/* { runnable "1"/"0", release, territory, NSB, banner, note }, as the desktop's
   Machine Manager lists an image: all of it read out of the image (MAIN.SET, or
   VERSION.STR on Photo Play 2.0; MENU\NSB.NR), never out of its file name.
   Needs no machine. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeIdentify)(JNIEnv *env, jclass, jstring jpath)
{
    const std::string path = jstr(env, jpath);
    char label[160]  = "";
    char banner[160] = "";
    char terr[32]    = "";
    char nsb[64]     = "";
    std::string note;

    const bool dongle = photoplay_identify_ex(path.c_str(), label, sizeof(label), banner, sizeof(banner), terr, sizeof(terr));
    const bool pp20   = !dongle && photoplay_identify_pp20(path.c_str(), label, sizeof(label), banner, sizeof(banner), terr, sizeof(terr));
    if (!dongle && !pp20)
        return strings(env, { "0", "", "", "", "", "Not a Photo Play image (no \\FOTO\\SETTINGS\\MAIN.SET)" });

    photoplay_image_nsb(path.c_str(), nsb, sizeof(nsb));
    if (pp20 && !pp20_repaired(path.c_str()))
        note = "Photo Play 2.0, not repaired with ppfix: the games will not start";

    /* The release without the territory on the end ("IGO 5 PT" -> "IGO 5"), as the
       desktop's list, which has a column for each (qt_hddmanager.cpp, bareRelease). */
    std::string release = label;
    const std::string t = terr;
    if (!t.empty()) {
        const std::string bracketed = " (" + t + ")";
        const std::string suffixed  = " " + t;
        if ((release.size() > bracketed.size()) && !release.compare(release.size() - bracketed.size(), bracketed.size(), bracketed))
            release.resize(release.size() - bracketed.size());
        else if ((release.size() > suffixed.size()) && !release.compare(release.size() - suffixed.size(), suffixed.size(), suffixed))
            release.resize(release.size() - suffixed.size());
    }
    return strings(env, { "1", release, t, nsb, banner, note });
}

/* Make this image the cabinet's disk (the desktop's Machine Manager pick).  After
   nativeInit; if the machine already runs, it is rebuilt on a hard reset: the
   profile is stamped again from pc_reset_hard_init(), which mounts the new image,
   and the dongle, which answers from what the image says it is, forgets the last
   one (photoplay_set_selected_image). */
extern "C" JNIEXPORT void JNICALL
JNI(nativeSelect)(JNIEnv *env, jclass, jstring jimage)
{
    const std::string image   = jstr(env, jimage);
    const bool        running = cpu_thread_running;
    const int         was     = dopause;
    if (running)
        plat_pause(1);

    photoplay_set_selected_image(image.c_str());

    /* Name the machine now: the reset that would set vm_name has not run yet. */
    char ident[96] = { 0 };
    photoplay_image_label(image.c_str(), ident, sizeof(ident));
    if (ident[0]) {
        strncpy(vm_name, ident, sizeof(vm_name) - 1);
        vm_name[sizeof(vm_name) - 1] = '\0';
    }

    config_save();
    if (running) {
        config_changed = 2;
        pc_reset_hard();
        plat_pause(was);
    }
}

/* ---- fun.net over the network card (the desktop's Tools > Network) --------- */

/* { fitted "1"/"0", "lswitch"/"rswitch", remote switch host, secret, card name }.
   No cabinet had a card; an image given the Ethernet option reaches fun.net
   through it instead of the modem (photoplay.c, photoplay_net_enabled). */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeNetworkGet)(JNIEnv *env, jclass)
{
    const netcard_conf_t &nc  = net_cards_conf[0];
    const int             dev = network_card_get_from_internal_name((char *) PHOTOPLAY_NIC);
    const device_t       *d   = (dev > 0) ? network_card_getdevice(dev) : nullptr;
    return strings(env, { photoplay_net_enabled() ? "1" : "0",
                          (nc.net_type == NET_TYPE_NRSWITCH) ? "rswitch" : "lswitch",
                          (nc.net_type == NET_TYPE_NRSWITCH) ? nc.nrs_hostname : "", nc.secret,
                          d ? d->name : PHOTOPLAY_NIC });
}

/* Fit (or take out) the card, on the local switch (the same network, as the
   desktop's Local Switch) or a remote one (host[:port]: a fun.net stand-in on the
   Internet, or one PC).  The cabinet restarts on it.  SLiRP and PCap are not in
   the Android build. */
extern "C" JNIEXPORT void JNICALL
JNI(nativeNetworkSet)(JNIEnv *env, jclass, jboolean on, jboolean remote, jstring jhost, jstring jsecret)
{
    const std::string host   = jstr(env, jhost);
    const std::string secret = jstr(env, jsecret);

    const bool was_on    = photoplay_net_enabled();
    const bool was_rs    = net_cards_conf[0].net_type == NET_TYPE_NRSWITCH;
    const bool changed   = (was_on != (bool) on) ||
                         (on && ((was_rs != (bool) remote) || strcmp(net_cards_conf[0].secret, secret.c_str()) ||
                                 (remote && strcmp(net_cards_conf[0].nrs_hostname, host.c_str()))));

    net_cards_conf[0].net_type = remote ? NET_TYPE_NRSWITCH : NET_TYPE_NLSWITCH;
    snprintf(net_cards_conf[0].nrs_hostname, sizeof(net_cards_conf[0].nrs_hostname), "%s", remote ? host.c_str() : "");
    snprintf(net_cards_conf[0].secret, sizeof(net_cards_conf[0].secret), "%s", secret.c_str());
    net_cards_conf[0].host_dev_name[0] = '\0';
    photoplay_set_net_enabled(on ? 1 : 0);
    if (on)
        net_cards_conf[0].device_num = network_card_get_from_internal_name((char *) PHOTOPLAY_NIC);

    if (changed)
        restart_cabinet();
    else
        config_save();
}

/* ---- the modem on COM4 (the desktop's Tools > Modem) ----------------------- */

/* The device's own settings live in its section for instance 4 (COM4: serial.c
   builds it with char_init(.., port + 1)), per part.  The line is written to both
   parts' sections, so changing the part keeps where it dials. */
static std::string
modem_section(const char *internal)
{
    const int dev = char_get_from_internal_name(internal, DEVICE_COM);
    if (dev <= 0)
        return std::string();
    return std::string(char_get_device(dev)->name) + " #" + std::to_string(PHOTOPLAY_MODEM_PORT + 1);
}

/* { fitted part ("" = none), host ("" = no line), port, sounds "1"/"0", then
   { internal name, name } per part the cabinets were found with }. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeModemGet)(JNIEnv *env, jclass)
{
    const char       *part = photoplay_modem();
    const std::string sec  = modem_section(part[0] ? part : photoplay_modem_list(0));
    char             *s    = (char *) sec.c_str();
    const char       *host = config_get_string(s, (char *) "host", (char *) "");
    const bool        tcp  = config_get_int(s, (char *) "line", 0) == 1;

    std::vector<std::string> v = { part, (tcp && host) ? host : "",
                                   std::to_string(config_get_int(s, (char *) "host_port", 23)),
                                   photoplay_modem_sounds() ? "1" : "0" };
    for (int i = 0; photoplay_modem_list(i); i++) {
        const int dev = char_get_from_internal_name(photoplay_modem_list(i), DEVICE_COM);
        if (dev <= 0)
            continue;
        v.emplace_back(photoplay_modem_list(i));
        v.emplace_back(char_get_device(dev)->name);
    }
    return strings(env, v);
}

/* Fit a part (or none) and set its line: an empty host is a dead line.  The
   speaker is heard (or not) at once; the rest restarts the cabinet, only if it
   changed (fitting the modem creates COM4). */
extern "C" JNIEXPORT void JNICALL
JNI(nativeModemSet)(JNIEnv *env, jclass, jstring jpart, jstring jhost, jint port, jboolean sounds)
{
    const std::string part = jstr(env, jpart);
    const std::string host = jstr(env, jhost);

    photoplay_set_modem_sounds(sounds ? 1 : 0);

    const std::string was_part = photoplay_modem();
    const std::string cur      = modem_section(was_part.empty() ? photoplay_modem_list(0) : was_part.c_str());
    const char       *old_host = config_get_string((char *) cur.c_str(), (char *) "host", (char *) "");
    const int         old_line = config_get_int((char *) cur.c_str(), (char *) "line", 0);
    const int         old_port = config_get_int((char *) cur.c_str(), (char *) "host_port", 23);
    const int         line     = host.empty() ? 0 : 1;
    const bool        changed  = (was_part != part) ||
                          (!part.empty() && ((old_line != line) ||
                                             (line && ((old_port != port) || strcmp(old_host ? old_host : "", host.c_str())))));

    photoplay_set_modem(part.c_str());
    for (int i = 0; photoplay_modem_list(i); i++) {
        const std::string sec = modem_section(photoplay_modem_list(i));
        if (sec.empty())
            continue;
        config_set_int((char *) sec.c_str(), (char *) "line", line);
        config_set_string((char *) sec.c_str(), (char *) "host", (char *) host.c_str());
        config_set_int((char *) sec.c_str(), (char *) "host_port", port);
    }

    if (changed)
        restart_cabinet();
    else
        config_save();
}
