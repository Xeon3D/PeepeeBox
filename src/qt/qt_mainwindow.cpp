/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main window module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *          dob205
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 *          Copyright 2022 dob205
 */
#include <QDebug>
#include <cmath>

#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

#include "qt_soundgain.hpp"
#include "qt_preferences.hpp"
#include "qt_autoupdate.hpp"
#include "qt_mcadevicelist.hpp"
#include "qt_hddmanager.hpp"

#include "qt_rendererstack.hpp"
#include "qt_renderercommon.hpp"


#include "qt_defs.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/mouse.h>
#include <86box/machine.h>
#include <86box/vid_ega.h>
#include <86box/version.h>
#include <86box/timer.h>
#include <86box/apm.h>
#include <86box/nvr.h>
#include <86box/renderdefs.h>
#include <86box/lpt.h>
#include <86box/char.h>
#include <86box/photoplay.h>
#include <86box/funworld_io.h>
#include <86box/prn_cp80.h>

#ifdef USE_VNC
#    include <86box/vnc.h>
#endif

extern int qt_nvr_save(void);

#ifdef MTR_ENABLED
#    include <minitrace/minitrace.h>
#endif

/* to avoid including the entire cpu.h */
extern void nmi_raise(void);

extern bool cpu_thread_running;
extern bool fast_forward;
};

#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCursor>
#include <QShortcut>
#include <QMessageBox>
#include <QFocusEvent>
#include <QApplication>
#include <QByteArray>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QMenuBar>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <QActionGroup>
#include <QOpenGLContext>
#include <QScreen>
#include <QString>
#include <QDir>
#include <QSysInfo>
#include <QEventLoop>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QFile>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QTransform>
#include <QGridLayout>
#if QT_CONFIG(vulkan)
#    include <QVulkanInstance>
#    include <QVulkanFunctions>
#endif

void qt_set_sequence_auto_mnemonic(bool b);

#include <array>
#include <memory>
#include <unordered_map>

#include "qt_deviceconfig.hpp"
#include "qt_networksettings.hpp"
#include "qt_about.hpp"
#include "qt_machinestatus.hpp"
#include "qt_mediamenu.hpp"
#include "qt_util.hpp"
#include "qt_osd.hpp"

#if defined __unix__ && !defined __HAIKU__
#    ifndef Q_OS_MACOS
#        include "evdev_keyboard.hpp"
#    endif
#    ifdef XKBCOMMON
#        include "xkbcommon_keyboard.hpp"
#        ifdef XKBCOMMON_X11
#            include "xkbcommon_x11_keyboard.hpp"
#        endif
#        ifdef WAYLAND
#            include "xkbcommon_wl_keyboard.hpp"
#        endif
#    endif
#    include <X11/Xlib.h>
#    include <X11/keysym.h>
#    undef KeyPress
#    undef KeyRelease
#endif

#if defined Q_OS_UNIX && !defined Q_OS_HAIKU && !defined Q_OS_MACOS
#    include <qpa/qplatformwindow.h>
#    include "x11_util.h"
#endif

#ifdef Q_OS_MACOS
#    include "cocoa_keyboard.hpp"
// The namespace is required to avoid clashing typedefs; we only use this
// header for its #defines anyway.
namespace IOKit {
#    include <IOKit/hidsystem/IOLLEvent.h>
}
#endif

#ifdef __HAIKU__
#    include <os/AppKit.h>
#    include <os/InterfaceKit.h>
#    include "be_keyboard.hpp"

extern MainWindow *main_window;
QShortcut         *windowedShortcut;

filter_result
keyb_filter(BMessage *message, BHandler **target, BMessageFilter *filter)
{
    if (message->what == B_KEY_DOWN || message->what == B_KEY_UP
        || message->what == B_UNMAPPED_KEY_DOWN || message->what == B_UNMAPPED_KEY_UP) {
        int key_state = 0, key_scancode = 0;
        key_state = message->what == B_KEY_DOWN || message->what == B_UNMAPPED_KEY_DOWN;
        message->FindInt32("key", &key_scancode);
        QGuiApplication::postEvent(main_window, new QKeyEvent(key_state ? QEvent::KeyPress : QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        if (key_scancode == 0x68 && key_state) {
            QGuiApplication::postEvent(main_window, new QKeyEvent(QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        }
    }
    return B_DISPATCH_MESSAGE;
}

static BMessageFilter *filter;
#endif

extern int      cpu_force_interpreter;

extern void     qt_mouse_capture(int);
extern "C" void qt_blit(int x, int y, int w, int h, int monitor_index);

extern MainWindow *main_window;

int                main_window_blocked = 0;
int                exiting_manually    = 0;

#ifdef Q_OS_WINDOWS
static bool
canProcessUiEventsInCurrentState()
{
    const bool has_modal_widget  = QApplication::activeModalWidget() != nullptr;
    return !cpu_thread_run || dopause || has_modal_widget || main_window_blocked;
}

static void
processEventsOnlyWhenPausedOrModal()
{
    if (!canProcessUiEventsInCurrentState())
        return;
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers);
}
#endif

/* PeepeeBox: the receipt printer's window, defined further down with the rest
   of the cabinet's controls. */
static void cp80_show(QWidget *parent);
static void cp80_pump();
static void cp80_select_model(int index, bool persist);
static void dp3000_select_english(bool enabled, bool persist);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    mm             = std::make_shared<MediaMenu>(this);
    MediaMenu::ptr = mm;
    status         = std::make_unique<MachineStatus>(this);

#ifdef __HAIKU__
    filter = new BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, keyb_filter);
    ((BWindow *) this->winId())->AddFilter(filter);
#endif
    setUnifiedTitleAndToolBarOnMac(true);
    extern MainWindow *main_window;
    main_window = this;
    ui->setupUi(this);
    status->setSoundMenu(ui->menuSound);
    ui->actionMute_Unmute->setText(sound_muted ? tr("&Unmute") : tr("&Mute"));
    ui->stackedWidget->setMouseTracking(true);

    /* The printer mechanism is cabinet configuration, not a control on either
       physical printer.  Keep the choice in Tools and persist its stable name
       in [Photo Play] rather than exposing a model picker in the paper window. */
    {
        auto *printer_type_group = new QActionGroup(this);

        printer_type_group->addAction(ui->actionPrinter_DPU_414);
        printer_type_group->addAction(ui->actionPrinter_DATAprint_3000);
        printer_type_group->setExclusive(true);
        ui->actionPrinter_DATAprint_3000->setChecked(
            !strcmp(photoplay_printer(), PHOTOPLAY_PRINTER_DP3000));
        ui->actionPrinter_DPU_414->setChecked(
            !ui->actionPrinter_DATAprint_3000->isChecked());
        ui->actionPrinter_DATAprint_English->setChecked(
            photoplay_dataprint_english() != 0);
        ui->actionPrinter_DATAprint_English->setEnabled(
            ui->actionPrinter_DATAprint_3000->isChecked());
        dp3000_select_english(
            ui->actionPrinter_DATAprint_English->isChecked(), false);

        connect(printer_type_group, &QActionGroup::triggered, this,
                [this](QAction *action) {
            const bool dataprint =
                action == ui->actionPrinter_DATAprint_3000;

            cp80_select_model(dataprint ? 1 : 0, true);
            ui->actionPrinter_DATAprint_English->setEnabled(dataprint);
        });
        connect(ui->actionPrinter_DATAprint_English, &QAction::toggled, this,
                [](bool enabled) {
            dp3000_select_english(enabled, true);
        });
    }

    /* PeepeeBox: watch for the guest starting to print.  The cabinet's printer
       is not a thing anyone thinks to go looking for, and a receipt that
       arrived while nobody had the window open is a run wasted -- so the paper
       puts itself on screen the first time a byte reaches it, and keeps itself
       up to date after that. */
    {
        auto *paper_watch = new QTimer(this);

        connect(paper_watch, &QTimer::timeout, this, [this]() {
            static bool shown = false;

            /* Busy guest polling asks for the still-unplugged printer window;
               also reveal it on the first byte, because a receipt that printed
               while nobody had the window open is a run wasted. */
            const bool wants = (prn_cp80_attention() != 0);
            const bool capture = getenv("PEEPEEBOX_PRN_SCREENSHOT") != nullptr;

            if (wants || !shown) {
                if (!wants && !prn_cp80_dirty() && !capture)
                    return;
                shown = true;
                cp80_show(this);
            }
            cp80_pump();
        });
        paper_watch->start(400);
    }
    statusBar()->setVisible(!hide_status_bar);

    /* The emulation speed -- and " - PAUSED", and the mouse capture hint when there
       is a mouse -- sits where upstream showed the refresh rate.  It used to trail the
       toolbar buttons; a cabinet's refresh rate is fixed and says nothing, the speed
       is the one reading worth a glance.  Wide enough for "100%" so the lock icons
       beside it do not twitch between 99 and 100. */
    status_label = new QLabel;
    status_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_label->setMinimumWidth(status_label->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
    status_label->setToolTip(tr("Emulation speed"));
    statusBar()->addPermanentWidget(status_label);

    num_icon        = QIcon(":/settings/qt/icons/num_lock_on.ico");
    num_icon_off    = QIcon(":/settings/qt/icons/num_lock_off.ico");
    scroll_icon     = QIcon(":/settings/qt/icons/scroll_lock_on.ico");
    scroll_icon_off = QIcon(":/settings/qt/icons/scroll_lock_off.ico");
    caps_icon       = QIcon(":/settings/qt/icons/caps_lock_on.ico");
    caps_icon_off   = QIcon(":/settings/qt/icons/caps_lock_off.ico");
    kana_icon       = QIcon(":/settings/qt/icons/kana_lock_on.ico");
    kana_icon_off   = QIcon(":/settings/qt/icons/kana_lock_off.ico");

    num_label = new QLabel;
    num_label->setPixmap(num_icon_off.pixmap(QSize(16, 16)));
    num_label->setToolTip(QShortcut::tr("Num Lock"));
    statusBar()->addPermanentWidget(num_label);

    caps_label = new QLabel;
    caps_label->setPixmap(caps_icon_off.pixmap(QSize(16, 16)));
    caps_label->setToolTip(QShortcut::tr("Caps Lock"));
    statusBar()->addPermanentWidget(caps_label);

    scroll_label = new QLabel;
    scroll_label->setPixmap(scroll_icon_off.pixmap(QSize(16, 16)));
    scroll_label->setToolTip(QShortcut::tr("Scroll Lock"));
    statusBar()->addPermanentWidget(scroll_label);

    kana_label = new QLabel;
    kana_label->setPixmap(kana_icon_off.pixmap(QSize(16, 16)));
    kana_label->setToolTip(QShortcut::tr("Kana Lock"));
    statusBar()->addPermanentWidget(kana_label);

    QTimer *ledKeyboardTimer = new QTimer(this);
    ledKeyboardTimer->setTimerType(Qt::CoarseTimer);
    ledKeyboardTimer->setInterval(100);
    connect(ledKeyboardTimer, &QTimer::timeout, this, [this]() {
        static uint8_t prev_caps = 255, prev_num = 255, prev_scroll = 255, prev_kana = 255;
        uint8_t caps, num, scroll, kana;
        keyboard_get_states(&caps, &num, &scroll, &kana);

        if (num_label->isVisible() && prev_num != num)
            num_label->setPixmap(num ? this->num_icon.pixmap(QSize(16, 16)) : this->num_icon_off.pixmap(QSize(16, 16)));
        if (caps_label->isVisible() && prev_caps != caps)
            caps_label->setPixmap(caps ? this->caps_icon.pixmap(QSize(16, 16)) : this->caps_icon_off.pixmap(QSize(16, 16)));
        if (scroll_label->isVisible() && prev_scroll != scroll)
            scroll_label->setPixmap(scroll ? this->scroll_icon.pixmap(QSize(16, 16)) : this->scroll_icon_off.pixmap(QSize(16, 16)));

        if (kana_label->isVisible() && prev_kana != kana)
            kana_label->setPixmap(kana ? this->kana_icon.pixmap(QSize(16, 16)) : this->kana_icon_off.pixmap(QSize(16, 16)));

        prev_caps   = caps;
        prev_num    = num;
        prev_scroll = scroll;
        prev_kana   = kana;
    });
    ledKeyboardTimer->start();

#ifdef Q_OS_WINDOWS
    util::setWin11RoundedCorners(this->winId(), (hide_status_bar ? false : true));
#endif
    statusBar()->setStyleSheet("QStatusBar::item {border: None; } QStatusBar QLabel { margin-right: 2px; margin-bottom: 1px; }");
    this->centralWidget()->setStyleSheet("background-color: black;");
    ui->toolBar->setVisible(!hide_tool_bar);
    /* Grayed out rather than hidden, so it is visible that the feature is
       there and switched off rather than missing from the build. */
    ui->actionHDD_manager->setEnabled(hdd_manager > 0);
#ifdef _WIN32
    ui->toolBar->setBackgroundRole(QPalette::Light);
#endif
    renderers[0].reset(nullptr);

    this->setWindowFlag(Qt::CustomizeWindowHint, true);
    this->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, vid_resize != 1);
    this->setWindowFlag(Qt::WindowMaximizeButtonHint, vid_resize == 1);
    this->setWindowFlag(Qt::WindowFullscreenButtonHint, vid_resize == 1);

    updateWindowTitle();

    connect(this, &MainWindow::forceInterpretationCompleted, this, [this]() {
        const auto fi_icon      = cpu_force_interpreter ? QIcon(":/menuicons/qt/icons/recompiler.ico") :
                                                          QIcon(":/menuicons/qt/icons/interpreter.ico");
        const auto tooltip_text = cpu_force_interpreter ? QString(tr("Allow recompilation")) :
                                                          QString(tr("Force interpretation"));
        const auto menu_text    = cpu_force_interpreter ? QString(tr("&Allow recompilation")) :
                                                          QString(tr("&Force interpretation"));

    });

    connect(this, &MainWindow::hardResetCompleted, this, [this]() {
        ui->actionMCA_devices->setVisible(machine_has_bus(machine, MACHINE_BUS_MCA));
        ui_update_force_interpreter();
        updateMouseStrings();
        num_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        scroll_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        caps_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        int ext_ax_kbd = machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD) && (keyboard_type == KEYBOARD_TYPE_AX);
        int int_ax_kbd = machine_has_flags(machine, MACHINE_KEYBOARD_JIS) && !machine_has_bus(machine, MACHINE_BUS_PS2_PORTS);
        kana_label->setVisible(ext_ax_kbd || int_ax_kbd);

        ui->actionMouse->setEnabled(true);
        ui->actionTablet->setEnabled(true);
        ui->actionTablet_Crosshair->setEnabled(true);
        if (!mouse_both_enabled()) {
            if (!mouse_type) {
                ui->actionMouse->setDisabled(true);
                if (mouse_input_mode == 0)
                    mouse_input_mode = 1;
            }

            if (!tablet_type) {
                ui->actionTablet->setDisabled(true);
                ui->actionTablet_Crosshair->setDisabled(true);
                if (mouse_input_mode > 0)
                    mouse_input_mode = 0;
            }
        }

        ui->menuInput_device->menuAction()->setVisible(tablet_type);

        if (mouse_input_mode >= 1 && QApplication::overrideCursor())
            while (QApplication::overrideCursor())
                QApplication::restoreOverrideCursor();


        if (mouse_input_mode == 0)
            ui->actionMouse->setChecked(1);
        if (mouse_input_mode == 1)
            ui->actionTablet->setChecked(1);
        if (mouse_input_mode == 2)
            ui->actionTablet_Crosshair->setChecked(1);
    });

    connect(this, &MainWindow::showMessageForNonQtThread, this, &MainWindow::showMessage_, Qt::QueuedConnection);

    /* Kept whether or not the status bar is showing: pausing appends to what
       getTitle() returns, and the on-screen menu shows it too. */
    connect(this, &MainWindow::setTitle, this, [this](const QString &title) {
        status_text = title;
        status_label->setText(status_text);
    });

    connect(this, &MainWindow::updateMenuResizeOptions, [this]() {
        ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    });

    connect(this, &MainWindow::updateWindowRememberOption, [this]() {
    });

    emit updateMenuResizeOptions();

    connect(this, &MainWindow::setMouseCapture, this, [this](bool state) {
        const int old_mouse_capture = mouse_capture;
        mouse_capture = state ? 1 : 0;

        if (mouse_capture == old_mouse_capture)
            return;

        qt_mouse_capture(mouse_capture);
        if (mouse_capture) {
            if (hook_enabled)
                this->grabKeyboard();
            if (ui->stackedWidget->mouse_capture_func) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                auto *win = ui->stackedWidget->captureWindow();
                ui->stackedWidget->mouse_capture_func(win ? win : this->windowHandle());
#else
                ui->stackedWidget->mouse_capture_func(this->windowHandle());
#endif
            }
        } else {
            this->releaseKeyboard();
            if (ui->stackedWidget->mouse_uncapture_func) {
                ui->stackedWidget->mouse_uncapture_func();
            }
            ui->stackedWidget->unsetCursor();
        }
#ifndef Q_OS_MACOS
        if (kbd_req_capture) {
            qt_set_sequence_auto_mnemonic(!mouse_capture);
            /* Hack to get the menubar to update the internal Alt+shortcut table */
            if (!video_fullscreen) {
                ui->menubar->hide();
                ui->menubar->show();
            }
        }
#endif
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationState::ApplicationActive) {
            if (auto_paused) {
                plat_pause(0);
                auto_paused = 0;
            }
        } else {
            if (mouse_capture)
                emit setMouseCapture(false);

            keyboard_all_up();

            if (do_auto_pause && !dopause) {
                auto_paused = 1;
                plat_pause(1);
            }
        }
    });

    connect(this, &MainWindow::resizeContents, this, [this](int w, int h) {
        if (shownonce) {
            if (resizableonce == false)
                ui->stackedWidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resizableonce = true;
        }
        if (!hide_status_bar) {
            statusBar()->hide();
            statusBar()->show();
        }
        if (!hide_tool_bar) {
            ui->toolBar->hide();
            ui->toolBar->show();
        }
        if (!QApplication::platformName().contains("eglfs") && vid_resize != 1) {
            w = static_cast<int>(w / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.));

            const int modifiedHeight = static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.))
                + menuBar()->height()
                + (statusBar()->height() * !hide_status_bar)
                + (ui->toolBar->height() * !hide_tool_bar);

            ui->stackedWidget->resize(w, static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.)));
            setFixedSize(w, modifiedHeight);
        }
    });

    connect(this, &MainWindow::resizeContentsMonitor, this, [this](int w, int h, int monitor_index) {
        if (!QApplication::platformName().contains("eglfs") && vid_resize != 1) {
#ifdef QT_RESIZE_DEBUG
            qDebug() << "Resize";
#endif
            w = static_cast<int>(w / (!dpi_scale ? util::screenOfWidget(renderers[monitor_index].get())->devicePixelRatio() : 1.));

            int modifiedHeight = static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(renderers[monitor_index].get())->devicePixelRatio() : 1.));

            renderers[monitor_index]->setFixedSize(w, modifiedHeight);
        }
    });

    connect(ui->menubar, &QMenuBar::triggered, this, [this] {
        config_save();
        if (QApplication::activeWindow() == this) {
            ui->stackedWidget->setFocusRenderer();
        }
    });

    connect(this, &MainWindow::updateStatusBarPanes, this, [this] {
        refreshMediaMenu();
    });
    connect(this, &MainWindow::updateStatusBarPanes, this, &MainWindow::refreshMediaMenu);
    connect(this, &MainWindow::updateStatusBarTip, status.get(), &MachineStatus::updateTip);
    connect(this, &MainWindow::statusBarMessage, status.get(), &MachineStatus::message, Qt::QueuedConnection);

    ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);

#ifdef Q_OS_MACOS
#endif

#ifndef DISCORD
#else
#endif

    if ((QApplication::platformName().contains("eglfs") || QApplication::platformName() == "haiku")) {
        if ((vid_api == RENDERER_OPENGL3) || (vid_api == RENDERER_VULKAN))
            fprintf(stderr, "OpenGL renderers are unsupported on %s.\n", QApplication::platformName().toUtf8().data());
        vid_api = RENDERER_SOFTWARE;
        ui->actionVulkan->setVisible(false);
        ui->actionOpenGL->setVisible(false);
    }

#ifndef USE_VNC
    if (vid_api == RENDERER_VNC)
        vid_api = RENDERER_SOFTWARE;
    ui->actionVNC->setVisible(false);
#endif

#if QT_CONFIG(vulkan)
    bool vulkanAvailable = false;
    {
        QVulkanInstance instance;
        instance.setApiVersion(QVersionNumber(1, 0));
        if (instance.create()) {
            uint32_t physicalDevices = 0;
            instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
            if (physicalDevices != 0) {
                vulkanAvailable = true;
            }
        }
    }
    if (!vulkanAvailable)
#endif
    {
        if (vid_api == RENDERER_VULKAN)
            vid_api = RENDERER_SOFTWARE;
        ui->actionVulkan->setVisible(false);
    }

    auto actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionSoftware_Renderer);
    actGroup->addAction(ui->actionOpenGL);
    actGroup->addAction(ui->actionVulkan);
    actGroup->addAction(ui->actionVNC);
    actGroup->setExclusive(true);

    connect(actGroup, &QActionGroup::triggered, [this](QAction *action) {
        vid_api = action->property("vid_api").toInt();
#ifdef USE_VNC
        if (vnc_enabled && vid_api != RENDERER_VNC) {
            startblit();
            vnc_enabled = 0;
            vnc_close();
            video_setblit(qt_blit);
            endblit();
        }
#endif
        RendererStack::Renderer newVidApi = RendererStack::Renderer::Software;
        switch (vid_api) {
            default:
                break;
            case RENDERER_SOFTWARE:
                newVidApi = RendererStack::Renderer::Software;
                break;
            case RENDERER_OPENGL3:
                newVidApi = RendererStack::Renderer::OpenGL3;
                break;
            case RENDERER_VULKAN:
                newVidApi = RendererStack::Renderer::Vulkan;
                break;
#ifdef USE_VNC
            case RENDERER_VNC:
                {
                    newVidApi = RendererStack::Renderer::Software;
                    startblit();
                    vnc_enabled = vnc_init(nullptr);
                    endblit();
                }
#endif
        }
        ui->stackedWidget->switchRenderer(newVidApi);
        ui->menuOpenGL_input_scale->setEnabled(newVidApi == RendererStack::Renderer::OpenGL3);
        ui->menuOpenGL_input_stretch_mode->setEnabled(newVidApi == RendererStack::Renderer::OpenGL3);
        if (!show_second_monitors)
            return;
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (renderers[i])
                renderers[i]->switchRenderer(newVidApi);
        }
    });

    connect(ui->stackedWidget, &RendererStack::rendererChanged, [this]() {
        ui->actionRenderer_options->setEnabled(ui->stackedWidget->hasOptions());
    });

    /* Trigger initial renderer switch */
    for (const auto action : actGroup->actions())
        if (action->property("vid_api").toInt() == vid_api) {
            action->setChecked(true);
            emit actGroup->triggered(action);
            break;
        }

    ui->action_0_5x_2->setChecked(video_gl_input_scale < 1.0);
    ui->action_1x_2->setChecked(video_gl_input_scale >= 1.0 && video_gl_input_scale < 1.5);
    ui->action1_5x_2->setChecked(video_gl_input_scale >= 1.5 && video_gl_input_scale < 2.0);
    ui->action_2x_2->setChecked(video_gl_input_scale >= 2.0 && video_gl_input_scale < 3.0);
    ui->action_3x_2->setChecked(video_gl_input_scale >= 3.0 && video_gl_input_scale < 4.0);
    ui->action_4x_2->setChecked(video_gl_input_scale >= 4.0 && video_gl_input_scale < 5.0);
    ui->action_5x_2->setChecked(video_gl_input_scale >= 5.0 && video_gl_input_scale < 6.0);
    ui->action_6x_2->setChecked(video_gl_input_scale >= 6.0 && video_gl_input_scale < 7.0);
    ui->action_7x_2->setChecked(video_gl_input_scale >= 7.0 && video_gl_input_scale < 8.0);
    ui->action_8x_2->setChecked(video_gl_input_scale >= 8.0);

    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_0_5x_2);
    actGroup->addAction(ui->action_1x_2);
    actGroup->addAction(ui->action1_5x_2);
    actGroup->addAction(ui->action_2x_2);
    actGroup->addAction(ui->action_3x_2);
    actGroup->addAction(ui->action_4x_2);
    actGroup->addAction(ui->action_5x_2);
    actGroup->addAction(ui->action_6x_2);
    actGroup->addAction(ui->action_7x_2);
    actGroup->addAction(ui->action_8x_2);
    connect(actGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        if (action == ui->action_0_5x_2)
            video_gl_input_scale = 0.5;
        if (action == ui->action_1x_2)
            video_gl_input_scale = 1;
        if (action == ui->action1_5x_2)
            video_gl_input_scale = 1.5;
        if (action == ui->action_2x_2)
            video_gl_input_scale = 2;
        if (action == ui->action_3x_2)
            video_gl_input_scale = 3;
        if (action == ui->action_4x_2)
            video_gl_input_scale = 4;
        if (action == ui->action_5x_2)
            video_gl_input_scale = 5;
        if (action == ui->action_6x_2)
            video_gl_input_scale = 6;
        if (action == ui->action_7x_2)
            video_gl_input_scale = 7;
        if (action == ui->action_8x_2)
            video_gl_input_scale = 8;
    });

    switch (scale) {
        default:
            break;
        case 0:
            ui->action0_5x->setChecked(true);
            break;
        case 1:
            ui->action1x->setChecked(true);
            break;
        case 2:
            ui->action1_5x->setChecked(true);
            break;
        case 3:
            ui->action2x->setChecked(true);
            break;
        case 4:
            ui->action3x->setChecked(true);
            break;
        case 5:
            ui->action4x->setChecked(true);
            break;
        case 6:
            ui->action5x->setChecked(true);
            break;
        case 7:
            ui->action6x->setChecked(true);
            break;
        case 8:
            ui->action7x->setChecked(true);
            break;
        case 9:
            ui->action8x->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action0_5x);
    actGroup->addAction(ui->action1x);
    actGroup->addAction(ui->action1_5x);
    actGroup->addAction(ui->action2x);
    actGroup->addAction(ui->action3x);
    actGroup->addAction(ui->action4x);
    actGroup->addAction(ui->action5x);
    actGroup->addAction(ui->action6x);
    actGroup->addAction(ui->action7x);
    actGroup->addAction(ui->action8x);
    switch (video_filter_method) {
        default:
            break;
        case 0:
            ui->actionNearest->setChecked(true);
            break;
        case 1:
            ui->actionLinear->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionNearest);
    actGroup->addAction(ui->actionLinear);
    switch (video_fullscreen_scale) {
        default:
            break;
        case FULLSCR_SCALE_FULL:
            break;
        case FULLSCR_SCALE_43:
            break;
        case FULLSCR_SCALE_KEEPRATIO:
            break;
        case FULLSCR_SCALE_INT:
            break;
        case FULLSCR_SCALE_INT43:
            break;
    }

    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionMouse);
    actGroup->addAction(ui->actionTablet);
    actGroup->addAction(ui->actionTablet_Crosshair);
    actGroup->setExclusive(true);

    connect(actGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
        if (action == ui->actionMouse)
            mouse_input_mode = 0;
        if (action == ui->actionTablet)
            mouse_input_mode = 1;
        if (action == ui->actionTablet_Crosshair)
            mouse_input_mode = 2;
    });

    auto orig_mouse_input_mode_initial = mouse_input_mode_initial;

    if (orig_mouse_input_mode_initial == 0)
        ui->actionMouse->setChecked(1);
    if (orig_mouse_input_mode_initial == 1)
        ui->actionTablet->setChecked(1);
    if (orig_mouse_input_mode_initial == 2)
        ui->actionTablet_Crosshair->setChecked(1);

    mouse_input_mode_initial = orig_mouse_input_mode_initial;

    actGroup = new QActionGroup(this);
    switch (video_gl_input_scale_mode) {
        default:
            break;
        case FULLSCR_SCALE_FULL:
            ui->action_Full_screen_stretch_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_43:
            ui->action_4_3_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_KEEPRATIO:
            ui->action_Square_pixels_keep_ratio_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_INT:
            ui->action_Integer_scale_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_INT43:
            ui->action4_3_Integer_scale_gl->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_Full_screen_stretch_gl);
    actGroup->addAction(ui->action_4_3_gl);
    actGroup->addAction(ui->action_Square_pixels_keep_ratio_gl);
    actGroup->addAction(ui->action_Integer_scale_gl);
    actGroup->addAction(ui->action4_3_Integer_scale_gl);
    connect(actGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        if (action == ui->action_Full_screen_stretch_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_FULL;
        if (action == ui->action_4_3_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_43;
        if (action == ui->action_Square_pixels_keep_ratio_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_KEEPRATIO;
        if (action == ui->action_Integer_scale_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_INT;
        if (action == ui->action4_3_Integer_scale_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_INT43;
    });
    if (force_43 > 0) {
    }
    if (force_constant_mouse > 0) {
    }

    if (!vnc_enabled)
        video_setblit(qt_blit);

    if (start_in_fullscreen) {
        connect(ui->stackedWidget, &RendererStack::blitToRenderer, this, [this]() {
            if (start_in_fullscreen) {
                QTimer::singleShot(100, ui->actionFullscreen, &QAction::trigger);
                start_in_fullscreen = 0;
            }
        });
    }

#ifdef MTR_ENABLED
    {
        ui->actionBegin_trace->setVisible(true);
        ui->actionEnd_trace->setVisible(true);
        ui->actionBegin_trace->setShortcut(QKeySequence(Qt::Key_Control + Qt::Key_T));
        ui->actionEnd_trace->setShortcut(QKeySequence(Qt::Key_Control + Qt::Key_T));
        ui->actionEnd_trace->setDisabled(true);
        static auto init_trace = [&] {
            mtr_init("trace.json");
            mtr_start();
        };
        static auto shutdown_trace = [&] {
            mtr_stop();
            mtr_shutdown();
        };
        static bool trace = false;
        connect(ui->actionBegin_trace, &QAction::triggered, this, [this] {
            if (trace)
                return;
            ui->actionBegin_trace->setDisabled(true);
            ui->actionEnd_trace->setDisabled(false);
            init_trace();
            trace = true;
        });
        connect(ui->actionEnd_trace, &QAction::triggered, this, [this] {
            if (!trace)
                return;
            ui->actionBegin_trace->setDisabled(false);
            ui->actionEnd_trace->setDisabled(true);
            shutdown_trace();
            trace = false;
        });
    }
#endif

    setContextMenuPolicy(Qt::PreventContextMenu);
    /* Remove default Shift+F10 handler, which unfocuses keyboard input even with no context menu. */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F10), this), &QShortcut::activated, this, []() {});
#else
    connect(new QShortcut(QKeySequence(Qt::SHIFT + Qt::Key_F10), this), &QShortcut::activated, this, []() {});
#endif

    connect(this, &MainWindow::initRendererMonitor, this, &MainWindow::initRendererMonitorSlot);
    connect(this, &MainWindow::initRendererMonitorForNonQtThread, this, &MainWindow::initRendererMonitorSlot, Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::destroyRendererMonitor, this, &MainWindow::destroyRendererMonitorSlot);
    connect(this, &MainWindow::destroyRendererMonitorForNonQtThread, this, &MainWindow::destroyRendererMonitorSlot, Qt::BlockingQueuedConnection);

#ifdef Q_OS_MACOS
    QTimer::singleShot(0, this, [this]() {
        for (auto curObj : this->menuBar()->children()) {
            if (qobject_cast<QMenu *>(curObj)) {
                auto menu = qobject_cast<QMenu *>(curObj);
                menu->setSeparatorsCollapsible(false);
                for (auto curObj2 : menu->children()) {
                    if (qobject_cast<QMenu *>(curObj2)) {
                        auto menu2 = qobject_cast<QMenu *>(curObj2);
                        menu2->setSeparatorsCollapsible(false);
                    }
                }
            }
        }
    });
#endif

    QTimer::singleShot(0, this, [this]() {
        for (auto curObj : this->menuBar()->children()) {
            if (qobject_cast<QMenu *>(curObj)) {
                auto menu = qobject_cast<QMenu *>(curObj);
                for (auto curObj2 : menu->children()) {
                    if (qobject_cast<QAction *>(curObj2)) {
                        auto action = qobject_cast<QAction *>(curObj2);
                        if (!action->shortcut().isEmpty()) {
                            this->insertAction(nullptr, action);
                        }
                    }
                }
            }
        }
    });

    actGroup = new QActionGroup(this);

    if (tablet_tool_type == 1) {
    } else {
    }

#ifdef XKBCOMMON
#    ifdef XKBCOMMON_X11
    if (QApplication::platformName().contains("xcb"))
        xkbcommon_x11_init();
    else
#    endif
#    ifdef WAYLAND
        if (QApplication::platformName().contains("wayland"))
        xkbcommon_wl_init();
    else
#    endif
    {
    }
#endif

#if defined Q_OS_UNIX && !defined Q_OS_MACOS && !defined Q_OS_HAIKU
    if (QApplication::platformName().contains("xcb")) {
        QTimer::singleShot(0, this, [this] {
            auto whandle = windowHandle();
            if (!whandle) {
                qWarning() << "No window handle";
            } else {
                QPlatformWindow *window = whandle->handle();
                set_wm_class(window->winId(), vm_name);
            }
        });
    }
#endif

    updateShortcuts();

    /* PeepeeBox: automatic updates.  Whatever the last update could not delete
       from under itself goes first; then the schedule starts. */
    AutoUpdate::cleanupLeftovers();
    (new AutoUpdate(this))->start();
}

/* PeepeeBox: the way out after an update has been installed and a restart
   accepted.  The confirmation is skipped because the question was just asked
   in other words; the shutdown itself is the ordinary one, and the relaunch
   happens in main() once it is complete. */
void
MainWindow::quitForUpdate()
{
    skip_exit_confirmation = true;
    on_actionExit_triggered();
}

void
MainWindow::closeEvent(QCloseEvent *event)
{
    if (!exiting_manually && mouse_capture) {
        event->ignore();
        return;
    }
    exiting_manually = 0;

    const bool skip_confirmation = skip_exit_confirmation;
    skip_exit_confirmation       = false;

    if (!skip_confirmation && confirm_exit && confirm_exit_cmdl && cpu_thread_run) {
        QMessageBox questionbox(QMessageBox::Icon::Question, EMU_NAME, tr("Are you sure you want to exit %1?").arg(EMU_NAME), QMessageBox::Yes | QMessageBox::No, this);
        auto        chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_exit);

        QObject::connect(chkbox, &QCheckBox::CHECK_STATE_CHANGED, [](int state) {
            confirm_exit = (state == Qt::CheckState::Unchecked);
        });
        questionbox.exec();
        if (questionbox.result() == QMessageBox::No) {
            confirm_exit = true;
            event->ignore();
            return;
        }
    }
    if (window_remember && !video_fullscreen) {
        window_w = ui->stackedWidget->width();
        window_h = ui->stackedWidget->height();
        if (!QApplication::platformName().contains("wayland")) {
            window_x = this->geometry().x();
            window_y = this->geometry().y();
        }
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (renderers[i]) {
                monitor_settings[i].mon_window_w = renderers[i]->geometry().width();
                monitor_settings[i].mon_window_h = renderers[i]->geometry().height();
                if (QApplication::platformName().contains("wayland"))
                    continue;
                monitor_settings[i].mon_window_x = renderers[i]->geometry().x();
                monitor_settings[i].mon_window_y = renderers[i]->geometry().y();
            }
        }
    }

    if (ui->stackedWidget->mouse_exit_func)
        ui->stackedWidget->mouse_exit_func();

    ui->stackedWidget->switchRenderer(RendererStack::Renderer::Software);
    for (int i = 1; i < MONITORS_NUM; i++) {
        if (renderers[i] && renderers[i]->isHidden()) {
            renderers[i]->show();
            renderers[i]->switchRenderer(RendererStack::Renderer::Software);
        }
    }

    qt_nvr_save();
    cpu_thread_run = 0;
    event->accept();
}

void
ui_update_force_interpreter()
{
    emit main_window->forceInterpretationCompleted();
}

void
MainWindow::updateShortcuts()
{
    /*
     Update menu shortcuts from accelerator table

     Note that these only work in windowed mode. If you add any new shortcuts,
     you have to go duplicate them in MainWindow::eventFilter()
     */

    // First we need to wipe all existing accelerators, otherwise Qt will
    // run into conflicts with old ones.
    ui->actionHard_Reset->setShortcut(QKeySequence());
    ui->actionFullscreen->setShortcut(QKeySequence());
    ui->actionPause->setShortcut(QKeySequence());
    ui->actionMute_Unmute->setShortcut(QKeySequence());
    ui->actionExit->setShortcut(QKeySequence());

    int          accID;
    QKeySequence seq;
    accID = FindAccelerator("hard_reset");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionHard_Reset->setShortcut(seq);
    accID = FindAccelerator("fullscreen");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionFullscreen->setShortcut(seq);

    accID = FindAccelerator("pause");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionPause->setShortcut(seq);

    accID = FindAccelerator("mute");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionMute_Unmute->setShortcut(seq);
    accID = FindAccelerator("exit");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionExit->setShortcut(seq);
}

void
MainWindow::updateMouseStrings()
{
    mouseStringCaptured = tr(mouse_get_buttons() > 2 ? "Press %1 to release mouse" : "Press %1 or middle button to release mouse").arg(QKeySequence(acc_keys[FindAccelerator("release_mouse")].seq, QKeySequence::PortableText).toString(QKeySequence::NativeText));
    mouseStringUncaptured = tr("Click to capture mouse");
}

void
MainWindow::resizeEvent(QResizeEvent *event)
{
#ifdef MOVE_WINDOW
    //qDebug() << pos().x() + event->size().width();
    //qDebug() << pos().y() + event->size().height();
    if (vid_resize == 1 || video_fullscreen)
        return;

    int newX = pos().x();
    int newY = pos().y();

    if (((frameGeometry().x() + event->size().width() + 1) > util::screenOfWidget(this)->availableGeometry().right())) {
        // move(util::screenOfWidget(this)->availableGeometry().right() - size().width() - 1, pos().y());
        newX = util::screenOfWidget(this)->availableGeometry().right() - frameGeometry().width() - 1;
        if (newX < 1)
            newX = 1;
    }

    if (((frameGeometry().y() + event->size().height() + 1) > util::screenOfWidget(this)->availableGeometry().bottom())) {
        newY = util::screenOfWidget(this)->availableGeometry().bottom() - frameGeometry().height() - 1;
        if (newY < 1)
            newY = 1;
    }
    move(newX, newY);
#endif /*MOVE_WINDOW*/
}

void
MainWindow::initRendererMonitorSlot(int monitor_index)
{
    auto &secondaryRenderer = this->renderers[monitor_index];
    secondaryRenderer       = std::make_unique<RendererStack>(nullptr, monitor_index);
    if (secondaryRenderer) {
        connect(secondaryRenderer.get(), &RendererStack::rendererChanged, this, [this, monitor_index] {
            this->renderers[monitor_index]->show();
        });
        secondaryRenderer->setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        secondaryRenderer->setWindowTitle(QObject::tr("%1 Monitor #%2").arg(EMU_NAME).arg(monitor_index + 1));
        secondaryRenderer->setContextMenuPolicy(Qt::PreventContextMenu);

        for (int i = 0; i < this->actions().size(); i++) {
            secondaryRenderer->addAction(this->actions()[i]);
        }

        if (vid_resize == 2)
            secondaryRenderer->setFixedSize(fixed_size_x, fixed_size_y);
        secondaryRenderer->setWindowIcon(this->windowIcon());
#ifdef Q_OS_WINDOWS
        util::setWin11RoundedCorners(secondaryRenderer->winId(), false);
#endif
        if (show_second_monitors) {
            secondaryRenderer->show();
            if (window_remember) {
                secondaryRenderer->setGeometry(monitor_settings[monitor_index].mon_window_x < 120 ? 120 : monitor_settings[monitor_index].mon_window_x,
                                               monitor_settings[monitor_index].mon_window_y < 120 ? 120 : monitor_settings[monitor_index].mon_window_y,
                                               monitor_settings[monitor_index].mon_window_w > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_w,
                                               monitor_settings[monitor_index].mon_window_h > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_h);
            }
            if (monitor_settings[monitor_index].mon_window_maximized)
                secondaryRenderer->showMaximized();
            secondaryRenderer->switchRenderer((RendererStack::Renderer) vid_api);
            secondaryRenderer->setMouseTracking(true);

            if (monitor_settings[monitor_index].mon_window_maximized) {
                if (renderers[monitor_index])
                    renderers[monitor_index]->onResize(renderers[monitor_index]->width(),
                                                       renderers[monitor_index]->height());

                device_force_redraw();
            }
        }
    }
}

void
MainWindow::destroyRendererMonitorSlot(int monitor_index)
{
    if (this->renderers[monitor_index]) {
        if (window_remember) {
            monitor_settings[monitor_index].mon_window_w = renderers[monitor_index]->geometry().width();
            monitor_settings[monitor_index].mon_window_h = renderers[monitor_index]->geometry().height();
            monitor_settings[monitor_index].mon_window_x = renderers[monitor_index]->geometry().x();
            monitor_settings[monitor_index].mon_window_y = renderers[monitor_index]->geometry().y();
        }
        config_save();
        this->renderers[monitor_index].release()->deleteLater();
        ui->stackedWidget->switchRenderer((RendererStack::Renderer) vid_api);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void
MainWindow::showEvent(QShowEvent *event)
{
    if (shownonce)
        return;
    shownonce = true;
    if (window_remember) {
        if (window_w == 0)
            window_w = 320;
        if (window_h == 0)
            window_h = 200;
    }

    if (window_remember && !QApplication::platformName().contains("wayland")) {
        setGeometry(window_x, window_y, window_w, window_h + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));
    }
    if (vid_resize == 2) {
        setFixedSize(fixed_size_x, fixed_size_y + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));

        monitors[0].mon_scrnsz_x = fixed_size_x;
        monitors[0].mon_scrnsz_y = fixed_size_y;
    }
    if (window_remember && vid_resize == 1) {
        ui->stackedWidget->setFixedSize(window_w, window_h);
        this->adjustSize();
    }
}

void
MainWindow::on_actionHard_Reset_triggered()
{
    if (confirm_reset) {
        QMessageBox questionbox(QMessageBox::Icon::Question, EMU_NAME, tr("Are you sure you want to hard reset the emulated machine?"), QMessageBox::Yes | QMessageBox::No, this);
        const auto chkbox    = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_reset);

        QObject::connect(chkbox, &QCheckBox::CHECK_STATE_CHANGED, [](int state) {
            confirm_reset = (state == Qt::CheckState::Unchecked);
        });
        questionbox.exec();
        if (questionbox.result() == QMessageBox::No) {
            confirm_reset = true;
            return;
        }
    }
    config_changed = 2;
    pc_reset_hard();
}

void
MainWindow::on_actionPause_triggered()
{
    plat_pause(dopause ^ 1);
}

void
MainWindow::on_actionExit_triggered()
{
    exiting_manually = 1;
    close();
}

void
MainWindow::emitVmmSignal()
{
    emit vmmConfigurationChanged();
}

/* PeepeeBox: which touchscreen is fitted, and its options.

   The cabinets are driven entirely by touch, so the part matters more here than
   any other peripheral -- and two of them turn up in the wild: the 3M MicroTouch
   these machines shipped with, and Elo SmartSet.  Both are ordinary 86Box tablet
   devices with their own config (the port, and for the MicroTouch the controller
   identity), so this dialog only has to pick one and hand the rest to the device
   config dialog that already knows how to draw it.

   Returns 1 when something changed and the machine has to be restarted for it --
   the device is bound to its serial port at init, so a live swap is not a thing. */
static int
pp_touchscreen_dialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Touchscreen"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *form  = new QFormLayout();
    auto *combo = new QComboBox();

    /* Built from the device table rather than a hardcoded pair, so a touchscreen
       added to 86Box later shows up here without anyone remembering to. */
    const char *current = photoplay_touchscreen();
    for (int i = 1; i < tablet_get_ndev(); i++) {
        const device_t *dev = tablet_get_device(i);

        if ((dev == NULL) || !(dev->flags & DEVICE_COM))
            continue;
        combo->addItem(QString::fromUtf8(dev->name), QString::fromUtf8(dev->internal_name));
        if (!strcmp(dev->internal_name, current))
            combo->setCurrentIndex(combo->count() - 1);
    }
    form->addRow(QObject::tr("Touchscreen:"), combo);

    /* The port, the interrupt and the line speed are all the device's own, under
       Options.  The interrupt used to have a second box here too, and the two could
       disagree; the Options one is the one the device obeys, so it is the one left.
       Its Automatic is photoplay_com3_irq(), which is what moves to 3 for fun.link. */
    auto *opts = new QPushButton(QObject::tr("&Options..."));
    form->addRow(QString(), opts);

    auto *note = new QLabel(QObject::tr(
        "The cabinets wired their touchscreen to COM3. A different port, or a "
        "controller the game does not expect, stops touch working with no error "
        "on screen."
        "\n\nThe interrupt is under Options. Automatic is 4, and 3 when fun.link "
        "is fitted. That is what the cabinets did: the link driver takes IRQ 4 for "
        "itself and does not hand it back, and funworld's service manual jumpers the "
        "touchscreen controller to I3 on a machine with an adapter."
        "\n\nChanging any of this restarts the machine."));
    note->setWordWrap(true);
    form->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *outer   = new QVBoxLayout(&dlg);
    outer->addLayout(form);
    outer->addWidget(buttons);

    /* Options are the chosen device's own, so the combo has to be applied before
       they can be shown -- otherwise picking Elo and pressing Options would
       configure the MicroTouch. */
    int inner_changed = 0;
    QObject::connect(opts, &QPushButton::clicked, [&]() {
        const int idx = tablet_get_from_internal_name(
            (char *) combo->currentData().toString().toUtf8().constData());

        if (idx > 0)
            inner_changed |= DeviceConfig::ConfigureDevice(tablet_get_device(idx), 0, &dlg);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return inner_changed;   /* the device dialog saves its own, Cancel here cannot undo it */

    int changed = inner_changed;

    const QString chosen = combo->currentData().toString();
    if (chosen != QString::fromUtf8(current)) {
        photoplay_set_touchscreen(chosen.toUtf8().constData());
        changed = 1;
    }
    return changed;
}

/* PeepeeBox: the modem, and whether there is one at all.

   The cabinets that were on fun.net had an external modem on COM4 -- the port
   the cabinet's own NET.CFG names, at 0x2E8 on IRQ 10, 57600 with hardware flow
   control.  Which modem is not a guess either: the disk carries funworld's modem
   database in \FN_SYS\DATABASE\NETWORK\, and the part this emulates is row 3 of
   it, a Diamond SupraExpress 56e PRO.  See docs/research/33-modem.md.

   Most cabinets never had one and no game needs one, so unlike the touchscreen
   this is a part that is fitted rather than a part that is -- hence None as the
   first entry and the default.  Everything past that choice belongs to the
   device, so Options hands off to its own config dialog the way the touchscreen
   does.

   Returns 1 when something changed and the machine has to be restarted for it:
   fitting the modem creates COM4, which is emulated hardware appearing. */
static int
pp_modem_dialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Modem"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const QString current = QString::fromUtf8(photoplay_modem());

    auto *form  = new QFormLayout();
    auto *combo = new QComboBox();

    /* None first, and the default: most cabinets never had one. */
    combo->addItem(QObject::tr("None"), QString());
    for (int i = 0; photoplay_modem_list(i) != nullptr; i++) {
        const char *internal = photoplay_modem_list(i);
        const int   dev_id   = char_get_from_internal_name(internal, DEVICE_COM);

        if (dev_id <= 0)
            continue;
        combo->addItem(QString::fromUtf8(char_get_device(dev_id)->name),
                       QString::fromUtf8(internal));
        if (current == QString::fromUtf8(internal))
            combo->setCurrentIndex(combo->count() - 1);
    }
    form->addRow(QObject::tr("Modem on COM4:"), combo);

    auto *opts = new QPushButton(QObject::tr("&Options..."));
    opts->setEnabled(!combo->currentData().toString().isEmpty());
    form->addRow(QString(), opts);

    auto *note = new QLabel(QObject::tr(
        "The cabinets that were on fun.net had an external modem on COM4, at "
        "0x2E8 on IRQ 10 — one of these two parts. Fitting one lets the operator "
        "menu's data transmission find it and report what it is; the line behind "
        "it is dead unless a host is set under Options."
        "\n\nChanging any of this restarts the machine."));
    note->setWordWrap(true);
    form->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *outer   = new QVBoxLayout(&dlg);
    outer->addLayout(form);
    outer->addWidget(buttons);

    /* Options are the chosen part's own, so the combo has to be read at click
       time -- otherwise picking the ELSA and pressing Options would configure
       the Supra.  Same reasoning as the touchscreen dialog above. */
    int inner_changed = 0;
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [opts, combo](int) {
                         opts->setEnabled(!combo->currentData().toString().isEmpty());
                     });
    QObject::connect(opts, &QPushButton::clicked, [&]() {
        const int dev_id = char_get_from_internal_name(
            combo->currentData().toString().toUtf8().constData(), DEVICE_COM);

        /* Instance, not 0 -- see the fun.link dialog below. COM4 is instance 4. */
        if (dev_id > 0)
            inner_changed |= DeviceConfig::ConfigureDevice(char_get_device(dev_id),
                                                           PHOTOPLAY_MODEM_PORT + 1, &dlg);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return inner_changed;   /* the device dialog saves its own, Cancel here cannot undo it */

    const QString chosen = combo->currentData().toString();
    if (chosen != current) {
        photoplay_set_modem(chosen.toUtf8().constData());
        return 1;
    }
    return inner_changed;
}

/* PeepeeBox: fun.link, the adapter that joins cabinets together.

   funworld's own advert for it draws four machines in a ring -- the games' own
   table holds sixteen -- and the box has a 25-pin plug to the cabinet's I/O
   connector, a DIN for power and a pair of jack sockets for the bus itself; but
   it is a *serial* bus, not a parallel one, whatever the 25-pin end suggests.
   The games say so: "LINK ERROR: No serial-port found !!!!. Please check
   mainboard COM-settings".  COM1 at 0x3F8 / IRQ 4, 115200 8N1.  See
   docs/research/34-funlink.md.

   Fitting it is the only thing this dialog decides; where the bus is and who
   hosts it belongs to the device, so Options hands off to its own config the way
   the modem and touchscreen dialogs do.

   Returns 1 when something changed and the machine has to be restarted for it:
   fitting the adapter is a device appearing on COM1. */
static int
pp_funlink_dialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("fun.link"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const bool current = photoplay_funlink_enabled();

    auto *form  = new QFormLayout();
    auto *fitted = new QCheckBox(QObject::tr("fun.link adapter fitted to COM1"));
    fitted->setChecked(current);
    form->addRow(fitted);

    auto *opts = new QPushButton(QObject::tr("&Options..."));
    opts->setEnabled(current);
    form->addRow(QString(), opts);

    auto *note = new QLabel(QObject::tr(
        "fun.link joins cabinets into one bus so their games can play each "
        "other. Every machine on the bus is its own PeepeeBox — a second copy "
        "beside this one, or one on another PC. Leave both on the default "
        "under Options and whichever starts first hosts the bus; to link over a "
        "network, set the others to the host's address."
        "\n\nUp to sixteen cabinets share one bus. Automatic numbering only "
        "covers a pair, so from a third machine on give each its own Station "
        "number under Options."
        "\n\nOnly the 1998/99, 2000 and 2001 releases can start a linked "
        "game. I.G.O. 1 and 2 still carry the serial driver, but their menu "
        "never launches a game in link mode, and I.G.O. 3 onward has neither "
        "— on those, fitting the adapter does nothing."
        "\n\nChanging this restarts the machine."));
    note->setWordWrap(true);
    form->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *outer   = new QVBoxLayout(&dlg);
    outer->addLayout(form);
    outer->addWidget(buttons);

    int inner_changed = 0;
    QObject::connect(fitted, &QCheckBox::toggled,
                     [opts](bool on) { opts->setEnabled(on); });
    QObject::connect(opts, &QPushButton::clicked, [&]() {
        const int dev_id = char_get_from_internal_name(PHOTOPLAY_FUNLINK, DEVICE_COM);

        /* Instance, not 0: serial.c builds the port's device with char_init(..,
           port + 1), so its options live in "<name> #1" for COM1.  Configuring
           instance 0 writes a section the device never reads. */
        if (dev_id > 0)
            inner_changed |= DeviceConfig::ConfigureDevice(char_get_device(dev_id),
                                                           PHOTOPLAY_FUNLINK_PORT + 1, &dlg);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return inner_changed;   /* the device dialog saves its own, Cancel here cannot undo it */

    if (fitted->isChecked() != current) {
        photoplay_set_funlink_enabled(fitted->isChecked());
        return 1;
    }
    return inner_changed;
}

/* PeepeeBox: what used to open the eleven-page machine settings dialog now opens
   the dongle.  Every other setting that dialog offered -- machine, CPU, RAM,
   video, sound, input, ports, drives -- follows from the cabinet (see
   src/photoplay.c) and is not a choice.

   The dongle is the exception, and it genuinely has to stay adjustable: the
   version banner it reports must match MAIN.SET["Version"] for the image being
   run, and that differs per image and per territory.  Get it wrong and the game
   reports "Wrong Version".  Those settings belong to the device, so Options
   hands off to its own config dialog.

   What this dialog adds is the machine licence the dongle presents -- the
   iButton serial FN_SYS.EXE turns into "machlic", which is how a fun.net
   server tells one cabinet from another (photoplay_machlic()).  Left at the
   old fixed default, every image on every PeepeeBox is the same cabinet to a
   server; "Random from the pool" draws one of the licences every fun.net
   stand-in registers in advance.

   Returns 1 when something changed and the machine has to be restarted for it. */
static QString
pp_machlic_where(const QString &lic)
{
    if (lic.length() != 12)
        return QObject::tr("Twelve hex digits (0-9, A-F).");
    if (!lic.compare(QStringLiteral(PHOTOPLAY_MACHLIC_DEFAULT), Qt::CaseInsensitive))
        return QObject::tr("The PeepeeBox default. Every image left on it is the same "
                           "cabinet to a fun.net server.");
    const int idx = photoplay_machlic_pool_index(lic.toLatin1().constData());
    if (idx >= 0)
        return QObject::tr("Entry %1 of the shared pool. fun.net servers register every pool "
                           "entry in advance, so it is accepted on its first call.").arg(idx);
    return QObject::tr("A licence of its own (not from the pool). A fun.net server has to be "
                       "told about it before it is accepted.");
}

static QString
pp_machlic_serial(const QString &lic)
{
    if (lic.length() != 12)
        return QString();
    QStringList bytes;
    for (int i = 5; i >= 0; i--)
        bytes << lic.mid(2 * i, 2).toUpper();
    return bytes.join(QLatin1Char(' '));
}

static int
pp_dongle_dialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Dongle"));
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const QString current = QString::fromLatin1(photoplay_machlic()).toUpper();

    auto *form = new QFormLayout();
    auto *lic  = new QLineEdit(current);
    lic->setMaxLength(12);
    lic->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,12}")), lic));
    lic->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lic->setMinimumWidth(lic->fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMMMMMMMM")));

    auto *random = new QPushButton(QObject::tr("&Random from the pool"));
    auto *reset  = new QPushButton(QObject::tr("&Default"));
    auto *row    = new QHBoxLayout();
    row->addWidget(lic, 1);
    row->addWidget(random);
    row->addWidget(reset);
    form->addRow(QObject::tr("Machine licence:"), row);

    auto *serial = new QLabel();
    serial->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    serial->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QObject::tr("Dongle serial:"), serial);

    auto *where = new QLabel();
    where->setWordWrap(true);
    form->addRow(QString(), where);

    auto *opts = new QPushButton(QObject::tr("Version and territory &Options..."));
    form->addRow(QObject::tr("Dongle:"), opts);

    auto *note = new QLabel(QObject::tr(
        "The machine licence (machlic) is how a fun.net server tells this cabinet from every "
        "other one: its scripts, its games, its mail and the acknowledgement of its uploads "
        "all go by it. Give each image its own.\n\n"
        "The cabinet software treats a new licence as a new machine: its technical data and "
        "counters start again, and a fun.net server sees a cabinet it has not met.\n\n"
        "Changing this restarts the machine."));
    note->setWordWrap(true);
    form->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *outer   = new QVBoxLayout(&dlg);
    outer->addLayout(form);
    outer->addWidget(buttons);

    auto refresh = [&]() {
        const QString v = lic->text().toUpper();
        where->setText(pp_machlic_where(v));
        serial->setText(v.length() == 12 ? pp_machlic_serial(v) : QStringLiteral("—"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(v.length() == 12);
    };
    QObject::connect(lic, &QLineEdit::textChanged, [&](const QString &) { refresh(); });
    QObject::connect(random, &QPushButton::clicked, [&]() {
        char entry[13];
        do
            photoplay_machlic_pool((int) QRandomGenerator::global()->bounded(PHOTOPLAY_MACHLIC_POOL), entry);
        while (!lic->text().compare(QLatin1String(entry), Qt::CaseInsensitive));
        lic->setText(QLatin1String(entry));
    });
    QObject::connect(reset, &QPushButton::clicked, [&]() { lic->setText(QStringLiteral(PHOTOPLAY_MACHLIC_DEFAULT)); });

    int inner_changed = 0;
    QObject::connect(opts, &QPushButton::clicked, [&]() {
        inner_changed |= DeviceConfig::ConfigureDevice(&lpt_dongle_photoplay_device, 0, &dlg);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    refresh();

    if (dlg.exec() != QDialog::Accepted)
        return inner_changed;   /* the device dialog saves its own, Cancel here cannot undo it */

    const QString chosen = lic->text().toUpper();
    if ((chosen.length() == 12) && (chosen != current)) {
        photoplay_set_machlic(chosen.toLatin1().constData());
        return 1;
    }
    return inner_changed;
}

void
MainWindow::on_actionSettings_triggered()
{
    const int currentPause = dopause;

    plat_pause(1);
    if (pp_dongle_dialog(this)) {
        config_changed = 2;
        config_save();
        pc_reset_hard();
    }
    plat_pause(currentPause);
}

/* The window is named after what the disk says it is, so this has to be
   rebuilt whenever the disk changes under the machine. */
void
MainWindow::updateWindowTitle()
{
    QString vmname(vm_name);

    if (vmname.isEmpty())
        vmname = QString(EMU_NAME);
    else if ((vmname.at(vmname.size() - 1) == '"') || (vmname.at(vmname.size() - 1) == '\''))
        vmname.truncate(vmname.size() - 1);

    /* The fork release, then the commit the build came from after a dash.  Several
       builds are usually in flight at once across the rig folders, so the title has to
       say which one is on screen; the log header repeats it. */
    QString title = QString("%1 - %2 %3").arg(vmname, EMU_NAME, PEEPEEBOX_RELEASE);
#ifdef EMU_GIT_HASH
    title += QString(" - %1").arg(EMU_GIT_HASH);
#endif
    this->setWindowTitle(title);
}

void
MainWindow::on_actionHDD_manager_triggered()
{
    HddManager manager(this);

    manager.setWindowModality(Qt::WindowModal);

    if ((manager.exec() != QDialog::Accepted) || manager.selectedImage().isEmpty())
        return;

    /* Make the choice the disk and restart the cabinet on it.  The profile is
       re-stamped from pc_reset_hard_init(), so the reset is what mounts the
       new image.  Nothing is saved: the pick is for this run. */
    const QByteArray image = manager.selectedImage().toUtf8();

    plat_pause(1);

    photoplay_set_selected_image(image.constData());

    /* Name the machine now rather than waiting for the reset to do it.
       pc_reset_hard() only raises a flag -- the emulation thread does the work
       later -- so vm_name still describes the previous disk at this point, and
       the title came out as whatever the working directory is called. */
    char ident[96] = { 0 };

    photoplay_image_label(image.constData(), ident, sizeof(ident));

    if (ident[0] != '\0') {
        strncpy(vm_name, ident, sizeof(vm_name) - 1);
        vm_name[sizeof(vm_name) - 1] = '\0';
    }

    pc_reset_hard();

    /* Loading an image means running it.  Restoring the previous pause state
       would leave it stopped in the case that matters most -- a machine that
       came up with no disk at all and is being given one. */
    plat_pause(0);

    updateWindowTitle();
    refreshMediaMenu();
}

/* PeepeeBox: the cabinet's own controls -- the coin slot and the two buttons
   behind the door -- on the toolbar.

   These do not type.  The keyboard has shortcuts that reach some of the same
   places (S opens the operator setup from the menu), but they are not the same
   thing: C is a credit on a game's start page and the CRC check on the menu, so
   a button that typed one would do the wrong thing depending on where the guest
   happened to be.  These drive the wires instead, through the funworld I/O card
   -- an 8255 at 0x210, which is where a boot with PEEPEEBOX_IO_PROBE caught the
   software configuring one.  See src/device/funworld_io.c.

   A coin is a validator accept line held for 100 ms, because that is what the
   part does and what the software is required to debounce.  The device owns that
   timing; these only say which line.

   Ten buttons, because the cabinet takes ten kinds of money on ten separate
   wires -- six coins from the C120 and four notes from the bill validator beside
   it -- and there is no line that means "money" in general.  They are numbered
   by channel rather than by value: the channel is the wiring and is fixed, while
   what each one is worth is whatever the operator setup on that image has been
   programmed to.  The values in the labels are the ones this I.G.O. 6 image
   uses. */
static void
insert_money(MainWindow *win, int line)
{
    funworld_io_pulse(line);

    /* Under PEEPEEBOX_IO_WALK coin 1 is not a coin at all -- it steps through
       the card's lines one per click.  Which line that was has to be on screen:
       counting clicks against a comment in a batch file is exactly the
       bookkeeping that produces a confident wrong answer, and whoever is
       clicking is watching the guest, not the log. */
    char what[48];

    if (funworld_io_walk_state(what, sizeof(what))) {
        static QLabel *walk = nullptr;

        if (walk == nullptr) {
            walk = new QLabel(win, Qt::Tool | Qt::WindowStaysOnTopHint);
            walk->setWindowTitle(MainWindow::tr("I/O card walk"));
            walk->setAlignment(Qt::AlignCenter);
            walk->setMargin(18);

            QFont font = walk->font();
            font.setPointSize(font.pointSize() + 8);
            font.setBold(true);
            walk->setFont(font);
        }

        walk->setText(QString::fromUtf8(what));
        walk->adjustSize();
        walk->show();
        walk->raise();
    }
}

void
MainWindow::on_actionInsert_coin_1_triggered()
{
    insert_money(this, FWIO_LINE_COIN1);
}

void
MainWindow::on_actionInsert_coin_2_triggered()
{
    insert_money(this, FWIO_LINE_COIN2);
}

void
MainWindow::on_actionInsert_coin_3_triggered()
{
    insert_money(this, FWIO_LINE_COIN3);
}

void
MainWindow::on_actionInsert_coin_4_triggered()
{
    insert_money(this, FWIO_LINE_COIN4);
}

void
MainWindow::on_actionInsert_coin_5_triggered()
{
    insert_money(this, FWIO_LINE_COIN5);
}

void
MainWindow::on_actionInsert_coin_6_triggered()
{
    insert_money(this, FWIO_LINE_COIN6);
}

void
MainWindow::on_actionInsert_note_1_triggered()
{
    insert_money(this, FWIO_LINE_NOTE1);
}

void
MainWindow::on_actionInsert_note_2_triggered()
{
    insert_money(this, FWIO_LINE_NOTE2);
}

void
MainWindow::on_actionInsert_note_3_triggered()
{
    insert_money(this, FWIO_LINE_NOTE3);
}

void
MainWindow::on_actionInsert_note_4_triggered()
{
    insert_money(this, FWIO_LINE_NOTE4);
}


/* PeepeeBox: what the cabinet's printer would be printing.

   Two panes, and the lower one is the point.  Until the command set is known
   the interesting half of a receipt is not its text but the control sequences
   that produced it, and a sequence this build does not recognise has to be
   visible rather than quietly dropped -- the whole device is built to find out
   what the printer is, not to pretend it already knows.  See
   src/device/prn_cp80.c. */
static QDialog *cp80_win    = nullptr;
static QLabel  *cp80_view   = nullptr;   /* the machine, with the roll drawn on */
static QPixmap *cp80_body   = nullptr;   /* the photograph, scaled once         */
static QString  cp80_queued;             /* printed by the guest, not yet fed   */
static QString  cp80_printed;            /* what is on the paper                */
static QTimer  *cp80_feed   = nullptr;
static size_t   cp80_paper_at = 0;
static QWidget *cp80_controls = nullptr;
static QLabel  *cp80_status   = nullptr;
static QVBoxLayout *cp80_layout = nullptr;
static QRect    cp80_available_hint;

/* The Photo Play protocol identifies a DATAPRINT controller, not the print
   mechanism behind it.  Keep the proven serial emulation common and let the
   operator choose which documented printer is sitting beside the cabinet. */
enum class Cp80PrinterModel {
    Dpu414 = 0,
    Dataprint3000
};

static Cp80PrinterModel cp80_model = Cp80PrinterModel::Dpu414;
static bool              cp80_model_initialised = false;
static QPushButton      *cp80_link_btn = nullptr;
static QPushButton      *cp80_feed_btn = nullptr;
static QPushButton      *cp80_paper_btn = nullptr;
static QPushButton      *dp3000_wp_btn = nullptr;

/* The 3000 itself deliberately has almost no front-panel controls.  Its two
   local switches are on the case edge and configuration is done with the
   detachable three-key keyboard shown in the manual.  These transparent
   buttons sit on the painted hardware, so the simulator is operated in the
   same places as the real unit rather than through invented front buttons. */
static QPushButton *dp3000_case_feed = nullptr;
static QPushButton *dp3000_case_reset = nullptr;
static QPushButton *dp3000_key_yes = nullptr;
static QPushButton *dp3000_key_init = nullptr;
static QPushButton *dp3000_key_no = nullptr;
static QPushButton *dp3000_card = nullptr;

enum class Dp3000KeyboardState {
    Idle = 0,
    PrintMaximal,
    PrintMedium,
    PrintShort,
    PrintBag,
    PrintCustom,
    DeleteConfirm,
    DeleteAgain,
    RecoverConfirm,
    InitialiseConfirm,
    InitialiseAgain,
    SettingsShowCurrent,
    SettingsOperatingBlock,
    SettingsEvaluationBlock,
    SettingsParameter,
    SettingsEdit
};

enum class Dp3000PrintFormat {
    Maximum = 0,
    Medium,
    Short,
    CashBag,
    Custom
};

enum class Dp3000Setting {
    DateTime = 0,
    DataprintNumber,
    CardNumber,
    StorageMode,
    FirstPrintLength,
    FirstCustom,
    SecondPrintLength,
    SecondCustom,
    CashReceipt,
    WithoutCard,
    PrintNumbers,
    EvaluationEnabled,
    OverwriteMachineSettings,
    PcBaud,
    KeyCode,
    EvaluationType,
    DeleteMachineData,
    Statistics,
    Copy,
    List,
    Control,
    DeleteAdpCode,
    ClockSetter,
    Vat,
    Count
};

struct Dp3000Settings {
    /* The real unit's clock keeps running while the emulator is closed.  Store
       its offset from the host clock instead of freezing an absolute display
       value at the instant the operator leaves the settings walk. */
    qint64 clock_offset_secs = 0;
    int dataprint_number = 0;
    int card_number = 0;
    int storage_mode = true;
    int first_print_length = 0;
    int first_custom = false;
    int second_print_length = 5;
    int second_custom = false;
    int cash_receipt = true;
    int without_card = false;
    int print_numbers = false;
    int evaluation_enabled = true;
    int overwrite_machine_settings = false;
    int pc_baud = 2;
    QString key_code = QStringLiteral("01010101");
    int evaluation_type = 5;
    int delete_machine_data = true;
    int statistics = true;
    int copy = true;
    int list = true;
    int control = false;
    int delete_adp_code = false;
    int clock_setter = false;
    int vat_half_percent = 32;
};

static Dp3000KeyboardState dp3000_keyboard_state = Dp3000KeyboardState::Idle;
static QStringList dp3000_records;
static QStringList dp3000_deleted_records;
static QStringList dp3000_print_job_records;
static int dp3000_print_job_record = -1;
static Dp3000PrintFormat dp3000_print_format = Dp3000PrintFormat::Maximum;
static uint64_t dp3000_transfer_serial_seen = 0;
static uint64_t dp3000_transfer_completed_seen = 0;
static uint64_t dp3000_discard_transfer_serial = 0;
static uint64_t dp3000_no_card_notified_serial = 0;
static uint64_t dp3000_wp_notified_serial = 0;
static uint32_t dp3000_dataset_number = 0;
static uint64_t dp3000_pending_serial = 0;
static QString  dp3000_pending_record;
static bool dp3000_keyboard_connected = true;
static bool dp3000_keyboard_job = false;
static bool dp3000_keyboard_paused = false;
static bool dp3000_card_inserted = true;
static bool dp3000_card_write_protected = false;
static bool dp3000_memory_fault = false;
static bool dp3000_memory_full = false;
static bool dp3000_transfer_error = false;
static bool dp3000_complete_signal = false;
static unsigned dp3000_job_generation = 0;
static bool dp3000_paper_loaded = true;
static int  dp3000_paper_lines = 12000;
static bool dp3000_paper_restart_required = false;
static bool dp3000_store_initialised = false;
static bool dp3000_english = false;
static bool dp3000_abort_pending = false;
static bool dp3000_plug_error_pending = false;
static bool dp3000_pause_requested = false;
static bool dp3000_print_press_handled = false;
static int dp3000_setting_index = 0;
static int dp3000_edit_subindex = 0;
static Dp3000Settings dp3000_settings;
static QTimer *dp3000_input_timeout = nullptr;
static bool dp3000_input_timeout_requested = false;

static QDateTime
dp3000_clock_now()
{
    return QDateTime::currentDateTime().addSecs(
        dp3000_settings.clock_offset_secs);
}

static void
dp3000_clock_set(const QDateTime &value)
{
    dp3000_settings.clock_offset_secs =
        QDateTime::currentDateTime().secsTo(value);
}

/* Keep every translatable physical legend and firmware string in one table so
   the English option cannot leave a German warning hidden in a less common
   confirmation path.  Firmware dialogue stays within the printer's
   24-character lines. */
enum class Dp3000Text {
    KeyYes = 0,
    KeyPrint,
    KeyNo,
    KeyDelete,
    StatusPowerOn,
    StatusBatteryEmpty,
    StatusToMachine,
    StatusPcMachineError,
    StatusPaperEnd,
    StatusToPc,
    StatusMemoryError,
    StatusCharging,
    CardTop,
    YesNo,
    NoCard,
    NoData,
    PrintMaximum,
    PrintMedium,
    PrintShort,
    PrintBag,
    PrintCustom,
    PressDeleteAgain,
    InitialiseConfirm,
    InitialiseWarning,
    Initialised,
    DataDeleted,
    NoMemoryCard,
    BufferAvailable,
    MemoryAvailable,
    DataRestored,
    Cancelled,
    RestoreDeleted,
    DataStoredDelete,
    Count
};

static QString
dp3000_text(Dp3000Text id)
{
    static const char *const german[] = {
        "ja",
        "drucken",
        "nein",
        "löschen",
        "GERÄT EIN",
        "BATTERIE LEER",
        "ZUM AUTOMAT",
        "FEHLER PC/AUTOMAT",
        "PAPIER ENDE",
        "ZUM PC",
        "SPEICHER FEHLER",
        "LADEN",
        "OBEN/TOP   256 kB",
        "...JA/NEIN",
        "ES IST KEINE SPEICHER-\nKARTE EINGESTECKT!",
        "KEINE DATEN GESPEICHERT",
        "MAXIMALER AUSDRUCK?",
        "MITTELLANGER AUSDRUCK?",
        "KURZER AUSDRUCK?",
        "GELDSACKBELEG?",
        "SELBSTDEF. AUSDRUCK?",
        "NOCHMALS LOESCHEN-TASTE",
        "INITIALISIEREN?\n...JA/NEIN",
        "DIE EINSTELLUNGEN WERDEN\n"
        "AUF WERKSEINSTELLUNGEN\n"
        "ZURUECKGESETZT UND DIE\n"
        "DATEN WERDEN GELOESCHT!\n"
        "WIRKLICH INITIALISIEREN?\n"
        "...JA/NEIN",
        "DATAPRINT WURDE NEU\n"
        "INITIALISIERT!\n"
        "DIE EINSTELLUNGEN WURDEN\n"
        "AUF WERKSEINSTELLUNG\n"
        "ZURUECKGESETZT!",
        "DIE DATEN SIND GELOESCHT",
        "KEINE SPEICHERKARTE",
        "  8KB PUFFER VORHANDEN",
        "256KB SPEICHER VORHANDEN",
        "DIE DATEN SIND WIEDER-\nHERGESTELLT",
        "VORGANG ABGEBROCHEN",
        "GELOESCHTE DATEN WIEDER-\nHERSTELLEN?\n...JA/NEIN",
        "DATEN SIND GESPEICHERT!\n"
        "WIRKLICH LOESCHEN?\n"
        "...JA/NEIN"
    };
    static const char *const english[] = {
        "yes",
        "print",
        "no",
        "delete",
        "POWER ON",
        "BATTERY EMPTY",
        "TO MACHINE",
        "PC/MACHINE ERROR",
        "OUT OF PAPER",
        "TO PC",
        "MEMORY ERROR",
        "CHARGING",
        "TOP   256 kB",
        "...YES/NO",
        "NO MEMORY CARD IS\nINSERTED!",
        "NO DATA STORED",
        "MAXIMUM PRINTOUT?",
        "MEDIUM-LENGTH PRINTOUT?",
        "SHORT PRINTOUT?",
        "CASH-BAG RECEIPT?",
        "CUSTOM PRINTOUT?",
        "PRESS DELETE AGAIN",
        "INITIALIZE?\n...YES/NO",
        "SETTINGS WILL BE RESET\n"
        "TO FACTORY DEFAULTS AND\n"
        "DATA WILL BE DELETED!\n"
        "REALLY INITIALIZE?\n"
        "...YES/NO",
        "DATAPRINT WAS\n"
        "INITIALIZED!\n"
        "SETTINGS WERE RESET TO\n"
        "FACTORY DEFAULTS!",
        "DATA HAS BEEN DELETED",
        "NO MEMORY CARD",
        "  8KB BUFFER AVAILABLE",
        "256KB MEMORY AVAILABLE",
        "DATA HAS BEEN RESTORED",
        "OPERATION CANCELLED",
        "RESTORE DELETED DATA?\n...YES/NO",
        "DATA IS STORED!\n"
        "REALLY DELETE?\n"
        "...YES/NO"
    };
    static_assert(sizeof(german) / sizeof(german[0])
                      == static_cast<size_t>(Dp3000Text::Count));
    static_assert(sizeof(english) / sizeof(english[0])
                      == static_cast<size_t>(Dp3000Text::Count));

    const size_t index = static_cast<size_t>(id);
    return QString::fromUtf8((dp3000_english ? english : german)[index]);
}

/* A torn receipt remains a visible object long enough to leave the serrated
   bar instead of vanishing on the button click.  Its text and historical ink
   levels are copied before the live roll is cleared, so newly printed data can
   appear behind it without becoming part of the departing sheet. */
static QTimer          *cp80_tear_timer = nullptr;
static QPushButton     *cp80_tear_btn   = nullptr;
static QString          cp80_torn_text;
static QVector<double>  cp80_torn_batt;
static int              cp80_torn_first = 0;
static int              cp80_tear_frame = 0;
static bool             cp80_tearing    = false;

/* The paper moves continuously during the roughly 200 ms platen advance.  The
   printer itself remains anchored while the new line emerges from the slot. */
#define CP80_PAPER_FRAME_MS 16
#define CP80_PAPER_FRAMES   13
#define CP80_SCREEN_MARGIN   0
#define CP80_PAPER_PAD_Y    12
#define CP80_PAPER_EDGE_ROOM 5
#define CP80_EMPTY_LIP_H    14
#define CP80_CRUMPLE_MIN_H  16
#define CP80_CRUMPLE_MAX_H  38
#define CP80_HOME_DELAY_MS  1100

static QTimer *cp80_paper_motion = nullptr;
static int     cp80_paper_frame  = CP80_PAPER_FRAMES;
static QTimer *cp80_home_timer     = nullptr;
static QTimer *cp80_manual_feed    = nullptr;
static unsigned cp80_last_head_columns = 0;
static unsigned cp80_last_head_speed   = 1000;
static bool     cp80_head_away         = false;
static unsigned cp80_edge_generation  = 0;

/* The serial link can fill the 28 KB buffer far faster than the mechanism can
   empty it.  This is a moving-head DPU-414, not a stationary line-thermal
   printer: SII rates normal text at at most 52.5 characters/second.  The head
   uses eight horizontal steps per cell (seven dots and a space), matching the
   401--422 Hz motor tone in the reference recordings.  A line also advances
   nine character dots plus the default six-dot line spacing.  Optical tracking
   and the isolated 150 Hz feed pulse train in the clean portable-printer video
   put an ordinary fifteen-dot advance at about 0.20 seconds. */
#define CP80_CHAR_CPS        52.5
#define CP80_PAPER_MS       200.0
#define CP80_FIRST_LINE_MS  400

/* 30 frames at 16 ms is 480 ms: about 320 ms for a diagonal pull across the
   documented 112 mm paper width, then 160 ms for the released sheet to flex
   and settle. */
#define CP80_TEAR_FRAME_MS 16
#define CP80_TEAR_FRAMES   30
#define CP80_UI_PI 3.14159265358979323846

/* The DPU-414 is a top-exit printer: the paper rises out of the slot on top of
   the machine and lies over the lid.

   This is painted rather than laid out.  Two attempts at overlapping a picture
   and a text widget both came out with the machine clipped and the paper in the
   wrong place -- a layout that is asked to put one child on top of another at a
   fixed offset is a layout being used as a canvas.  Drawing it means the paper
   is exactly where the slot is, the roll can extend past the top of the
   photograph, and clipping the oldest lines is a subtraction rather than a
   scrollbar.

   Everything here is measured off the image rather than judged by eye: it is
   510 x 435, the paper in the slot runs x=122..436 at y=176, the two panel
   buttons and the two LEDs are where the scan below says they are.  The drawn
   roll takes the same width and the same colour as the paper already in the
   photograph, so the two are one piece of paper. */
#define CP80_IMG_W    510
#define CP80_IMG_H    435
#define CP80_SLOT_Y   176
#define CP80_SLOT_L   122
#define CP80_SLOT_R   436

/* The panel, in image coordinates: the square ON LINE button, the wide FEED
   button, and the two LEDs left of them -- OFF LINE above, ON LINE below. */
#define CP80_BTN_ON_X   69
#define CP80_BTN_ON_W   45
#define CP80_BTN_FD_X  135
#define CP80_BTN_FD_W   73
#define CP80_BTN_Y     317
#define CP80_BTN_H      40

/* The lamps light *inside* their housings.  Those measure x 46..61 by y 318..325
   and x 45..60 by y 344..351 in the photograph; these are inset by a pixel so the
   dark rim survives and the lamp does not sit on top of it. */
#define CP80_LED_OFF_X  47
#define CP80_LED_OFF_Y 319
#define CP80_LED_ON_X   46
#define CP80_LED_ON_Y  345
#define CP80_LED_W      14
#define CP80_LED_H       6

/* The Power LED is not on the panel: it is a lens in the front edge, below and
   left of the buttons.  The dark bar measures x 41..66 by y 411..414 -- four
   pixels tall, which is why it is given as edges rather than as an origin and a
   size.  CP80_SCALE(x) + CP80_SCALE(w) rounds twice and the second rounding
   pushed the lit bar outside a lens this thin; differencing two scaled edges
   rounds once and stays inside it.

   The Power *switch* is on the left-hand side of the machine and so is not in
   this photograph at all, which is why it is a labelled button in the row below
   rather than an invisible one on the picture. */
#define CP80_LED_PWR_X0  42
#define CP80_LED_PWR_X1  65
#define CP80_LED_PWR_Y0 411
#define CP80_LED_PWR_Y1 415

#define CP80_HEAD_W   460
#define CP80_SCALE(v) (((v) * CP80_HEAD_W) / CP80_IMG_W)
#define CP80_HEAD_H   CP80_SCALE(CP80_IMG_H)
#define CP80_PAPER_W  CP80_SCALE(CP80_SLOT_R - CP80_SLOT_L)
#define CP80_PAPER_L  CP80_SCALE(CP80_SLOT_L)
#define CP80_SLOT_YS  CP80_SCALE(CP80_SLOT_Y)

/* DATAprint 3000 presentation geometry.  Three logical UI pixels represent one
   millimetre.  Consequently the documented 113 x 230 mm case is 339 x 690
   logical pixels and its 57 mm roll is 171 pixels wide.  Qt applies the current
   Windows display scale to those dimensions, just as it does to the dialog's
   native controls. */
#define DP3000_PX_PER_MM       3
#define DP3000_HEAD_W        760
#define DP3000_HEAD_H        720
#define DP3000_BODY_X        395
#define DP3000_BODY_Y          8
#define DP3000_BODY_W        (113 * DP3000_PX_PER_MM)
#define DP3000_BODY_H        (230 * DP3000_PX_PER_MM)
#define DP3000_SLOT_Y        (DP3000_BODY_Y + 535)
#define DP3000_PAPER_W       (57 * DP3000_PX_PER_MM)
#define DP3000_PAPER_L       (DP3000_BODY_X + ((DP3000_BODY_W - DP3000_PAPER_W) / 2))
#define DP3000_TEXT_LINE_H    10
#define DP3000_TEXT_CELL_W     6.6
#define DP3000_TEXT_FONT_PX     9
#define DP3000_COLUMNS        24
#define DP3000_LINE_MS      1429 /* Epson M-160: 0.7 line/second */
#define DP3000_FEED_MS       429 /* three 7 Hz blank dot-line advances */
#define DP3000_BATT_LINES  12000 /* about one 40 m roll at 3.3 mm/line */
#define DP3000_CHARGE_MINS   840 /* DATAprint 3000 manual: 14 hours */
#define DP3000_STORE_MAX  (256 * 1024)

static int
cp80_machine_height()
{
    return (cp80_model == Cp80PrinterModel::Dataprint3000)
         ? DP3000_HEAD_H : CP80_HEAD_H;
}

static int
cp80_machine_width()
{
    return (cp80_model == Cp80PrinterModel::Dataprint3000)
         ? DP3000_HEAD_W : CP80_HEAD_W;
}

/* The manual gives enough dimensions to keep the print geometry independent of
   whichever fixed-pitch UI font happens to be installed.  The 112 mm roll is
   400 printer dots wide at the specified 0.28 mm pitch.  The head covers the
   centred 320 dots: forty 8-dot cells, each a 7-dot glyph plus one blank dot.
   Vertically a normal line is the 9-dot matrix plus the default 6-dot feed.
   The final 1.2x visual scale is a deliberate concession to screen reading;
   it enlarges both axes together and recentres all forty columns. */
#define CP80_PAPER_DOTS       400.0
#define CP80_PRINT_DOTS       320.0
#define CP80_TEXT_CELL_DOTS     8.0
#define CP80_TEXT_LINE_DOTS    15.0
#define CP80_TEXT_VISUAL_SCALE   1.2
#define CP80_TEXT_FONT_PX       11
static constexpr double CP80_TEXT_CELL_W = (double) CP80_PAPER_W
    * CP80_TEXT_CELL_DOTS / CP80_PAPER_DOTS * CP80_TEXT_VISUAL_SCALE;
static constexpr int CP80_TEXT_LINE_H = (int) (((double) CP80_PAPER_W
    * CP80_TEXT_LINE_DOTS / CP80_PAPER_DOTS * CP80_TEXT_VISUAL_SCALE) + 0.5);
static constexpr int CP80_TEXT_MARGIN_X = (int) ((((double) CP80_PAPER_W
    - ((CP80_PRINT_DOTS / CP80_TEXT_CELL_DOTS) * CP80_TEXT_CELL_W)) / 2.0)
    + 0.5);

/* Sampled out of the photograph, so the drawn roll and the real one match. */
#define CP80_PAPER_RGB 227, 228, 236
#define CP80_INK_RGB    34,  34,  40

/* The battery pack.

   A **BP-4005-E**: Ni-MH, 4.8 V, about 120 g, and good for **3000 lines** on a
   charge (manual sections 2.10 and 6.1).  So a line costs 100/3000 of a pack,
   and that number is the manual's rather than a guess.

   A thermal head on a tired pack does not stop.  It prints fainter and slower
   until the paper comes out blank and nobody notices for a page, which is the
   failure worth being able to see.

   The two thresholds are for *testing* and are deliberately kind -- fading from
   90% and blank by 60% puts the whole arc inside a few reports where a real pack
   would fade far later and far lower.  PEEPEEBOX_PRN_DRAIN=<percent per line>
   makes it quicker again.

   Charging is the manual's too: about **ten hours** from flat, the Power LED
   blinking once a second while it happens and going steady when it is done
   (section 2.10, Charging the Battery pack).  Charging stops while printing and
   resumes after, and the printer will not charge with the power off.
   PEEPEEBOX_PRN_CHARGE=<minutes> shortens it for testing, since ten hours is not
   a thing anybody will sit through. */
#define CP80_BATT_FULL     100.0

/* What a pack is worth, in characters at full density.  The manual rates it at
   3000 lines "of 40 columns of the number 8" -- the worst case, every dot fired
   -- so a pack is 3000 x 40 = 120,000 such characters.

   Which means the cost of a line is what is *on* it.  A space fires no dots and
   costs nothing but the motor; a 24-column report line half full of spaces costs
   about a third of what the manual's line costs.  Draining a flat percentage per
   line would have a page of blanks cost the same as a page of solid print, and
   the whole point of the figure is that it is about dots.

   The paper still has to move for a blank line, and the motor comes off the same
   pack.  The manual does not price that, so it is an assumption and a
   deliberately visible one: a feed costs what two characters cost, which makes
   3000 blank feeds about 5% of a pack.

   The capacity counts the motor in, so that 3000 lines of forty 8s comes to
   exactly one pack and not 105% of one -- the manual's number is the whole line,
   paper movement included. */
#define CP80_BATT_COLS      40.0
#define CP80_BATT_LINES   3000.0
#define CP80_FEED_COST       2.0
#define CP80_BATT_CHARS   (CP80_BATT_LINES * (CP80_BATT_COLS + CP80_FEED_COST))

/* The head weakens before it stops, and then the pack is called flat with
   something still in it -- 4%, not 0.  A Ni-MH pack driving a thermal head has
   no useful print left well before it is empty, and the machine stays *on* at
   that point: the manual has it go OFFLINE with the Power LED blinking, not shut
   down.  The fade runs out at the same figure, so the last line printed is the
   faintest one and nothing prints after it. */
#define CP80_BATT_FADE      12.0    /* the last stretch, where print goes faint */
#define CP80_BATT_FLAT       4.0    /* flat: offline, Power LED blinking */
#define CP80_BATT_RESUME     5.0    /* and this much before it will print again */
#define CP80_LINE_MS_FLAT 1400      /* feed interval as the pack gives out */
#define CP80_CHARGE_MINS   600.0    /* about ten hours, per the manual */
#define CP80_BLINK_MS      250      /* the LED tick; 1 Hz and 2 Hz divide into it */

/* "about 28000 characters (approx. 28KB)", manual section 2.9.  Data that has
   arrived and not yet been printed sits here, and the ONLINE lamp blinks while
   any of it is left -- which is exactly what the manual says happens when the
   pack gives out mid-job. */
#define CP80_BUFFER_MAX 28000

static double       cp80_batt     = CP80_BATT_FULL;
static double       cp80_drain    = 1.0;     /* multiplier, for testing */
static double       cp80_charge   = CP80_CHARGE_MINS;
static bool         cp80_charge_overridden = false;
static bool         cp80_power    = true;    /* the switch on the left side */
static bool         cp80_ac       = false;   /* the AC adapter is plugged in */
static bool         cp80_charging = false;   /* and is actually putting charge in */
static int          cp80_blink    = 0;       /* CP80_BLINK_MS ticks, for the LEDs */
static QPushButton *cp80_replace  = nullptr;
static QPushButton *cp80_pwr_btn  = nullptr;
static QTimer      *cp80_blink_t  = nullptr;
static QSlider     *cp80_batt_sl  = nullptr;   /* testing only */

/* The battery level each printed line was printed at.  One entry per line of
   cp80_printed, because the fade belongs to the line and not to the printer: a
   receipt that started on a good pack and ended on a flat one should show that,
   with the first lines black and the last ones gone.  Fading the whole roll to
   match the present level would rewrite history every time a line arrived. */
static QVector<double> cp80_line_batt;

/* Enough to print with.  There is a percent of hysteresis on purpose: it drops
   offline at 4 and will not print again until 5, so a pack nursed along on the
   adapter prints a burst, gives out, charges a little and prints another --
   rather than dropping offline on every second line at the threshold.  The
   adapter does not exempt it: the head still comes off the pack. */
static bool
cp80_can_print(void)
{
    /* Unlike the portable DPU-414, a DATAprint 3000 with empty batteries can
       still print while the game or mains adapter is powering it. */
    if (cp80_model == Cp80PrinterModel::Dataprint3000)
        return cp80_power && ((prn_cp80_connected() != 0) || cp80_ac
                              || (cp80_batt >= CP80_BATT_RESUME));
    return cp80_power && (cp80_batt >= CP80_BATT_RESUME);
}

/* What the head is actually being driven by, as a battery level.

   Plugged in, that is the adapter: 6.5 V at 2 A, which is more than the pack
   ever delivers, so the print comes out black and at full speed however tired
   the pack is.  It is the pack only when it is on its own.

   That is why the fade is stored per line rather than read from the pack when
   the paper is drawn -- a receipt half of which was printed on the adapter has
   to show both, and there is no single number for the roll that can. */
static double
cp80_drive_level(void)
{
    if ((cp80_model == Cp80PrinterModel::Dataprint3000)
        && ((prn_cp80_connected() != 0) || cp80_ac))
        return CP80_BATT_FULL;
    return cp80_ac ? CP80_BATT_FULL : cp80_batt;
}

/* 0 on a full pack, 1 at the flat end. */
static double
cp80_fade_at(double batt)
{
    if (batt >= CP80_BATT_FADE)
        return 0.0;
    if (batt <= CP80_BATT_FLAT)
        return 1.0;
    return (CP80_BATT_FADE - batt) / (CP80_BATT_FADE - CP80_BATT_FLAT);
}

static QColor
cp80_mix(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red()   + ((b.red()   - a.red())   * t)),
                  int(a.green() + ((b.green() - a.green()) * t)),
                  int(a.blue()  + ((b.blue()  - a.blue())  * t)));
}

/* The pack keeps its charge across runs, in the rig's nvr directory beside the
   machine's own nvram.  A pack that starts full every boot is not a pack, and
   the whole point of the thing is that it runs down over a session and has to be
   put back -- which cannot be felt if closing the window undoes it.

   Absent or unreadable means a new pack, full.  Text rather than a struct
   because it is one number and being able to read it with an editor is worth
   more than four saved bytes. */
#define CP80_NVR_DPU414 "dpu414.nvr"
#define CP80_NVR_DP3000 "dataprint3000.nvr"

static bool cp80_batt_dirty = false;

static const char *
cp80_batt_file(void)
{
    return (cp80_model == Cp80PrinterModel::Dataprint3000)
         ? CP80_NVR_DP3000 : CP80_NVR_DPU414;
}

static void
cp80_batt_load(void)
{
    FILE  *f = plat_fopen(nvr_path((char *) cp80_batt_file()), "rt");
    double v = 0.0;

    if (f == nullptr)
        return;                        /* no file: a fresh pack */

    if ((fscanf(f, "%lf", &v) == 1) && (v >= 0.0) && (v <= CP80_BATT_FULL))
        cp80_batt = v;
    fclose(f);
}

static void
cp80_batt_store(void)
{
    FILE *f = plat_fopen(nvr_path((char *) cp80_batt_file()), "wt");

    if (f == nullptr)
        return;

    fprintf(f, "%.4f\n", cp80_batt);
    fclose(f);
    cp80_batt_dirty = false;
}

#define DP3000_NVR_CARD "dataprint3000.sram"
#define DP3000_NVR_UNDO "dataprint3000.undo"
#define DP3000_NVR_SETTINGS "dataprint3000.settings"

/* The manual makes the ownership boundary explicit: date/time, DATAprint
   number, PC baud rate and cardless-operation permission belong to the
   DATAprint; the remaining configuration travels with the removable SRAM
   card.  Version 1 of the simulator kept all of it in one global text file.
   DP3D2/DP3K2 split it without abandoning those installations. */
static void
dp3000_device_settings_save()
{
    FILE *f = plat_fopen(nvr_path((char *) DP3000_NVR_SETTINGS), "wt");

    if (f == nullptr)
        return;
    fprintf(f, "DP3D2 %lld %d %d %d\n",
            (long long) dp3000_settings.clock_offset_secs,
            dp3000_settings.dataprint_number, dp3000_settings.pc_baud,
            dp3000_settings.without_card);
    fclose(f);
}

static void
dp3000_device_settings_load()
{
    FILE *f = plat_fopen(nvr_path((char *) DP3000_NVR_SETTINGS), "rt");
    char tag[16] = "";

    if (f == nullptr)
        return;
    if ((fscanf(f, "%15s", tag) == 1) && !strcmp(tag, "DP3D2")) {
        long long offset = 0;
        int number = 0;
        int baud = 2;
        int without_card = false;

        if (fscanf(f, "%lld %d %d %d", &offset, &number, &baud,
                   &without_card) == 4) {
            dp3000_settings.clock_offset_secs = (qint64) offset;
            dp3000_settings.dataprint_number = qBound(0, number, 9999);
            dp3000_settings.pc_baud = qBound(0, baud, 4);
            dp3000_settings.without_card = !!without_card;
        }
        fclose(f);
        return;
    }

    /* Version 1: rewind and import its one-file settings.  Card settings are
       written into the DP3K2 card image on the next committed settings walk. */
    rewind(f);
    long long epoch = 0;
    char key[16] = "01010101";
    Dp3000Settings s;

    const int got = fscanf(f,
        "%lld %d %d %d %d %d %d %d %d %d %d %d %d %d %15s %d %d %d %d %d %d %d %d %d",
        &epoch, &s.dataprint_number, &s.card_number, &s.storage_mode,
        &s.first_print_length, &s.first_custom, &s.second_print_length,
        &s.second_custom, &s.cash_receipt, &s.without_card, &s.print_numbers,
        &s.evaluation_enabled, &s.overwrite_machine_settings, &s.pc_baud, key,
        &s.evaluation_type, &s.delete_machine_data, &s.statistics, &s.copy,
        &s.list, &s.control, &s.delete_adp_code, &s.clock_setter,
        &s.vat_half_percent);
    fclose(f);
    if (got != 24)
        return;
    s.clock_offset_secs = QDateTime::currentDateTime().secsTo(
        QDateTime::fromSecsSinceEpoch(epoch));
    s.key_code = QString::fromLatin1(key).left(8);
    if (s.key_code.size() != 8)
        s.key_code = QStringLiteral("01010101");
    s.dataprint_number = qBound(0, s.dataprint_number, 9999);
    s.card_number = qBound(0, s.card_number, 9999);
    s.first_print_length = qBound(0, s.first_print_length, 5);
    s.second_print_length = qBound(0, s.second_print_length, 5);
    s.pc_baud = qBound(0, s.pc_baud, 4);
    s.evaluation_type = qBound(0, s.evaluation_type, 5);
    s.vat_half_percent = qBound(28, s.vat_half_percent, 40);
    s.storage_mode = !!s.storage_mode;
    s.first_custom = !!s.first_custom;
    s.second_custom = !!s.second_custom;
    s.cash_receipt = !!s.cash_receipt;
    s.without_card = !!s.without_card;
    s.print_numbers = !!s.print_numbers;
    s.evaluation_enabled = !!s.evaluation_enabled;
    s.overwrite_machine_settings = !!s.overwrite_machine_settings;
    s.delete_machine_data = !!s.delete_machine_data;
    s.statistics = !!s.statistics;
    s.copy = !!s.copy;
    s.list = !!s.list;
    s.control = !!s.control;
    s.delete_adp_code = !!s.delete_adp_code;
    s.clock_setter = !!s.clock_setter;
    dp3000_settings = s;
}

static QByteArray dp3000_records_encode(const QStringList &records,
                                        bool card_image);

static int
dp3000_records_bytes(const QStringList &records)
{
    return dp3000_records_encode(records, true).size();
}

static QByteArray
dp3000_records_encode(const QStringList &records, bool card_image)
{
    QByteArray data;

    if (card_image) {
        char metadata[384];
        const int n = snprintf(metadata, sizeof(metadata),
            "DP3K2\n%d %d %d %d %d %d %d %d %d %d %s %d %d %d %d %d %d %d %d %d %u\n",
            dp3000_settings.card_number, dp3000_settings.storage_mode,
            dp3000_settings.first_print_length, dp3000_settings.first_custom,
            dp3000_settings.second_print_length, dp3000_settings.second_custom,
            dp3000_settings.cash_receipt, dp3000_settings.print_numbers,
            dp3000_settings.evaluation_enabled,
            dp3000_settings.overwrite_machine_settings,
            dp3000_settings.key_code.toLatin1().constData(),
            dp3000_settings.evaluation_type,
            dp3000_settings.delete_machine_data, dp3000_settings.statistics,
            dp3000_settings.copy, dp3000_settings.list,
            dp3000_settings.control, dp3000_settings.delete_adp_code,
            dp3000_settings.clock_setter, dp3000_settings.vat_half_percent,
            dp3000_dataset_number);

        if (n > 0)
            data.append(metadata, qMin<int>(n, int(sizeof(metadata) - 1)));
        data += char(0x1f); /* settings/records boundary; reports are printable */
    } else {
        data = QByteArray("DP3K1\n", 6);
    }

    for (const QString &record : records) {
        data += record.toUtf8();
        data += char(0x1e); /* never emitted by the printable paper parser */
    }
    return data;
}

static bool
dp3000_card_settings_decode(const QByteArray &line)
{
    int card_number = 0;
    int storage_mode = true;
    int first_length = 0;
    int first_custom = false;
    int second_length = 5;
    int second_custom = false;
    int cash_receipt = true;
    int print_numbers = false;
    int evaluation_enabled = true;
    int overwrite = false;
    char key[16] = "01010101";
    int evaluation_type = 5;
    int delete_machine_data = true;
    int statistics = true;
    int copy = true;
    int list = true;
    int control = false;
    int delete_adp = false;
    int clock_setter = false;
    int vat = 32;
    unsigned dataset = 0;

    const int got = sscanf(line.constData(),
        "%d %d %d %d %d %d %d %d %d %d %15s %d %d %d %d %d %d %d %d %d %u",
        &card_number, &storage_mode, &first_length, &first_custom,
        &second_length, &second_custom, &cash_receipt, &print_numbers,
        &evaluation_enabled, &overwrite, key, &evaluation_type,
        &delete_machine_data, &statistics, &copy, &list, &control,
        &delete_adp, &clock_setter, &vat, &dataset);

    if (got != 21)
        return false;

    dp3000_settings.card_number = qBound(0, card_number, 9999);
    dp3000_settings.storage_mode = !!storage_mode;
    dp3000_settings.first_print_length = qBound(0, first_length, 5);
    dp3000_settings.first_custom = !!first_custom;
    dp3000_settings.second_print_length = qBound(0, second_length, 5);
    dp3000_settings.second_custom = !!second_custom;
    dp3000_settings.cash_receipt = !!cash_receipt;
    dp3000_settings.print_numbers = !!print_numbers;
    dp3000_settings.evaluation_enabled = !!evaluation_enabled;
    dp3000_settings.overwrite_machine_settings = !!overwrite;
    dp3000_settings.key_code = QString::fromLatin1(key).left(8);
    if (dp3000_settings.key_code.size() != 8)
        dp3000_settings.key_code = QStringLiteral("01010101");
    dp3000_settings.evaluation_type = qBound(0, evaluation_type, 5);
    dp3000_settings.delete_machine_data = !!delete_machine_data;
    dp3000_settings.statistics = !!statistics;
    dp3000_settings.copy = !!copy;
    dp3000_settings.list = !!list;
    dp3000_settings.control = !!control;
    dp3000_settings.delete_adp_code = !!delete_adp;
    dp3000_settings.clock_setter = !!clock_setter;
    dp3000_settings.vat_half_percent = qBound(28, vat, 40);
    dp3000_dataset_number = dataset;
    return true;
}

static bool
dp3000_store_read(const char *path, QStringList *records, bool card_image)
{
    FILE *f = plat_fopen(nvr_path((char *) path), "rb");

    records->clear();
    if (f == nullptr)
        return true;

    QByteArray data;
    char buffer[4096];
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0) {
        const int room = DP3000_STORE_MAX - data.size();

        if (room <= 0) {
            fclose(f);
            return false;
        }
        data.append(buffer, qMin<int>(int(n), room));
        if (int(n) > room) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    if (card_image && data.startsWith("DP3K2\n")) {
        data.remove(0, 6);
        const int boundary = data.indexOf(char(0x1f));

        if ((boundary < 0)
            || !dp3000_card_settings_decode(data.left(boundary)))
            return false;
        data.remove(0, boundary + 1);
        const QList<QByteArray> chunks = data.split(char(0x1e));

        for (const QByteArray &chunk : chunks)
            if (!chunk.isEmpty())
                records->append(QString::fromUtf8(chunk));
    } else if (data.startsWith("DP3K1\n")) {
        data.remove(0, 6);
        const QList<QByteArray> chunks = data.split(char(0x1e));

        for (const QByteArray &chunk : chunks)
            if (!chunk.isEmpty())
                records->append(QString::fromUtf8(chunk));
    } else if (!data.isEmpty()) {
        /* Version 1 wrote one undivided text stream.  Preserve it as one record
           so existing simulated cards remain readable after the upgrade. */
        records->append(QString::fromUtf8(data));
    }
    return true;
}

static bool
dp3000_store_write(const char *path, const QStringList &records,
                   bool card_image)
{
    FILE *f = plat_fopen(nvr_path((char *) path), "wb");

    if (f == nullptr)
        return false;

    const QByteArray encoded = dp3000_records_encode(records, card_image);
    if (encoded.size() > DP3000_STORE_MAX) {
        fclose(f);
        return false;
    }
    const QByteArray data = encoded;
    const bool ok = data.isEmpty()
                 || (fwrite(data.constData(), 1, size_t(data.size()), f)
                     == size_t(data.size()));
    fclose(f);
    return ok;
}

static void
dp3000_store_load()
{
    if (!dp3000_store_read(DP3000_NVR_CARD, &dp3000_records, true)
        || !dp3000_store_read(DP3000_NVR_UNDO, &dp3000_deleted_records,
                              false))
        dp3000_memory_fault = true;
    dp3000_memory_full = dp3000_records_bytes(dp3000_records)
                       >= DP3000_STORE_MAX;
    dp3000_store_initialised = true;
}

static void
dp3000_store_save()
{
    if (!dp3000_card_inserted)
        return;

    if (!dp3000_store_write(DP3000_NVR_CARD, dp3000_records, true))
        dp3000_memory_fault = true;
}

static void
dp3000_recovery_save()
{
    if (!dp3000_card_inserted)
        return;

    if (!dp3000_store_write(DP3000_NVR_UNDO, dp3000_deleted_records, false))
        dp3000_memory_fault = true;
}

/* What one line costs the pack: the dots on it, plus the motor that moved the
   paper.  Spaces are free of everything but the motor. */
static int
cp80_print_columns(const QString &line)
{
    int columns = line.size();

    while ((columns > 0) && line.at(columns - 1).isSpace())
        columns--;
    /* The controller formats 24 columns.  The DPU can physically take forty;
       the Epson M-160 in the DATAprint 3000 is itself a 24-column mechanism. */
    return qMin(columns, (cp80_model == Cp80PrinterModel::Dataprint3000)
                              ? DP3000_COLUMNS : 40);
}

static int
cp80_print_ink(const QString &line)
{
    int ink = 0;

    for (const QChar &c : line)
        if (!c.isSpace())
            ink++;
    return ink;
}

/* Spend the line and return the motor-speed scale in permille.  Logical seek
   means a short line really is faster than a 40-column line; pacing every line
   at one guessed fixed interval hid one of the DPU-414's defining behaviours. */
static int
cp80_batt_spend(const QString &line, bool pace_queue = true)
{
    if (cp80_model == Cp80PrinterModel::Dataprint3000) {
        /* At 0.7 line/s the M-160 shuttle determines the pace, not the amount of
           ink in a record.  A connected game or the adapter supplies power and
           therefore does not spend the battery set. */
        if ((cp80_batt > 0.0) && !cp80_ac && (prn_cp80_connected() == 0)) {
            cp80_batt -= (CP80_BATT_FULL / DP3000_BATT_LINES) * cp80_drain;
            if (cp80_batt < 0.0)
                cp80_batt = 0.0;
            cp80_batt_dirty = true;
        }
        if (pace_queue && (cp80_feed != nullptr)
            && (cp80_feed->interval() != DP3000_LINE_MS))
            cp80_feed->setInterval(DP3000_LINE_MS);
        return 1000;
    }

    double cells = CP80_FEED_COST;
    const int columns = cp80_print_columns(line);
    const int base_ms = int(CP80_PAPER_MS
                          + std::ceil((double) columns * 1000.0 / CP80_CHAR_CPS));
    int       speed = 1000;

    for (const QChar &c : line) {
        if (!c.isSpace())
            cells += 1.0;
    }

    /* Not on the adapter: the head is running off the mains, so printing costs
       the pack nothing and a full pack stays full.  The gate below is separate
       and still applies -- being plugged in does not let it print under 5%, it
       only means that what it does print does not come out of the battery. */
    if ((cp80_batt > 0.0) && !cp80_ac) {
        cp80_batt -= (cells / CP80_BATT_CHARS) * CP80_BATT_FULL * cp80_drain;
        if (cp80_batt < 0.0)
            cp80_batt = 0.0;
        cp80_batt_dirty = true;
    }

    {
        const int ms = int(base_ms
                           + ((CP80_LINE_MS_FLAT - base_ms)
                              * cp80_fade_at(cp80_drive_level())));

        if (pace_queue && (cp80_feed != nullptr) && (cp80_feed->interval() != ms))
            cp80_feed->setInterval(ms);

        speed = qBound(100, (base_ms * 1000) / qMax(ms, 1), 1000);
    }

    /* Flat.  The manual: the printer goes OFFLINE and the Power LED blinks about
       twice a second; anything still in the buffer stays there and the ONLINE
       lamp blinks over it until the adapter is connected and ONLINE pressed. */
    if ((cp80_batt <= CP80_BATT_FLAT) && (prn_cp80_connected() != 0)) {
        prn_cp80_set_connected(0);
        pclog("CP80: pack down to %.2f%% -- offline, %d characters still "
              "buffered\n", cp80_batt, int(cp80_queued.size()));
    }

    return speed;
}

static QPushButton *cp80_btn_on = nullptr;   /* ON LINE */
static QPushButton *cp80_btn_fd = nullptr;   /* FEED    */
static QScrollBar  *cp80_scroll = nullptr;   /* on the paper, past the viewport */
static bool         cp80_btn_on_down = false;
static bool         cp80_btn_fd_down = false;
static bool         cp80_paper_hover = false;
static QRect        cp80_paper_hit_rect;

static QStringList
cp80_lines_on(const QString &paper)
{
    QStringList lines = paper.split(QLatin1Char('\n'));

    /* split() leaves an empty tail after the final newline; that is the blank
       the next line will be printed on, not a line of exposed paper. */
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

static double
cp80_smoothstep(double t)
{
    t = qBound(0.0, t, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

static void cp80_render();

static void
cp80_update_paper_hover()
{
    const bool hovering = (cp80_view != nullptr)
                       && cp80_paper_hit_rect.contains(
                              cp80_view->mapFromGlobal(QCursor::pos()));

    if (hovering == cp80_paper_hover)
        return;
    cp80_paper_hover = hovering;
    if (cp80_scroll != nullptr)
        cp80_scroll->setVisible(hovering
                                && (cp80_scroll->maximum()
                                    > cp80_scroll->minimum()));
}

class Cp80PaperEventFilter final : public QObject {
public:
    explicit Cp80PaperEventFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::Enter)
            || (event->type() == QEvent::MouseMove)) {
            cp80_update_paper_hover();
        } else if (event->type() == QEvent::Leave) {
            cp80_paper_hover = false;
            if ((cp80_scroll != nullptr) && !cp80_scroll->isSliderDown())
                cp80_scroll->hide();
        } else if ((event->type() == QEvent::Wheel) && cp80_paper_hover
                   && (cp80_scroll != nullptr)
                   && (cp80_scroll->maximum() > cp80_scroll->minimum())) {
            const int delta = static_cast<QWheelEvent *>(event)->angleDelta().y();

            if (delta != 0) {
                cp80_scroll->setValue(cp80_scroll->value()
                    - ((delta > 0) ? 3 : -3));
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

/* Use the work area, not a fixed height or the full screen: on Windows the full
   geometry includes the taskbar, while the old constant left a conspicuous
   unused strip above it.  The hint covers the first paint, before the dialog has
   a native window; after that the screen containing the dialog wins. */
static QRect
cp80_available_geometry()
{
    QScreen *screen = nullptr;

    if ((cp80_win != nullptr) && cp80_win->isVisible())
        screen = QGuiApplication::screenAt(cp80_win->frameGeometry().center());
    if ((screen == nullptr) && !cp80_available_hint.isNull())
        return cp80_available_hint;
    if (screen == nullptr)
        screen = QGuiApplication::primaryScreen();

    return (screen != nullptr) ? screen->availableGeometry()
                               : QRect(0, 0, cp80_machine_width() + 80,
                                       cp80_machine_height() + 240);
}

/* Widget geometry is expressed in logical pixels and is therefore scaled by
   Qt on a per-monitor-DPI-aware Windows build.  Give the painted canvases a
   matching high-resolution backing store as well, so vector details and print
   stay sharp instead of stretching a 100% bitmap on a scaled display. */
static QPixmap
cp80_canvas(int logical_width, int logical_height)
{
    const qreal scale = (cp80_win != nullptr)
                      ? qMax<qreal>(1.0, cp80_win->devicePixelRatioF()) : 1.0;
    QPixmap pixmap(int(std::ceil(logical_width * scale)),
                   int(std::ceil(logical_height * scale)));

    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

class Cp80BatterySlider final : public QSlider {
public:
    explicit Cp80BatterySlider(QWidget *parent)
        : QSlider(Qt::Horizontal, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRectF body(1.0, 1.0, width() - 8.0, height() - 2.0);
        const QRectF tip(body.right() + 1.0, body.top() + (body.height() * 0.31),
                         5.0, body.height() * 0.38);
        const QRectF level = body.adjusted(3.0, 3.0, -3.0, -3.0);
        const qreal amount = (maximum() == minimum()) ? 0.0
            : qBound<qreal>(0.0, (value() - minimum())
                                  / qreal(maximum() - minimum()), 1.0);
        QColor charge(75, 176, 86);

        if (value() <= int(CP80_BATT_FLAT))
            charge = QColor(210, 54, 45);
        else if (value() <= 20)
            charge = QColor(216, 153, 45);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(46, 47, 51));
        p.drawRoundedRect(body, 3.5, 3.5);
        p.setBrush(QColor(142, 143, 149));
        p.drawRoundedRect(tip, 1.5, 1.5);

        QPainterPath level_shape;
        level_shape.addRoundedRect(level, 1.8, 1.8);
        p.save();
        p.setClipPath(level_shape);
        p.fillRect(level, QColor(26, 27, 30));
        p.fillRect(QRectF(level.left(), level.top(), level.width() * amount,
                          level.height()), charge);
        if (cp80_charging) {
            QLinearGradient sheen(level.topLeft(), level.bottomRight());
            sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
            sheen.setColorAt(0.5, QColor(255, 255, 255, 54));
            sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
            p.fillRect(level, sheen);
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(hasFocus() ? QColor(218, 218, 222)
                                 : QColor(151, 152, 157), 1.5));
        p.drawRoundedRect(body, 3.5, 3.5);

        QFont number_font = font();
        number_font.setBold(true);
        p.setFont(number_font);
        p.setPen(QColor(250, 250, 250));
        p.drawText(body, Qt::AlignCenter,
                   QStringLiteral("%1%").arg(cp80_batt, 0, 'f', 0));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setSliderDown(true);
        setFromPosition(eventPosition(event));
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QSlider::mouseMoveEvent(event);
            return;
        }
        setFromPosition(eventPosition(event));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if ((event->button() == Qt::LeftButton) && isSliderDown()) {
            setFromPosition(eventPosition(event));
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    static qreal eventPosition(const QMouseEvent *event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return event->position().x();
#else
        return event->localPos().x();
#endif
    }

    void setFromPosition(qreal x)
    {
        const qreal left = 4.0;
        const qreal span = qMax<qreal>(1.0, width() - 14.0);
        const qreal fraction = qBound<qreal>(0.0, (x - left) / span, 1.0);

        setValue(qRound(minimum() + (fraction * (maximum() - minimum()))));
    }
};

static int
cp80_window_frame_height()
{
    if ((cp80_win != nullptr) && cp80_win->isVisible()) {
        const int measured = cp80_win->frameGeometry().height()
                           - cp80_win->geometry().height();

        if (measured > 0)
            return measured;
    }

    if (cp80_win == nullptr)
        return 32;

    return cp80_win->style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr,
                                           cp80_win)
         + (2 * cp80_win->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,
                                                nullptr, cp80_win));
}

/* Height beside the painted machine: layout margins, the real controls below
   it, and (only on an error) the connection-status label. */
static int
cp80_non_canvas_height()
{
    if ((cp80_win == nullptr) || (cp80_win->layout() == nullptr))
        return 56;

    /* Ask the layout rather than reconstructing its height from selected
       children.  That includes platform-dependent control minima and rounding
       at fractional desktop scales. */
    cp80_win->layout()->invalidate();
    cp80_win->layout()->activate();
    return qMax(0, cp80_win->sizeHint().height() - cp80_view->height());
}

static int
cp80_max_canvas_height()
{
    const QRect available = cp80_available_geometry().adjusted(
        CP80_SCREEN_MARGIN, CP80_SCREEN_MARGIN,
        -CP80_SCREEN_MARGIN, -CP80_SCREEN_MARGIN);
    const int outside = cp80_window_frame_height() + cp80_non_canvas_height();
    int available_logical;

    if ((cp80_win != nullptr) && cp80_win->isVisible()) {
        /* The physical paper direction decides which decorated-frame edge is
           stationary.  The DPU's top-exit roll grows upward from its lower
           edge; the DATAprint's operator-facing paper grows downward from its
           upper edge.  Use the actual layout overhead in Qt logical pixels so
           neither direction can cross the desktop work area. */
        const QRect frame = cp80_win->frameGeometry();

        if (cp80_model == Cp80PrinterModel::Dataprint3000) {
            const int latest_top = qMax(
                available.top(), available.bottom() - outside
                               - cp80_machine_height() + 1);
            const int anchor_top = qBound(available.top(), frame.top(),
                                           latest_top);

            available_logical = available.bottom() - anchor_top + 1 - outside;
        } else {
            const int earliest_bottom = qMin(
                available.bottom(), available.top() + outside
                                  + cp80_machine_height() - 1);
            const int anchor_bottom = qBound(earliest_bottom, frame.bottom(),
                                              available.bottom());

            available_logical = anchor_bottom - available.top() + 1 - outside;
        }
    } else {
        available_logical = available.height() - outside;
    }

    return qMax(cp80_machine_height(), available_logical);
}

/* The roll's capacity is a property of the screen it is actually on.  On a
   short display it scrolls sooner; on a tall one it uses the space all the way
   down to the printer instead of stopping at an arbitrary twenty lines. */
static int
cp80_visible_line_capacity()
{
    const int paper_height = CP80_SLOT_YS
                           + (cp80_max_canvas_height() - CP80_HEAD_H);
    const int lines = (paper_height - CP80_PAPER_PAD_Y - CP80_PAPER_EDGE_ROOM)
                    / CP80_TEXT_LINE_H;

    return qBound(1, lines, 200);
}

static int
cp80_paper_motion_offset(int line_height)
{
    if (cp80_paper_frame >= CP80_PAPER_FRAMES)
        return 0;

    const double t = (double) cp80_paper_frame
                   / (double) (CP80_PAPER_FRAMES - 1);

    double offset = (1.0 - cp80_smoothstep(t)) * line_height;

    /* Thin stock carries a little momentum past its resting position.  The
       one-to-two-pixel lift in the final third then damps back to exactly zero,
       avoiding both a rubbery bounce and a permanent alignment error. */
    if (t > 0.58) {
        const double settle = (t - 0.58) / 0.42;

        offset -= 1.8 * sin(CP80_UI_PI * settle);
    }
    return qRound(offset);
}

static void
cp80_begin_paper_motion()
{
    cp80_paper_frame = 0;
    if (cp80_paper_motion != nullptr)
        cp80_paper_motion->start();
    cp80_render();
}

static void
cp80_schedule_home(unsigned columns, unsigned speed, int delay_ms)
{
    if (columns > 0) {
        cp80_last_head_columns = columns;
        cp80_last_head_speed   = speed;
    }
    if (cp80_home_timer != nullptr) {
        if (cp80_head_away)
            cp80_home_timer->start(delay_ms);
        else
            cp80_home_timer->stop();
    }
}

/* A freshly torn roll does not have a mathematically straight leading edge.
   Keep the profile shallow so it reads as thin thermal paper, not card stock.
   When the roll reaches the ceiling, the first few centimetres buckle inward;
   returning the leading path separately lets it be stroked after all surface
   effects so the ripped edge never disappears into them. */
static QPainterPath
cp80_live_paper_shape(const QRect &paper, int crumple_depth,
                      QPainterPath *leading_edge, QPainterPath *shadow_edge)
{
    static const int edge[] = { 2, 1, 3, 1, 2, 0, 2, 1, 3, 1 };
    const int edge_count = (int) (sizeof(edge) / sizeof(edge[0]));
    const int edge_phase = (int) ((cp80_edge_generation * 3u) % edge_count);
    QPainterPath shape;
    QPainterPath leading;

    shape.moveTo(paper.left(), paper.top() + edge[edge_phase]);
    leading.moveTo(paper.left(), paper.top() + edge[edge_phase]);
    for (int x = 0; x <= paper.width(); x += 11) {
        const int i = ((x / 11) + edge_phase) % edge_count;
        const QPoint point(paper.left() + x, paper.top() + edge[i]);

        shape.lineTo(point);
        leading.lineTo(point);
    }
    const QPoint top_right(paper.right(), paper.top()
        + edge[((paper.width() / 11) + edge_phase) % edge_count]);
    shape.lineTo(top_right);
    leading.lineTo(top_right);
    QPainterPath exposed = leading;

    crumple_depth = qBound(0, crumple_depth, qMax(0, paper.height() - 2));
    if (crumple_depth > 0) {
        shape.lineTo(paper.right() - 3, paper.top() + (crumple_depth / 3));
        shape.lineTo(paper.right(), paper.top() + ((crumple_depth * 2) / 3));
        shape.lineTo(paper.right() - 1, paper.top() + crumple_depth);
        exposed.lineTo(paper.right() - 3,
                       paper.top() + (crumple_depth / 3));
        exposed.lineTo(paper.right(),
                       paper.top() + ((crumple_depth * 2) / 3));
        exposed.lineTo(paper.right() - 1, paper.top() + crumple_depth);
    }
    shape.lineTo(paper.right(), paper.bottom());
    exposed.lineTo(paper.right(), paper.bottom());
    shape.lineTo(paper.left(), paper.bottom());

    QPainterPath left_side;
    left_side.moveTo(paper.left(), paper.bottom());
    if (crumple_depth > 0) {
        shape.lineTo(paper.left() + 2, paper.top() + crumple_depth);
        shape.lineTo(paper.left(), paper.top() + ((crumple_depth * 2) / 3));
        shape.lineTo(paper.left() + 3, paper.top() + (crumple_depth / 3));
        left_side.lineTo(paper.left() + 2,
                         paper.top() + crumple_depth);
        left_side.lineTo(paper.left(),
                         paper.top() + ((crumple_depth * 2) / 3));
        left_side.lineTo(paper.left() + 3,
                         paper.top() + (crumple_depth / 3));
    }
    left_side.lineTo(paper.left(), paper.top() + edge[edge_phase]);
    exposed.addPath(left_side);
    shape.closeSubpath();

    if (leading_edge != nullptr)
        *leading_edge = leading;
    if (shadow_edge != nullptr)
        *shadow_edge = exposed;
    return shape;
}

static void
cp80_draw_crumple(QPainter &g, const QRect &paper, int depth, int excess)
{
    if (depth <= 0)
        return;

    /* Successive ridges compress the surplus roll into a shallow accordion.
       Their spacing and curvature are deliberately unequal: a perfectly
       periodic stack reads as a graphic pattern rather than thin paper. */
    const int folds = qBound(2, 2 + (excess / 4), 5);
    const int phase = cp80_paper_motion_offset(3);

    QLinearGradient compression(0, paper.top(), 0, paper.top() + depth);
    compression.setColorAt(0.00, QColor(68, 70, 80, 34));
    compression.setColorAt(0.28, QColor(255, 255, 255, 26));
    compression.setColorAt(0.58, QColor(75, 77, 87, 23));
    compression.setColorAt(1.00, QColor(255, 255, 255, 0));
    g.fillRect(QRect(paper.left(), paper.top(), paper.width(), depth),
               compression);

    for (int i = 0; i < folds; i++) {
        const int y = paper.top() + 5 + phase
                    + ((i + 1) * (depth - 7) / (folds + 1));
        const int bend = 2 + ((i * 3 + excess) % 4);
        QPainterPath ridge;

        ridge.moveTo(paper.left() + 1, y);
        ridge.cubicTo(paper.left() + (paper.width() * 2 / 9), y - bend,
                      paper.left() + (paper.width() * 3 / 8), y + bend,
                      paper.left() + (paper.width() / 2), y);
        ridge.cubicTo(paper.left() + (paper.width() * 5 / 8), y - bend - 1,
                      paper.left() + (paper.width() * 7 / 9), y + bend,
                      paper.right() - 1, y - 1);

        g.setPen(QPen(QColor(66, 68, 78, 34), 3.0));
        g.drawPath(ridge.translated(0, 2));
        g.setPen(QPen(QColor(255, 255, 255, 43), 1.0));
        g.drawPath(ridge);
    }
}

/* Redraw the photographed cap two pixels lower while its invisible hit target
   is held.  A dark strip in the newly exposed recess and a light compression
   shade make the movement legible without replacing the real button texture. */
static void
cp80_draw_pressed_button(QPainter &g, bool down, int image_x, int image_w,
                         int machine_top)
{
    if (!down || (cp80_body == nullptr))
        return;

    const QRect source(CP80_SCALE(image_x), CP80_SCALE(CP80_BTN_Y),
                       CP80_SCALE(image_w), CP80_SCALE(CP80_BTN_H));
    const QRect target = source.translated(0, machine_top);

    g.save();
    g.setClipRect(target);
    g.drawPixmap(target.translated(0, 2), *cp80_body, source);
    g.fillRect(QRect(target.left(), target.top(), target.width(), 2),
               QColor(28, 29, 34, 92));

    QLinearGradient pressure(0, target.top(), 0, target.bottom());
    pressure.setColorAt(0.0, QColor(20, 21, 25, 34));
    pressure.setColorAt(0.55, QColor(20, 21, 25, 12));
    pressure.setColorAt(1.0, QColor(255, 255, 255, 18));
    g.fillRect(target, pressure);
    g.restore();
}

/* The printer is the stationary object.  Resize around the edge opposite the
   paper outlet: upward around the DPU's lower edge and downward around the
   DATAprint's upper edge. */
static void
cp80_resize_to_content()
{
    if ((cp80_win == nullptr) || (cp80_win->layout() == nullptr))
        return;

    const bool  anchored = cp80_win->isVisible() && !cp80_win->isMaximized()
                         && !cp80_win->isFullScreen();
    const QRect old_frame = cp80_win->frameGeometry();

    cp80_win->layout()->invalidate();
    cp80_win->layout()->activate();
    QSize wanted = cp80_win->sizeHint();
    const int outside_canvas = qMax(0, wanted.height() - cp80_view->height());
    const int maximum_client = cp80_max_canvas_height() + outside_canvas;

    wanted.setHeight(qMin(wanted.height(), maximum_client));
    cp80_win->resize(wanted);

    if (anchored) {
        const QRect available = cp80_available_geometry().adjusted(
            CP80_SCREEN_MARGIN, CP80_SCREEN_MARGIN,
            -CP80_SCREEN_MARGIN, -CP80_SCREEN_MARGIN);
        const int minimum_frame = cp80_window_frame_height()
                                + cp80_non_canvas_height()
                                + cp80_machine_height();
        int dy;

        if (cp80_model == Cp80PrinterModel::Dataprint3000) {
            const int latest_top = qMax(available.top(),
                available.bottom() - minimum_frame + 1);
            const int anchor_top = qBound(available.top(), old_frame.top(),
                                           latest_top);

            dy = anchor_top - cp80_win->frameGeometry().top();
        } else {
            const int earliest_bottom = qMin(
                available.bottom(), available.top() + minimum_frame - 1);
            const int anchor_bottom = qBound(earliest_bottom,
                                             old_frame.bottom(),
                                             available.bottom());

            dy = anchor_bottom - cp80_win->frameGeometry().bottom();
        }

        /* Clamp the anchor itself to the work area.  This also repairs an
           already-overgrown dialog on the first repaint without pushing its
           stationary edge farther outside the work area. */
        if (dy != 0)
            cp80_win->move(cp80_win->pos() + QPoint(0, dy));
    }
}

static void
cp80_anchor_vertical_edge()
{
    if (cp80_win == nullptr)
        return;

    const QRect available = cp80_available_geometry().adjusted(
        CP80_SCREEN_MARGIN, CP80_SCREEN_MARGIN,
        -CP80_SCREEN_MARGIN, -CP80_SCREEN_MARGIN);
    const QRect frame = cp80_win->frameGeometry();
    const int y = (cp80_model == Cp80PrinterModel::Dataprint3000)
                ? available.top()
                : qMax(available.top(), available.bottom() - frame.height() + 1);

    cp80_win->move(cp80_win->pos() + QPoint(0, y - frame.top()));
}

static void
cp80_place_initial(QWidget *parent)
{
    if (cp80_win == nullptr)
        return;

    const QRect parent_frame = (parent != nullptr)
                             ? parent->window()->frameGeometry() : QRect();
    QScreen *screen = !parent_frame.isNull()
                    ? QGuiApplication::screenAt(parent_frame.center()) : nullptr;

    if (screen == nullptr)
        screen = QGuiApplication::screenAt(cp80_win->frameGeometry().center());
    if (screen == nullptr)
        screen = QGuiApplication::primaryScreen();
    if (screen == nullptr)
        return;

    cp80_available_hint = screen->availableGeometry();
    cp80_render();                 /* native frame metrics are now available */

    const QRect available = cp80_available_hint.adjusted(
        CP80_SCREEN_MARGIN, CP80_SCREEN_MARGIN,
        -CP80_SCREEN_MARGIN, -CP80_SCREEN_MARGIN);
    const QRect frame = cp80_win->frameGeometry();
    int x = !parent_frame.isNull() ? parent_frame.right() + 8
                                   : available.right() - frame.width() + 1;

    if ((x + frame.width() - 1) > available.right())
        x = !parent_frame.isNull() ? parent_frame.left() - frame.width() - 8
                                   : available.left();
    x = qBound(available.left(), x,
               qMax(available.left(), available.right() - frame.width() + 1));
    const int y = (cp80_model == Cp80PrinterModel::Dataprint3000)
                ? available.top()
                : qMax(available.top(), available.bottom() - frame.height() + 1);

    /* move() and frameGeometry() use slightly different origins on decorated
       top-level windows.  Moving by the measured delta keeps the outer frame,
       including its title bar, inside the work area exactly. */
    cp80_win->move(cp80_win->pos()
                   + QPoint(x - frame.left(), y - frame.top()));

    /* The compact dialog now has the edge appropriate to its paper path.
       Repaint once more so a long roll may use exactly the room on the output
       side of that anchor, but no more. */
    cp80_render();
}

static QFont
cp80_print_font()
{
    /* Courier New's hinted eleven-pixel strike is a close platform-independent
       stand-in for the printer ROM's 7 x 9 raster.  Drawing it without text
       antialiasing is important: ClearType fringes make tiny thermal dots look
       like ordinary blue/red screen text.  Characters are positioned below at
       the printer's own eight-dot pitch, so font substitution cannot change
       the receipt's columns. */
    QFont font(QStringLiteral("Courier New"));

    font.setStyleHint(QFont::TypeWriter, QFont::PreferMatch);
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setFixedPitch(true);
    font.setKerning(false);
    font.setPixelSize(CP80_TEXT_FONT_PX);
    return font;
}

static void
cp80_draw_print_line(QPainter &painter, qreal left, qreal baseline,
                     const QString &line)
{
    const int columns = qMin(line.size(), int(CP80_PRINT_DOTS
                                               / CP80_TEXT_CELL_DOTS));

    /* Position every glyph explicitly.  This retains the DPU-414's 40-column
       measure even if Qt has to substitute another typewriter face. */
    for (int column = 0; column < columns; column++)
        painter.drawText(QPointF(left + (column * CP80_TEXT_CELL_W), baseline),
                         line.mid(column, 1));
}

static void
cp80_update_model_controls()
{
    const bool dp3000 = (cp80_model == Cp80PrinterModel::Dataprint3000);

    if (cp80_layout != nullptr) {
        /* Insertion order is canvas, gap, status, controls.  Reverse it only
           for the top-anchored DATAprint so the controls stay fixed above the
           machine while its paper and dialog extend downward. */
        cp80_layout->setDirection(dp3000 ? QBoxLayout::BottomToTop
                                         : QBoxLayout::TopToBottom);
        cp80_layout->setAlignment(
            cp80_view, Qt::AlignHCenter
                      | (dp3000 ? Qt::AlignTop : Qt::AlignBottom));
    }

    /* The DPU controls are transparent hit targets over its photographed
       switches.  The DATAprint has its cable and paper-feed controls outside
       the top panel, so it uses the labelled controls above the illustration. */
    if (cp80_btn_on != nullptr)
        cp80_btn_on->setVisible(!dp3000);
    if (cp80_btn_fd != nullptr)
        cp80_btn_fd->setVisible(!dp3000);
    if (cp80_link_btn != nullptr)
        cp80_link_btn->setVisible(dp3000);
    if (cp80_feed_btn != nullptr)
        cp80_feed_btn->setVisible(dp3000);
    if (cp80_paper_btn != nullptr)
        cp80_paper_btn->setVisible(dp3000);
    if (dp3000_wp_btn != nullptr)
        dp3000_wp_btn->setVisible(dp3000);
    if (cp80_pwr_btn != nullptr)
        cp80_pwr_btn->setVisible(!dp3000);
    if (dp3000_case_feed != nullptr)
        dp3000_case_feed->setVisible(dp3000);
    if (dp3000_case_reset != nullptr)
        dp3000_case_reset->setVisible(dp3000);
    if (dp3000_key_yes != nullptr)
        dp3000_key_yes->setVisible(dp3000);
    if (dp3000_key_init != nullptr)
        dp3000_key_init->setVisible(dp3000);
    if (dp3000_key_no != nullptr)
        dp3000_key_no->setVisible(dp3000);
    if (dp3000_card != nullptr)
        dp3000_card->setVisible(dp3000);
}

static QFont
cp80_dp3000_print_font()
{
    QFont font(QStringLiteral("Courier New"));

    font.setStyleHint(QFont::TypeWriter, QFont::PreferMatch);
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setFixedPitch(true);
    font.setKerning(false);
    font.setPixelSize(DP3000_TEXT_FONT_PX);
    return font;
}

static void
cp80_render_dp3000()
{
    if (cp80_view == nullptr)
        return;

    cp80_update_model_controls();

    /* The general alarm table assigns double beeps to internal failures, but
       the memory-LED description is more specific: a card/storage failure
       blinks the LED and uses rapid beeps.  A merely full card is steady and
       silent; the current device record is still forced to paper. */
    const bool alert_powered = cp80_power
                            && ((prn_cp80_connected() != 0) || cp80_ac
                                || (cp80_batt > 0.0));
    prn_dp3000_sound_signal(
        !alert_powered ? PRN_DP3000_SIGNAL_NONE
      : ((dp3000_card_inserted && dp3000_memory_fault)
         || dp3000_transfer_error || !dp3000_paper_loaded)
            ? PRN_DP3000_SIGNAL_EXTERNAL_ERROR
      : dp3000_complete_signal ? PRN_DP3000_SIGNAL_COMPLETE
                               : PRN_DP3000_SIGNAL_NONE);

    const QFont mono = cp80_dp3000_print_font();
    const QFontMetrics fm(mono);
    const QStringList all = cp80_lines_on(cp80_printed);
    const int slot_y = DP3000_SLOT_Y;
    const int max_canvas = cp80_max_canvas_height();
    const int paper_room = max_canvas - (slot_y + 7)
                         - CP80_PAPER_PAD_Y - CP80_PAPER_EDGE_ROOM;
    const int visible_lines = qBound(1, paper_room / DP3000_TEXT_LINE_H, 200);
    const int over = qMax(0, all.size() - visible_lines);

    if (cp80_scroll != nullptr) {
        const bool at_end = (cp80_scroll->value() >= cp80_scroll->maximum());

        cp80_scroll->blockSignals(true);
        cp80_scroll->setRange(0, over);
        cp80_scroll->setPageStep(visible_lines);
        cp80_scroll->setSingleStep(1);
        if (at_end)
            cp80_scroll->setValue(over);
        cp80_scroll->blockSignals(false);
        cp80_scroll->setVisible((over > 0) && cp80_paper_hover);
    }

    const int first = (cp80_scroll != nullptr)
                    ? qBound(0, cp80_scroll->value(), over) : over;
    const bool paper_moving = (cp80_paper_frame < CP80_PAPER_FRAMES)
                           && (first == over);
    const bool tail_motion = paper_moving && (over > 0);
    const int line_first = tail_motion ? first - 1 : first;
    const QStringList lines = all.mid(line_first,
        visible_lines + (tail_motion ? 1 : 0));
    const int paper_rows = qMin(lines.size(), visible_lines);
    const int paper_h = lines.isEmpty() ? CP80_EMPTY_LIP_H
                                       : (paper_rows * DP3000_TEXT_LINE_H)
                                         + CP80_PAPER_PAD_Y;
    const QStringList torn_all = cp80_tearing
                               ? cp80_lines_on(cp80_torn_text) : QStringList();
    const QStringList torn_lines = torn_all.mid(cp80_torn_first, visible_lines);
    const int torn_h = torn_lines.isEmpty() ? 0
                                           : (torn_lines.size()
                                              * DP3000_TEXT_LINE_H)
                                             + CP80_PAPER_PAD_Y;
    const int tear_room = (torn_h > 0) ? 48 : 0;
    const int natural_h = qMax(
        DP3000_HEAD_H, slot_y + 7 + qMax(paper_h, torn_h + tear_room)
                         + CP80_PAPER_EDGE_ROOM);
    /* Once the receipt reaches the work-area edge, hold the canvas at its
       exact limit and scroll older lines.  Before then every printed line
       lengthens the canvas toward the bottom, never toward the title bar. */
    const int H = (over > 0) ? max_canvas : qMin(max_canvas, natural_h);

    QPixmap out = cp80_canvas(DP3000_HEAD_W, H);
    out.fill(Qt::transparent);
    QPainter g(&out);
    g.setRenderHint(QPainter::Antialiasing, true);

    const int bx = DP3000_BODY_X;
    const int by = DP3000_BODY_Y;
    const int bw = DP3000_BODY_W;
    const int bh = DP3000_BODY_H;
    const int keyboard_x = 22;
    const int keyboard_y = 330;
    const int keyboard_w = 326;
    const int keyboard_h = 166;

    /* Accessory and machine cables sit below the hardware.  Their route and
       connector size come from the supplied top-down photographs. */
    auto draw_cable = [&g](const QPainterPath &path, const QColor &colour) {
        g.setPen(QPen(QColor(7, 8, 9, 92), 10.0, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        g.drawPath(path.translated(2, 3));
        g.setPen(QPen(colour, 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g.drawPath(path);
        g.setPen(QPen(QColor(255, 255, 255, 30), 1.0, Qt::SolidLine,
                      Qt::RoundCap));
        g.drawPath(path.translated(-1, -1));
    };

    QPainterPath keyboard_cable;
    keyboard_cable.moveTo(keyboard_x + keyboard_w - 8, keyboard_y + 37);
    keyboard_cable.cubicTo(383, 350, 338, 620, 430, 692);
    keyboard_cable.cubicTo(465, 718, 511, 713,
                           dp3000_keyboard_connected ? bx + 92 : bx - 20,
                           dp3000_keyboard_connected ? by + bh + 5 : by + bh + 18);
    draw_cable(keyboard_cable, QColor(36, 37, 39));

    if (cp80_ac) {
        QPainterPath adapter_cable;
        adapter_cable.moveTo(bx + bw - 2, by + 579);
        adapter_cable.cubicTo(718, 588, 697, 676, 758, 689);
        draw_cable(adapter_cable, QColor(37, 38, 41));
    }
    if (prn_cp80_connected() != 0) {
        QPainterPath vdai_cable;
        vdai_cable.moveTo(bx + bw - 77, by + bh - 2);
        vdai_cable.cubicTo(642, 704, 708, 675, 758, 713);
        draw_cable(vdai_cable, QColor(45, 45, 47));
    }

    /* The detachable keyboard is a physical part of the DATAprint workflow.
       Its three red caps are multifunction keys: yes/print/plus,
       initialise, and no/delete/minus. */
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(5, 6, 7, 80));
    g.drawRoundedRect(QRectF(keyboard_x + 6, keyboard_y + 8,
                             keyboard_w, keyboard_h), 12, 12);
    QLinearGradient keyboard_case(0, keyboard_y, 0, keyboard_y + keyboard_h);
    keyboard_case.setColorAt(0.0, QColor(58, 59, 61));
    keyboard_case.setColorAt(0.18, QColor(39, 40, 42));
    keyboard_case.setColorAt(1.0, QColor(18, 19, 21));
    g.setBrush(keyboard_case);
    g.setPen(QPen(QColor(9, 10, 11), 2.0));
    g.drawRoundedRect(QRectF(keyboard_x, keyboard_y, keyboard_w, keyboard_h),
                      10, 10);
    g.setBrush(QColor(19, 20, 22));
    g.setPen(QPen(QColor(107, 108, 110), 1.0));
    g.drawRect(QRectF(keyboard_x + 48, keyboard_y + 17,
                      keyboard_w - 66, 66));

    QFont key_label = cp80_win != nullptr ? cp80_win->font() : QFont();
    key_label.setBold(true);
    key_label.setPixelSize(11);
    g.setFont(key_label);
    g.setPen(QColor(238, 237, 226));
    g.drawText(QRect(keyboard_x + 58, keyboard_y + 22, 54, 17),
               Qt::AlignCenter, QStringLiteral("+"));
    g.drawText(QRect(keyboard_x + 58, keyboard_y + 38, 54, 15),
               Qt::AlignCenter, dp3000_text(Dp3000Text::KeyYes));
    g.drawText(QRect(keyboard_x + 48, keyboard_y + 56, 74, 20),
               Qt::AlignCenter, dp3000_text(Dp3000Text::KeyPrint));
    g.drawText(QRect(keyboard_x + 133, keyboard_y + 54, 58, 22),
               Qt::AlignCenter, QStringLiteral("init."));
    g.drawText(QRect(keyboard_x + 218, keyboard_y + 22, 54, 17),
               Qt::AlignCenter, QStringLiteral("−"));
    g.drawText(QRect(keyboard_x + 218, keyboard_y + 38, 54, 15),
               Qt::AlignCenter, dp3000_text(Dp3000Text::KeyNo));
    g.drawText(QRect(keyboard_x + 207, keyboard_y + 56, 76, 20),
               Qt::AlignCenter, dp3000_text(Dp3000Text::KeyDelete));

    const int key_centres[] = { keyboard_x + 85, keyboard_x + 163,
                                keyboard_x + 244 };
    const bool key_down[] = {
        dp3000_key_yes != nullptr && dp3000_key_yes->isDown(),
        dp3000_key_init != nullptr && dp3000_key_init->isDown(),
        dp3000_key_no != nullptr && dp3000_key_no->isDown()
    };
    for (int i = 0; i < 3; i++) {
        const int press = key_down[i] ? 3 : 0;
        const QRectF recess(key_centres[i] - 20, keyboard_y + 100,
                            40, 40);
        g.setPen(QPen(QColor(5, 5, 6), 2.0));
        g.setBrush(QColor(9, 10, 11));
        g.drawEllipse(recess);
        QRadialGradient cap(key_centres[i] - 6,
                            keyboard_y + 112 + press, 25);
        cap.setColorAt(0.0, QColor(255, 100, 71));
        cap.setColorAt(0.56, QColor(218, 47, 28));
        cap.setColorAt(1.0, QColor(111, 16, 12));
        g.setBrush(cap);
        g.setPen(QPen(QColor(86, 12, 9), 1.0));
        g.drawEllipse(QRectF(key_centres[i] - 15,
                             keyboard_y + 105 + press, 30, 30));
        g.setPen(QPen(QColor(255, 208, 181, 95), 1.0));
        g.drawArc(QRectF(key_centres[i] - 11,
                         keyboard_y + 108 + press, 22, 17),
                  25 * 16, 120 * 16);
    }
    g.setBrush(QColor(155, 156, 158));
    g.setPen(QPen(QColor(18, 18, 19), 1.0));
    g.drawEllipse(QRectF(keyboard_x + 12, keyboard_y + 10, 7, 7));
    g.drawEllipse(QRectF(keyboard_x + keyboard_w - 19,
                         keyboard_y + keyboard_h - 18, 7, 7));

    /* Case shadow and exact 113 x 230 mm footprint. */
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(5, 11, 16, 82));
    g.drawRoundedRect(QRectF(bx + 8, by + 10, bw, bh), 30, 30);

    /* Rear paper-roll cover.  Its axis runs across the case, so the broad
       lighting bands must run across it too: a left-to-right gradient made the
       old drawing read as a flat, softly rounded box.  The custom outline also
       keeps the photographed straight crown and near-vertical side walls. */
    QPainterPath roll;
    roll.moveTo(bx + 31, by);
    roll.lineTo(bx + bw - 31, by);
    roll.cubicTo(bx + bw - 12, by, bx + bw, by + 15,
                 bx + bw, by + 34);
    roll.lineTo(bx + bw, by + 215);
    roll.cubicTo(bx + bw, by + 232, bx + bw - 13, by + 244,
                 bx + bw - 30, by + 244);
    roll.lineTo(bx + 30, by + 244);
    roll.cubicTo(bx + 13, by + 244, bx, by + 232, bx, by + 215);
    roll.lineTo(bx, by + 34);
    roll.cubicTo(bx, by + 15, bx + 12, by, bx + 31, by);
    roll.closeSubpath();

    QLinearGradient roll_curve(0, by, 0, by + 244);
    roll_curve.setColorAt(0.00, QColor(57, 190, 198));
    roll_curve.setColorAt(0.025, QColor(76, 207, 212));
    roll_curve.setColorAt(0.10, QColor(25, 137, 150));
    roll_curve.setColorAt(0.27, QColor(15, 116, 132));
    roll_curve.setColorAt(0.46, QColor(34, 159, 170));
    roll_curve.setColorAt(0.59, QColor(86, 207, 211));
    roll_curve.setColorAt(0.66, QColor(157, 237, 235));
    roll_curve.setColorAt(0.73, QColor(83, 207, 211));
    roll_curve.setColorAt(0.88, QColor(37, 168, 180));
    roll_curve.setColorAt(1.00, QColor(15, 117, 132));
    g.setBrush(roll_curve);
    g.setPen(QPen(QColor(10, 91, 105), 1.8));
    g.drawPath(roll);

    /* Moulded plastic loses light at both ends of the roll.  Keep the effect
       narrow: the vertical gradient above carries the cylindrical form, while
       this edge falloff gives the shell thickness without turning it into a
       left-to-right tube. */
    QLinearGradient roll_ends(bx, 0, bx + bw, 0);
    roll_ends.setColorAt(0.00, QColor(0, 48, 59, 92));
    roll_ends.setColorAt(0.055, QColor(0, 61, 70, 42));
    roll_ends.setColorAt(0.17, QColor(0, 0, 0, 0));
    roll_ends.setColorAt(0.76, QColor(0, 0, 0, 0));
    roll_ends.setColorAt(0.95, QColor(0, 58, 69, 50));
    roll_ends.setColorAt(1.00, QColor(0, 39, 50, 112));
    g.fillPath(roll, roll_ends);

    /* A restrained specular streak follows the roll axis in the reference
       photographs; the darker band beneath it makes the curvature visible
       even on a dim display. */
    QLinearGradient roll_glare(0, by + 116, 0, by + 184);
    roll_glare.setColorAt(0.00, QColor(255, 255, 255, 0));
    roll_glare.setColorAt(0.37, QColor(231, 255, 253, 24));
    roll_glare.setColorAt(0.51, QColor(244, 255, 253, 82));
    roll_glare.setColorAt(0.64, QColor(226, 255, 252, 22));
    roll_glare.setColorAt(1.00, QColor(0, 55, 66, 0));
    g.fillPath(roll, roll_glare);

    g.setPen(QPen(QColor(196, 246, 241, 72), 1.4));
    g.drawLine(QPointF(bx + 31, by + 2),
               QPointF(bx + bw - 31, by + 2));
    g.setPen(QPen(QColor(4, 76, 90, 72), 2.0));
    g.drawLine(QPointF(bx + 8, by + 213),
               QPointF(bx + bw - 8, by + 213));

    /* Main top cover, including its narrow darker side walls and front lip. */
    g.setBrush(QColor(12, 102, 116));
    g.drawRoundedRect(QRectF(bx, by + 214, bw, bh - 214), 14, 14);
    QLinearGradient lid_colour(bx, by + 214, bx + bw, by + bh);
    lid_colour.setColorAt(0.00, QColor(55, 188, 198));
    lid_colour.setColorAt(0.43, QColor(34, 174, 187));
    lid_colour.setColorAt(0.76, QColor(31, 161, 176));
    lid_colour.setColorAt(1.00, QColor(20, 133, 149));
    g.setBrush(lid_colour);
    g.setPen(QPen(QColor(17, 111, 125), 2.0));
    g.drawRoundedRect(QRectF(bx + 7, by + 220, bw - 14, bh - 235),
                      12, 12);
    QLinearGradient roll_shadow(0, by + 220, 0, by + 252);
    roll_shadow.setColorAt(0.00, QColor(0, 57, 68, 116));
    roll_shadow.setColorAt(0.24, QColor(0, 64, 74, 64));
    roll_shadow.setColorAt(1.00, QColor(0, 64, 74, 0));
    g.setPen(Qt::NoPen);
    g.setBrush(roll_shadow);
    g.drawRoundedRect(QRectF(bx + 9, by + 221, bw - 18, 31), 9, 9);
    g.setPen(QPen(QColor(180, 241, 239, 58), 2.0));
    g.drawLine(bx + 19, by + 230, bx + 19, by + bh - 29);
    g.setPen(QPen(QColor(0, 77, 90, 70), 3.0));
    g.drawLine(bx + bw - 17, by + 232, bx + bw - 17, by + bh - 28);
    g.setPen(QPen(QColor(9, 91, 105), 2.0));
    g.drawLine(bx + 12, by + bh - 23, bx + bw - 12, by + bh - 23);
    g.setPen(QPen(QColor(154, 225, 225, 94), 2.0));
    g.drawLine(bx + 22, by + bh - 29, bx + bw - 22, by + bh - 29);

    /* D-shaped black legend plate copied from the real 3000, not the reversed
       3000S lamp order. */
    const int plate_x = bx + 69;
    const int plate_y = by + 262;
    const int plate_w = 206;
    const int plate_h = 226;
    QPainterPath plate;
    plate.moveTo(plate_x, plate_y);
    plate.lineTo(plate_x + 113, plate_y);
    plate.cubicTo(plate_x + 168, plate_y, plate_x + plate_w,
                  plate_y + 46, plate_x + plate_w, plate_y + 112);
    plate.cubicTo(plate_x + plate_w, plate_y + 179, plate_x + 169,
                  plate_y + plate_h, plate_x + 113, plate_y + plate_h);
    plate.lineTo(plate_x, plate_y + plate_h);
    plate.closeSubpath();
    QLinearGradient plate_colour(plate_x, plate_y, plate_x + plate_w,
                                 plate_y + plate_h);
    plate_colour.setColorAt(0.0, QColor(24, 25, 26));
    plate_colour.setColorAt(0.48, QColor(5, 6, 7));
    plate_colour.setColorAt(1.0, QColor(29, 31, 32));
    g.setBrush(plate_colour);
    g.setPen(QPen(QColor(193, 199, 193), 1.2));
    g.drawPath(plate);

    QFont logo = cp80_win != nullptr ? cp80_win->font() : QFont();
    logo.setBold(true);
    logo.setPixelSize(18);
    g.setFont(logo);
    g.setPen(QColor(246, 239, 222));
    g.drawText(QRect(plate_x + 11, plate_y + 8, 60, 22),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("NSM"));
    logo.setPixelSize(27);
    g.setFont(logo);
    g.drawText(QRect(plate_x + 10, plate_y + 27, 139, 34),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DATAprint"));
    logo.setPixelSize(23);
    g.setFont(logo);
    g.drawText(QRect(plate_x + 132, plate_y + 48, 68, 27),
               Qt::AlignCenter, QStringLiteral("3000"));

    static const Dp3000Text labels[] = {
        Dp3000Text::StatusPowerOn,
        Dp3000Text::StatusBatteryEmpty,
        Dp3000Text::StatusToMachine,
        Dp3000Text::StatusPcMachineError,
        Dp3000Text::StatusPaperEnd,
        Dp3000Text::StatusToPc,
        Dp3000Text::StatusMemoryError,
        Dp3000Text::StatusCharging
    };
    const bool linked = (prn_cp80_connected() != 0);
    const bool supplied = linked || cp80_ac;
    const bool powered = cp80_power && (supplied || (cp80_batt > 0.0));
    const bool pc_active = false; /* DATAcontact/PC transport is not emulated. */
    const bool lights[] = {
        powered,
        powered && (cp80_batt <= CP80_BATT_FLAT),
        powered && linked,
        powered && dp3000_transfer_error,
        powered && !dp3000_paper_loaded,
        powered && pc_active,
        powered && dp3000_card_inserted
            && (dp3000_memory_fault || dp3000_memory_full),
        supplied
    };

    QFont legend = cp80_win != nullptr ? cp80_win->font() : QFont();
    legend.setPixelSize(10);
    legend.setBold(true);
    g.setFont(legend);
    for (int i = 0; i < 8; i++) {
        const int y = plate_y + 91 + (i * 16);
        const bool light_on = lights[i]
                           && !((i == 6) && dp3000_memory_fault
                                && ((cp80_blink & 1) == 0));
        const bool error = (i == 1) || (i == 3) || (i == 4) || (i == 6);
        const bool activity = (i == 2) || (i == 5);
        const QColor lit = error ? QColor(226, 47, 39)
                         : activity ? QColor(238, 183, 35)
                                    : QColor(62, 201, 87);
        const QColor dark = error ? QColor(74, 25, 25)
                          : activity ? QColor(74, 63, 29)
                                     : QColor(27, 65, 39);

        const int led_x = plate_x - 30;
        QRadialGradient lens(led_x + 4, y + 3, 8);
        lens.setColorAt(0.0, light_on ? lit.lighter(150)
                                       : dark.lighter(112));
        lens.setColorAt(0.55, light_on ? lit : dark);
        lens.setColorAt(1.0, QColor(5, 8, 8));
        g.setPen(QPen(QColor(3, 8, 9), 1.0));
        g.setBrush(lens);
        g.drawEllipse(QRectF(led_x, y, 11, 11));
        if (light_on) {
            g.setPen(QPen(QColor(255, 255, 236, 150), 1.0));
            g.drawArc(QRectF(led_x + 2, y + 1, 7, 6), 32 * 16, 112 * 16);
        }
        g.setPen(QColor(234, 234, 229));
        g.drawText(QRect(plate_x + 10, y - 2, 184, 13),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   dp3000_text(labels[i]));
        if (i < 7) {
            g.setPen(QColor(137, 139, 136));
            g.drawLine(plate_x + 9, y + 12, plate_x + 193, y + 12);
        }
    }

    /* The front-edge PCMCIA-style SRAM card and two webbing attachment loops. */
    const int card_x = bx + 119;
    const int card_y = by + bh - 13;
    g.setPen(QPen(QColor(4, 53, 62), 2.0));
    g.setBrush(QColor(4, 75, 84));
    g.drawRoundedRect(QRectF(card_x - 8, card_y, 110, 16), 2, 2);
    if (dp3000_card_inserted) {
        g.setBrush(QColor(177, 180, 169));
        g.setPen(QPen(QColor(66, 68, 63), 1.0));
        g.drawRect(QRectF(card_x, card_y + 9, 94, 19));
        QFont card_font = legend;
        card_font.setPixelSize(8);
        g.setFont(card_font);
        g.setPen(QColor(47, 49, 46));
        g.drawText(QRect(card_x + 4, card_y + 10, 86, 16), Qt::AlignCenter,
                   dp3000_text(Dp3000Text::CardTop));
    }
    g.setPen(QPen(QColor(22, 23, 24), 7.0));
    g.drawLine(bx + 29, by + bh - 2, bx + 29, DP3000_HEAD_H);
    g.drawLine(bx + bw - 29, by + bh - 2, bx + bw - 29, DP3000_HEAD_H);
    g.setPen(QPen(QColor(149, 93, 29), 2.0));
    g.drawLine(bx + 29, by + bh + 1, bx + 29, DP3000_HEAD_H);
    g.drawLine(bx + bw - 29, by + bh + 1, bx + bw - 29, DP3000_HEAD_H);

    /* Local switches.  The green side plunger advances paper; RESET is a
       recessed pin switch so it cannot be pressed accidentally. */
    const bool feed_down = dp3000_case_feed != nullptr
                        && dp3000_case_feed->isDown();
    g.setPen(QPen(QColor(4, 70, 60), 1.0));
    g.setBrush(feed_down ? QColor(36, 137, 91) : QColor(72, 204, 127));
    g.drawRoundedRect(QRectF(bx - 9 + (feed_down ? 3 : 0), by + 602,
                             13, 25), 3, 3);
    g.setBrush(QColor(9, 86, 98));
    g.setPen(QPen(QColor(4, 58, 67), 1.0));
    g.drawEllipse(QRectF(bx + bw - 8, by + 606, 10, 10));
    g.setBrush(QColor(9, 20, 22));
    g.drawEllipse(QRectF(bx + bw - 5, by + 609, 4, 4));

    /* The impact paper leaves the slot toward the operator and lies over the
       lower cover.  Width and 3.3 mm line feed stay tied to the physical scale. */
    {
        const int motion = (first == over)
                         ? cp80_paper_motion_offset(DP3000_TEXT_LINE_H) : 0;
        const int animated_h = qMax(CP80_EMPTY_LIP_H, paper_h - motion);
        const QRect paper(DP3000_PAPER_L, slot_y + 7,
                          DP3000_PAPER_W, animated_h);
        /* QRect::right()/bottom() name the last included pixel, not the outer
           edge.  Using them as polygon boundaries left the final antialiased
           column transparent enough for the turquoise cover to show through
           the right side of the sheet. */
        const int paper_right = paper.x() + paper.width();
        const int paper_bottom = paper.y() + paper.height();
        QPainterPath paper_shape;
        paper_shape.moveTo(paper.left(), paper.top());
        paper_shape.lineTo(paper_right, paper.top());
        paper_shape.lineTo(paper_right, paper_bottom - 2);
        for (int x = paper_right; x >= paper.left(); x -= 10)
            paper_shape.lineTo(x, paper_bottom - ((x / 10) & 1));
        paper_shape.closeSubpath();

        cp80_paper_hit_rect = paper;

        g.setPen(QPen(QColor(16, 24, 25, 72), 3.0));
        g.setBrush(QColor(242, 239, 226));
        g.drawPath(paper_shape.translated(2, 3));
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(242, 239, 226));
        g.drawPath(paper_shape);
        QLinearGradient paper_bow(paper.left(), 0, paper_right, 0);
        paper_bow.setColorAt(0.0, QColor(80, 76, 63, 29));
        paper_bow.setColorAt(0.18, QColor(255, 255, 255, 22));
        paper_bow.setColorAt(0.55, QColor(255, 255, 255, 32));
        paper_bow.setColorAt(1.0, QColor(76, 72, 59, 34));
        g.fillPath(paper_shape, paper_bow);

        g.save();
        g.setClipPath(paper_shape);
        g.setRenderHint(QPainter::Antialiasing, false);
        g.setRenderHint(QPainter::TextAntialiasing, false);
        g.setFont(mono);
        g.setPen(QColor(34, 32, 28));
        /* This paper exits toward the operator, down the top-view drawing.
           The newest line therefore belongs nearest the slot and pushes all
           older lines toward the free edge.  At the first animation frame the
           new line is still hidden above the slot while the formerly newest
           line remains exactly where it was. */
        const int text_motion = paper_moving ? -motion : 0;
        int y = paper.top() + 5 + fm.ascent() + text_motion;
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const QString &line = *it;
            const int columns = qMin(line.size(), DP3000_COLUMNS);
            for (int column = 0; column < columns; column++)
                g.drawText(QPointF(paper.left() + 6
                                      + (column * DP3000_TEXT_CELL_W), y),
                           line.mid(column, 1));
            y += DP3000_TEXT_LINE_H;
        }
        g.restore();

        if ((cp80_scroll != nullptr) && (over > 0)) {
            cp80_scroll->setGeometry(
                paper.right() - 11, paper.top() + 3, 9,
                qMax(20, paper.height() - 6));
        }
    }

    /* Deep slot, turquoise bevel and the M-160's visible metal tear rail. */
    QPainterPath slot_bevel;
    slot_bevel.moveTo(DP3000_PAPER_L - 15, slot_y - 8);
    slot_bevel.lineTo(DP3000_PAPER_L + DP3000_PAPER_W + 15, slot_y - 8);
    slot_bevel.lineTo(DP3000_PAPER_L + DP3000_PAPER_W + 8, slot_y + 15);
    slot_bevel.lineTo(DP3000_PAPER_L - 8, slot_y + 15);
    slot_bevel.closeSubpath();
    QLinearGradient slot_colour(0, slot_y - 8, 0, slot_y + 15);
    slot_colour.setColorAt(0.0, QColor(10, 106, 119));
    slot_colour.setColorAt(0.35, QColor(27, 142, 156));
    slot_colour.setColorAt(1.0, QColor(111, 218, 216));
    /* The moulded turquoise bevel belongs behind the emerging sheet.  It is
       drawn late so the slot itself stays crisp, therefore clip its lower lip
       at the paper exit instead of letting the horizontal blue edge paint over
       the first row of paper. */
    g.save();
    g.setClipRect(QRectF(0, 0, DP3000_HEAD_W, slot_y + 7));
    g.setPen(Qt::NoPen);
    g.setBrush(slot_colour);
    g.drawPath(slot_bevel);
    g.restore();
    g.setBrush(QColor(5, 14, 16));
    g.drawRoundedRect(QRectF(DP3000_PAPER_L - 4, slot_y - 2,
                             DP3000_PAPER_W + 8, 10), 2, 2);
    g.setPen(QPen(QColor(191, 194, 187), 2.0));
    g.drawLine(DP3000_PAPER_L, slot_y + 7,
               DP3000_PAPER_L + DP3000_PAPER_W, slot_y + 7);

    /* A torn sheet is pulled forward and to the right, matching the physical
       paper direction rather than the DPU's top-exit animation. */
    if (torn_h > 0) {
        QPixmap sheet = cp80_canvas(DP3000_PAPER_W, torn_h);
        sheet.fill(QColor(242, 239, 226));
        {
            QPainter s(&sheet);
            s.setRenderHint(QPainter::TextAntialiasing, false);
            s.setFont(mono);
            s.setPen(QColor(35, 34, 31));
            int y = 5 + fm.ascent();
            /* Preserve the live roll's physical order on the detached sheet:
               its last printed line remains at the former slot edge. */
            for (auto it = torn_lines.crbegin(); it != torn_lines.crend(); ++it) {
                const QString &line = *it;
                const int columns = qMin(line.size(), DP3000_COLUMNS);
                for (int column = 0; column < columns; column++)
                    s.drawText(QPointF(3 + (column * DP3000_TEXT_CELL_W), y),
                               line.mid(column, 1));
                y += DP3000_TEXT_LINE_H;
            }
        }

        const double t = qBound(0.0, (double) cp80_tear_frame
                                      / (double) (CP80_TEAR_FRAMES - 1), 1.0);
        const QPointF origin(DP3000_PAPER_L, slot_y + 7);
        QTransform motion;
        motion.translate(54.0 * cp80_smoothstep(t), 30.0 * t * t);
        motion.rotate(-6.0 * cp80_smoothstep(t));
        g.save();
        g.setOpacity(1.0 - cp80_smoothstep((t - 0.76) / 0.24));
        g.setWorldTransform(motion);
        g.drawPixmap(origin, sheet);
        g.restore();
    }

    if (cp80_win != nullptr) {
        const QString supply = prn_cp80_transfer_active()
                                 ? QObject::tr(", receiving data set")
                             : dp3000_transfer_error
                                 ? QObject::tr(", transfer error")
                             : linked && dp3000_complete_signal
                                 ? QObject::tr(", complete — unplug VDAI")
                             : linked ? QObject::tr(", VDAI connected")
                             : dp3000_keyboard_connected && cp80_ac
                                 ? QObject::tr(", keyboard active")
                             : dp3000_keyboard_connected
                                 ? QObject::tr(", keyboard needs adapter")
                             : cp80_ac ? QObject::tr(", adapter connected")
                                       : QString();
        cp80_win->setWindowTitle(
            cp80_power ? QObject::tr("NSM DATAprint 3000 — %1%%2")
                              .arg(cp80_batt, 0, 'f', 1).arg(supply)
                        : QObject::tr("NSM DATAprint 3000 — off"));
    }
    if (cp80_replace != nullptr) {
        cp80_replace->setText(QObject::tr("Adapter"));
        cp80_replace->setChecked(cp80_ac);
    }
    if (cp80_link_btn != nullptr) {
        cp80_link_btn->setText(QObject::tr("VDAI cable"));
        cp80_link_btn->setChecked(linked);
        if (prn_cp80_transfer_active())
            cp80_link_btn->setToolTip(QObject::tr(
                "Receiving a data set. Do not unplug until the completion signal."));
        else if (dp3000_transfer_error)
            cp80_link_btn->setToolTip(QObject::tr(
                "The last transfer was aborted or failed its checksum. Unplug, retry in Photo Play, then reconnect."));
        else if (linked && dp3000_complete_signal)
            cp80_link_btn->setToolTip(QObject::tr(
                "The complete data set was verified and committed. It is safe to unplug the VDAI cable."));
        else if (linked)
            cp80_link_btn->setToolTip(QObject::tr(
                "VDAI connected — waiting for Photo Play to send its data set."));
        else
            cp80_link_btn->setToolTip(QObject::tr(
                "VDAI unplugged — connect it when Photo Play asks for the DATAprint interface."));
    }
    if (cp80_feed_btn != nullptr) {
        cp80_feed_btn->setText(QObject::tr("Keyboard"));
        cp80_feed_btn->setChecked(dp3000_keyboard_connected);
    }
    if (cp80_paper_btn != nullptr) {
        cp80_paper_btn->setText(QObject::tr("Paper roll"));
        cp80_paper_btn->setChecked(dp3000_paper_loaded);
    }
    if (dp3000_wp_btn != nullptr) {
        dp3000_wp_btn->setText(QObject::tr("Card WP"));
        dp3000_wp_btn->setChecked(dp3000_card_write_protected);
        dp3000_wp_btn->setEnabled(dp3000_card_inserted);
    }
    if (dp3000_card != nullptr) {
        const int free_kb = qMax(0, DP3000_STORE_MAX
            - dp3000_records_bytes(dp3000_records)) / 1024;
        dp3000_card->setToolTip(dp3000_card_inserted
            ? QObject::tr("SRAM card %1 — %2 stored data set(s), %3 KB free. Switch the DATAprint off before removal.")
                  .arg(dp3000_settings.card_number, 4, 10, QLatin1Char('0'))
                  .arg(dp3000_records.size()).arg(free_kb)
            : QObject::tr("No SRAM card inserted. Click to insert the persisted 256 kB card image."));
    }

    if (cp80_batt_sl != nullptr) {
        const int want = int(cp80_batt + 0.5);
        if (cp80_batt_sl->value() != want) {
            cp80_batt_sl->blockSignals(true);
            cp80_batt_sl->setValue(want);
            cp80_batt_sl->blockSignals(false);
        }
        cp80_batt_sl->setToolTip(QObject::tr(
            "Battery set: %1%. Drag to set the test level; a connected game "
            "can power the DATAprint even when the batteries are empty.")
                .arg(cp80_batt, 0, 'f', 1));
        cp80_batt_sl->update();
    }

    g.end();
    cp80_view->setPixmap(out);
    cp80_view->setFixedSize(DP3000_HEAD_W, H);

    auto place_button = [](QPushButton *button, int x, int y, int w, int h) {
        if (button == nullptr)
            return;
        button->setGeometry(x, y, w, h);
    };
    place_button(dp3000_case_feed, bx - 14, by + 592, 26, 45);
    place_button(dp3000_case_reset, bx + bw - 17, by + 594, 28, 34);
    place_button(dp3000_key_yes, key_centres[0] - 23, keyboard_y + 94, 46, 50);
    place_button(dp3000_key_init, key_centres[1] - 23, keyboard_y + 94, 46, 50);
    place_button(dp3000_key_no, key_centres[2] - 23, keyboard_y + 94, 46, 50);
    place_button(dp3000_card, card_x - 9, card_y - 3, 112, 37);
    cp80_resize_to_content();
    cp80_update_paper_hover();
}

static void
cp80_render()
{
    if (cp80_view == nullptr)
        return;

    if (cp80_model == Cp80PrinterModel::Dataprint3000) {
        cp80_render_dp3000();
        return;
    }

    if (cp80_body == nullptr)
        return;

    cp80_update_model_controls();

    const QFont mono = cp80_print_font();

    const QFontMetrics fm(mono);
    const int          lh = CP80_TEXT_LINE_H;

    const QStringList all = cp80_lines_on(cp80_printed);

    const int visible_lines = cp80_visible_line_capacity();

    /* Once the roll fills the usable work area, earlier lines ride out of sight.
       The scrollbar is how they are got back; it follows the bottom unless
       somebody has deliberately scrolled away from it. */
    const int over = qMax(0, all.size() - visible_lines);

    if (cp80_scroll != nullptr) {
        const bool at_end = (cp80_scroll->value() >= cp80_scroll->maximum());

        cp80_scroll->blockSignals(true);
        cp80_scroll->setRange(0, over);
        cp80_scroll->setPageStep(visible_lines);
        cp80_scroll->setSingleStep(1);
        if (at_end)
            cp80_scroll->setValue(over);
        cp80_scroll->blockSignals(false);
        cp80_scroll->setVisible((over > 0) && cp80_paper_hover);
    }

    const int first = (cp80_scroll != nullptr)
                    ? qBound(0, cp80_scroll->value(), over) : over;
    const bool tail_motion = (cp80_paper_frame < CP80_PAPER_FRAMES)
                          && (over > 0) && (first == over);
    const int line_first = tail_motion ? first - 1 : first;
    const int line_count = visible_lines + (tail_motion ? 1 : 0);
    const QStringList lines = all.mid(line_first, line_count);

    const bool crumpled = (over > 0);
    const int paper_rows = qMin(lines.size(), visible_lines);
    const int paperH = lines.isEmpty() ? CP80_EMPTY_LIP_H
                                      : ((paper_rows * lh) + CP80_PAPER_PAD_Y);
    const QStringList torn_all   = cp80_tearing ? cp80_lines_on(cp80_torn_text)
                                               : QStringList();
    const QStringList torn_lines = torn_all.mid(cp80_torn_first, visible_lines);
    const int tornH = torn_lines.isEmpty() ? 0
                                          : ((torn_lines.size() * lh)
                                             + CP80_PAPER_PAD_Y);
    /* Negative when the roll is longer than the machine is tall, which is the
       whole point of measuring it this way. */
    /* Rotation lifts the free upper corners.  Reserve that room while tearing
       so a long receipt does not lose its top edge against the label bounds. */
    const int tear_room = (tornH > 0) ? 48 : 0;
    const int edge_room = (paperH > 0) ? CP80_PAPER_EDGE_ROOM : 0;
    const int max_canvas = cp80_max_canvas_height();
    const int natural_top = qMin(0, CP80_SLOT_YS
                                    - qMax(paperH, tornH)
                                    - tear_room - edge_room);
    /* Once surplus paper has reached the ceiling, keep the canvas at its exact
       maximum.  The excess now buckles inside it; it must never make the window
       grow for one animation frame and shrink downward again when that line
       settles. */
    const int top    = crumpled ? CP80_HEAD_H - max_canvas
                               : qMax(CP80_HEAD_H - max_canvas, natural_top);
    const int H      = CP80_HEAD_H - top;

    QPixmap out = cp80_canvas(CP80_HEAD_W, H);

    out.fill(Qt::transparent);

    QPainter g(&out);

    g.setRenderHint(QPainter::Antialiasing, false);
    g.drawPixmap(0, -top, *cp80_body);
    cp80_draw_pressed_button(g, cp80_btn_on_down, CP80_BTN_ON_X,
                             CP80_BTN_ON_W, -top);
    cp80_draw_pressed_button(g, cp80_btn_fd_down, CP80_BTN_FD_X,
                             CP80_BTN_FD_W, -top);

    if (paperH > 0) {
        const int slot_y = -top + CP80_SLOT_YS;
        const QRect final_paper(CP80_PAPER_L, slot_y - paperH,
                                CP80_PAPER_W, paperH);
        const int crumple_depth = crumpled
            ? qMin(CP80_CRUMPLE_MAX_H, CP80_CRUMPLE_MIN_H + (over * 2)) : 0;
        const int motion_y = (!crumpled && (first == over))
                           ? cp80_paper_motion_offset(lh) : 0;
        const QRect r = final_paper.translated(0, motion_y);
        QPainterPath leading_edge;
        QPainterPath shadow_edge;
        const QPainterPath paper_shape = cp80_live_paper_shape(
            r, crumple_depth, &leading_edge, &shadow_edge);

        cp80_paper_hit_rect = r;

        /* The line rises from behind the slot instead of teleporting upward.
           Clipping at the slot is also what makes the paper look threaded
           through the mechanism rather than pasted over the photograph. */
        g.save();
        g.setClipRect(0, 0, CP80_HEAD_W, slot_y + 1);
        g.setRenderHint(QPainter::Antialiasing, true);

        /* Stroke the silhouette itself rather than an offset copy.  The shadow
           therefore begins on the exact first paper pixel at the torn edge.
           The lower edge is intentionally absent: it enters the photographed
           slot and must not acquire a synthetic dark seam across the paper. */
        g.setPen(QPen(QColor(24, 25, 31, 64), 2.0));
        g.drawPath(shadow_edge);
        g.setPen(Qt::NoPen);
        g.fillPath(paper_shape, QColor(CP80_PAPER_RGB));

        /* Low-contrast edge falloff gives the roll a shallow cross-sheet bow;
           sparse deterministic fibres stop a long blank receipt looking like a
           perfectly flat UI rectangle. */
        QLinearGradient bow(r.left(), 0, r.right(), 0);
        bow.setColorAt(0.00, QColor(80, 82, 91, 23));
        bow.setColorAt(0.10, QColor(255, 255, 255, 10));
        bow.setColorAt(0.52, QColor(255, 255, 255, 23));
        bow.setColorAt(0.88, QColor(255, 255, 255, 7));
        bow.setColorAt(1.00, QColor(73, 75, 84, 27));
        g.fillPath(paper_shape, bow);

        g.setClipPath(paper_shape, Qt::IntersectClip);
        g.setPen(QColor(255, 255, 255, 16));
        for (int y = r.top() + 19; y < r.bottom(); y += 37)
            g.drawLine(r.left() + 3, y, r.right() - 3, y);
        g.setPen(QColor(102, 104, 113, 10));
        for (int y = r.top() + 31; y < r.bottom(); y += 53)
            g.drawLine(r.left() + 5, y, r.right() - 4, y);

        g.setRenderHint(QPainter::Antialiasing, false);
        g.setRenderHint(QPainter::TextAntialiasing, false);
        g.setFont(mono);

        /* Each line in the ink it was printed with.  A receipt that began on a
           good pack and finished on a flat one reads that way -- black at the
           top, gone at the bottom -- which is what the paper would look like.
           Colouring the whole roll from the present level would rewrite the
           earlier lines every time a new one arrived. */
        const int text_motion = tail_motion
                              ? cp80_paper_motion_offset(lh) - lh : 0;
        int y = r.top() + text_motion + 6 + fm.ascent();
        for (int i = 0; i < lines.size(); i++) {
            const int    at = line_first + i;
            const double bt = (at < cp80_line_batt.size())
                            ? cp80_line_batt.at(at) : CP80_BATT_FULL;

            g.setPen(cp80_mix(QColor(CP80_INK_RGB), QColor(CP80_PAPER_RGB),
                              cp80_fade_at(bt)));
            cp80_draw_print_line(g, r.left() + CP80_TEXT_MARGIN_X, y,
                                 lines.at(i));
            y += lh;
        }

        cp80_draw_crumple(g, r, crumple_depth, over);
        g.restore();

        /* Surface effects and folds are allowed to meet the cut, never cover
           it.  Drawing this last also leaves the leading edge visible after a
           feed animation has fully settled at the top of the window. */
        g.save();
        g.setClipRect(0, 0, CP80_HEAD_W, slot_y + 1);
        g.setRenderHint(QPainter::Antialiasing, true);
        g.setPen(QPen(QColor(83, 85, 94, 118), 1.25));
        g.drawPath(leading_edge);
        g.restore();

        /* Inside the paper, against its right edge -- it belongs to the roll
           rather than to the window. */
        if ((cp80_scroll != nullptr) && (over > 0)) {
            const int scroll_h = (visible_lines * lh) + CP80_PAPER_PAD_Y;
            const QRect scroll_paper(CP80_PAPER_L,
                                     slot_y - scroll_h,
                                     CP80_PAPER_W, scroll_h);
            const int scroll_top = scroll_paper.top() + crumple_depth + 3;

            cp80_scroll->setGeometry(
                scroll_paper.right() - 12, scroll_top, 9,
                qMax(20, scroll_paper.bottom() - scroll_top - 3));
        }
    }

    if (tornH > 0) {
        /* Cut a real silhouette rather than rotating a rectangular widget.  A
           repeating but non-uniform tooth profile reads as the DPU-414's tear
           bar at native size and stays deterministic between repaints. */
        QPixmap      sheet = cp80_canvas(CP80_PAPER_W, tornH);
        QPainterPath shape;
        QPainterPath torn_edge;

        sheet.fill(Qt::transparent);
        shape.moveTo(0, 0);
        shape.lineTo(CP80_PAPER_W, 0);
        shape.lineTo(CP80_PAPER_W, tornH - 3);
        torn_edge.moveTo(CP80_PAPER_W, tornH - 3);
        for (int x = CP80_PAPER_W; x >= 0; x -= 7) {
            static const int tooth[] = { 1, 4, 2, 5, 2, 3, 0, 4 };
            const int at = (x / 7) & 7;

            shape.lineTo(x, tornH - 1 - tooth[at]);
            torn_edge.lineTo(x, tornH - 1 - tooth[at]);
        }
        shape.lineTo(0, tornH - 2);
        torn_edge.lineTo(0, tornH - 2);
        shape.lineTo(0, 0);
        shape.closeSubpath();

        {
            QPainter s(&sheet);

            s.setRenderHint(QPainter::Antialiasing, true);
            s.setClipPath(shape);
            s.fillPath(shape, QColor(CP80_PAPER_RGB));

            /* A faint cross-sheet gradient and a moving lower-edge shadow make
               the flat label bend as it is pulled; ink stays on the surface. */
            QLinearGradient cross(0, 0, CP80_PAPER_W, 0);
            cross.setColorAt(0.00, QColor(255, 255, 255, 22));
            cross.setColorAt(0.56, QColor(255, 255, 255, 0));
            cross.setColorAt(1.00, QColor(105, 108, 122, 19));
            s.fillPath(shape, cross);

            QLinearGradient curl(0, qMax(0, tornH - 30), 0, tornH);
            const int curl_alpha = int(12.0 + (30.0 * cp80_smoothstep(
                                             (double) cp80_tear_frame
                                             / (CP80_TEAR_FRAMES * 0.70))));
            curl.setColorAt(0.0, QColor(80, 84, 96, 0));
            curl.setColorAt(1.0, QColor(80, 84, 96, curl_alpha));
            s.fillPath(shape, curl);

            s.setRenderHint(QPainter::TextAntialiasing, false);
            s.setFont(mono);
            int y = 6 + fm.ascent();
            for (int i = 0; i < torn_lines.size(); i++) {
                const int    at = cp80_torn_first + i;
                const double bt = (at < cp80_torn_batt.size())
                                ? cp80_torn_batt.at(at) : CP80_BATT_FULL;

                s.setPen(cp80_mix(QColor(CP80_INK_RGB), QColor(CP80_PAPER_RGB),
                                  cp80_fade_at(bt)));
                cp80_draw_print_line(s, CP80_TEXT_MARGIN_X, y,
                                     torn_lines.at(i));
                y += lh;
            }

            s.setClipping(false);
            s.setPen(QColor(92, 94, 104, 105));
            s.drawPath(torn_edge);
        }

        const double t       = qBound(0.0, (double) cp80_tear_frame
                                          / (double) (CP80_TEAR_FRAMES - 1), 1.0);
        const double across  = cp80_smoothstep(t / 0.70);
        const double release = cp80_smoothstep((t - 0.70) / 0.30);
        const double angle   = (5.2 * across) + (2.3 * release)
                             + (0.8 * sin(CP80_UI_PI * release));
        const double dx      = 16.0 * release;
        const double dy      = 32.0 * release * release;
        const double opacity = 1.0 - cp80_smoothstep((t - 0.78) / 0.22);
        const QPointF origin(CP80_PAPER_L,
                             (-top + CP80_SLOT_YS) - tornH);
        const QPointF pivot(origin.x() + CP80_PAPER_W,
                            origin.y() + tornH - 2);
        QTransform motion;

        /* The rightmost tooth is the pivot while the tear travels left to
           right.  Once it releases, the receipt carries on forward and down
           with a small elastic overshoot instead of simply fading in place. */
        motion.translate(dx, dy);
        motion.translate(pivot.x(), pivot.y());
        motion.rotate(angle);
        motion.scale(1.0 - (0.018 * sin(CP80_UI_PI * t)), 1.0);
        motion.translate(-pivot.x(), -pivot.y());

        QPainterPath shadow_shape = shape.translated(origin);

        g.save();
        g.setRenderHint(QPainter::Antialiasing, true);
        g.setWorldTransform(motion);
        g.translate(3.0, 5.0);
        g.setOpacity(0.20 * opacity);
        g.fillPath(shadow_shape, QColor(25, 27, 34));
        g.restore();

        g.save();
        g.setRenderHint(QPainter::SmoothPixmapTransform, true);
        g.setWorldTransform(motion);
        g.setOpacity(opacity);
        g.drawPixmap(origin, sheet);
        g.restore();
    }

    /* The three lamps.  They are lit or they are not -- no dimming with the
       pack, because that is not what the machine does: a low battery is
       announced by the Power LED blinking, and the manual is explicit about the
       rates.  Section 2.10: once a second while charging, steady when full.
       Section, low pack during printing: about twice a second, and the printer
       goes OFFLINE. */
    {
        const bool online = cp80_power && (prn_cp80_connected() != 0);
        const bool low    = (cp80_batt <= CP80_BATT_FLAT);   /* flat: 0% */
        const bool full   = (cp80_batt >= CP80_BATT_FULL);

        g.setRenderHint(QPainter::Antialiasing, true);
        g.setPen(Qt::NoPen);

        /* The tick is 250 ms and a blink is a whole cycle, on and off -- so a
           half-second blink is on for one tick and off for the next, and a
           one-second blink is two ticks each way.  Getting that wrong by a
           factor of two is easy and it was: the LED was toggling every half
           second, which is a one-second blink, not a half-second one.

               CP80_BLINK_FAST   one tick on, one off   -> 0.5 s, the flat pack
               CP80_BLINK_SLOW   two ticks on, two off  -> 1 s, charging */
        const bool fast = ((cp80_blink & 1) == 0);
        const bool slow = (((cp80_blink / 2) & 1) == 0);

        /* Two separate lamps, drawn separately.  They were being treated as one
           indicator that moved between two positions, so a job stuck in the
           buffer lit the ONLINE lamp and the OFFLINE lamp was never drawn at
           all -- which is backwards: the printer is *offline*, and that is the
           lamp that should be on.  A machine with two LEDs can light both. */
        if (cp80_power) {
            /* OFFLINE: lit whenever it is not online.  Steady, because what is
               blinking on a low pack is the Power LED. */
            if (!online) {
                const int   ly = -top + CP80_SCALE(CP80_LED_OFF_Y);
                const QRect led(CP80_SCALE(CP80_LED_OFF_X), ly,
                                CP80_SCALE(CP80_LED_W), CP80_SCALE(CP80_LED_H));

                g.setBrush(QColor(0xe0, 0x22, 0x18));
                g.drawRoundedRect(led, 1, 1);
            }

            /* ONLINE: lit when it is online, and blinking when it is not but
               still holding a job -- "the ONLINE LED will blink if there is data
               left in the memory buffer", section 2.11. */
            const bool waiting = !online && !cp80_queued.isEmpty();

            if (online || (waiting && slow)) {
                const int   ly = -top + CP80_SCALE(CP80_LED_ON_Y);
                const QRect led(CP80_SCALE(CP80_LED_ON_X), ly,
                                CP80_SCALE(CP80_LED_W), CP80_SCALE(CP80_LED_H));

                g.setBrush(QColor(0x35, 0xd0, 0x4a));
                g.drawRoundedRect(led, 1, 1);
            }
        }

        /* Steady when it is simply on; once a second while charging; twice a
           second -- every half second -- when the pack is flat. */
        bool pwr_lit = cp80_power;

        if (cp80_power && cp80_charging && !full)
            pwr_lit = slow;
        else if (cp80_power && low)
            pwr_lit = fast;

        if (pwr_lit) {
            const int x0 = CP80_SCALE(CP80_LED_PWR_X0);
            const int y0 = CP80_SCALE(CP80_LED_PWR_Y0);
            const QRect pwr(x0, -top + y0,
                            CP80_SCALE(CP80_LED_PWR_X1) - x0,
                            CP80_SCALE(CP80_LED_PWR_Y1) - y0);

            g.setBrush(QColor(0x35, 0xd0, 0x4a));
            g.drawRect(pwr);
        }
    }

    if (cp80_win != nullptr)
        cp80_win->setWindowTitle(
            cp80_power ? QObject::tr("Seiko DPU-414 — %1%%2")
                             .arg(cp80_batt, 0, 'f', 2)
                             .arg(cp80_charging ? QObject::tr(", charging")
                                  : (cp80_ac ? QObject::tr(", on the adapter")
                                             : QString()))
                       : QObject::tr("Seiko DPU-414 — off"));

    /* Always there now that it unplugs as well as plugs in: an adapter is a
       thing on the desk, not a button that appears when the machine is in
       trouble. */
    if (cp80_replace != nullptr) {
        cp80_replace->setText(cp80_ac ? QObject::tr("Disconnect adapter")
                                      : QObject::tr("Connect adapter"));
        cp80_replace->setChecked(cp80_ac);
    }
    if (cp80_pwr_btn != nullptr)
        cp80_pwr_btn->setText(cp80_power ? QObject::tr("Power: on")
                                         : QObject::tr("Power: off"));

    /* The battery control follows the pack when it moves on its own; blocked so
       that following it does not read back as the user having dragged it. */
    if (cp80_batt_sl != nullptr) {
        const int want = int(cp80_batt + 0.5);

        if (cp80_batt_sl->value() != want) {
            cp80_batt_sl->blockSignals(true);
            cp80_batt_sl->setValue(want);
            cp80_batt_sl->blockSignals(false);
        }
        cp80_batt_sl->setToolTip(
            QObject::tr("Battery pack: %1%. Drag to set the test level.")
                .arg(cp80_batt, 0, 'f', 1));
        cp80_batt_sl->update();
    }

    g.end();
    cp80_view->setPixmap(out);
    cp80_view->setFixedSize(CP80_HEAD_W, H);

    /* The panel buttons ride with the picture.  The machine is always flush
       with the bottom of the pixmap, so their offset from the bottom never
       changes however long the roll gets. */
    const int base = H - CP80_HEAD_H;

    if (cp80_btn_on != nullptr)
        cp80_btn_on->setGeometry(
            CP80_SCALE(CP80_BTN_ON_X), base + CP80_SCALE(CP80_BTN_Y),
            CP80_SCALE(CP80_BTN_ON_W), CP80_SCALE(CP80_BTN_H));
    if (cp80_btn_fd != nullptr)
        cp80_btn_fd->setGeometry(
            CP80_SCALE(CP80_BTN_FD_X), base + CP80_SCALE(CP80_BTN_Y),
            CP80_SCALE(CP80_BTN_FD_W), CP80_SCALE(CP80_BTN_H));

    cp80_resize_to_content();
    cp80_update_paper_hover();
}

static bool
dp3000_keyboard_ready()
{
    if ((cp80_model != Cp80PrinterModel::Dataprint3000)
        || !dp3000_keyboard_connected || !cp80_ac)
        return false;

    /* A key is itself one of the manual's ways of switching the DATAprint on.
       This matters after RESET has put an otherwise connected idle unit to
       sleep: the next keyboard operation must wake it again. */
    cp80_power = true;
    return true;
}

static QString
dp3000_localised(const char *german, const char *english)
{
    return QString::fromUtf8(dp3000_english ? english : german);
}

static bool
dp3000_has_stored_data()
{
    return !dp3000_records.isEmpty();
}

static void
dp3000_arm_input_timeout()
{
    /* The one-minute operator interval starts after the question has emerged
       from the printer, not while its (sometimes multi-line) prompt is still
       being printed. */
    dp3000_input_timeout_requested = true;
    if (dp3000_input_timeout != nullptr) {
        dp3000_input_timeout->stop();
        if (!dp3000_keyboard_job && cp80_queued.isEmpty())
            dp3000_input_timeout->start(60000);
    }
}

static void
dp3000_cancel_input_timeout()
{
    dp3000_input_timeout_requested = false;
    if (dp3000_input_timeout != nullptr)
        dp3000_input_timeout->stop();
}

static QString
dp3000_format_record(const QString &record, Dp3000PrintFormat format)
{
    if (format == Dp3000PrintFormat::Maximum)
        return record;

    const QStringList lines = record.split(QLatin1Char('\n'));
    QStringList out;
    bool in_cash = false;

    for (int i = 0; i < lines.size(); i++) {
        const QString line = lines.at(i);
        const QString lower = line.toLower();
        const QString trimmed = line.trimmed();
        bool keep = false;

        switch (format) {
            case Dp3000PrintFormat::Medium:
                keep = !trimmed.isEmpty();
                if (trimmed.startsWith(QStringLiteral("---"))
                    && !out.isEmpty()
                    && out.constLast().trimmed().startsWith(QStringLiteral("---")))
                    keep = false;
                break;

            case Dp3000PrintFormat::Short:
                keep = (i < 4) || lower.contains(QStringLiteral("serial"))
                    || lower.contains(QStringLiteral("date"))
                    || lower.contains(QStringLiteral("total"))
                    || lower.contains(QStringLiteral("gesamt"))
                    || lower.startsWith(QStringLiteral("pr "));
                break;

            case Dp3000PrintFormat::CashBag:
                if (lower.contains(QStringLiteral("coin"))
                    || lower.contains(QStringLiteral("note"))
                    || lower.contains(QStringLiteral("kasse"))
                    || lower.contains(QStringLiteral("cash")))
                    in_cash = true;
                keep = (i < 4) || in_cash || lower.contains(QStringLiteral("total"))
                    || lower.contains(QStringLiteral("gesamt"));
                break;

            case Dp3000PrintFormat::Custom:
                /* The V4 custom block mask was authored in DATAcontact.  The
                   simulator exposes the documented on/off choice: enabled uses
                   the compact operator fields; disabled falls back to maximum. */
                if (!dp3000_settings.first_custom)
                    return record;
                keep = (i < 4) || lower.contains(QStringLiteral("book"))
                    || lower.contains(QStringLiteral("kasse"))
                    || lower.contains(QStringLiteral("action"))
                    || lower.contains(QStringLiteral("total"))
                    || lower.contains(QStringLiteral("gesamt"));
                break;

            case Dp3000PrintFormat::Maximum:
                keep = true;
                break;
        }
        if (keep)
            out.append(line);
    }

    QString result = out.join(QLatin1Char('\n'));
    if (!result.endsWith(QLatin1Char('\n')))
        result += QLatin1Char('\n');
    return result;
}

static QString
dp3000_settings_status(bool complete)
{
    static const char *const lengths_de[] = {
        "MAXIMALER AUSDRUCK", "MITTELLANGER AUSDRUCK", "KURZER AUSDRUCK",
        "GELDSACKBELEG", "SELBSTDEF. AUSDRUCK", "KEIN AUSDRUCK"
    };
    static const char *const lengths_en[] = {
        "MAXIMUM PRINTOUT", "MEDIUM PRINTOUT", "SHORT PRINTOUT",
        "CASH-BAG RECEIPT", "CUSTOM PRINTOUT", "NO PRINTOUT"
    };
    static const int baud[] = { 2400, 4800, 9600, 19200, 38400 };
    const char *const *lengths = dp3000_english ? lengths_en : lengths_de;
    QString s = QStringLiteral("------------------------\nSTATUS\n"
                               "------------------------\n")
              + dp3000_localised("  8KB PUFFER VORHANDEN\n",
                                 "  8KB BUFFER AVAILABLE\n");

    if (dp3000_card_inserted) {
        const int available = qMax(0, DP3000_STORE_MAX
                                      - dp3000_records_bytes(dp3000_records));
        s += dp3000_localised("%1KB SPEICHER FREI\n",
                              "%1KB MEMORY AVAILABLE\n")
                 .arg(available / 1024, 4);
        s += dp3000_has_stored_data()
           ? dp3000_localised("DATEN SIND GESPEICHERT\n",
                              "DATA IS STORED\n")
           : dp3000_text(Dp3000Text::NoData) + QLatin1Char('\n');
    } else {
        s += dp3000_text(Dp3000Text::NoMemoryCard) + QLatin1Char('\n');
    }

    if (!complete)
        return s;

    s += QStringLiteral("========================\n")
       + dp3000_localised("AKTUELLE EINSTELLUNG:\n",
                          "CURRENT SETTINGS:\n")
       + QStringLiteral("------------------------\n");
    s += dp3000_clock_now().toString(QStringLiteral("dd.MM.yy/hh:mm"))
       + QStringLiteral("     V4.00\n");
    s += dp3000_localised("DATAPRINT:          %1\n",
                          "DATAPRINT:          %1\n")
             .arg(dp3000_settings.dataprint_number, 4, 10, QLatin1Char('0'));
    s += dp3000_localised("SPEICHERKARTE:      %1\n",
                          "MEMORY CARD:        %1\n")
             .arg(dp3000_settings.card_number, 4, 10, QLatin1Char('0'));
    s += dp3000_settings.storage_mode
       ? dp3000_localised("SPEICHERBETRIEB\n", "STORAGE MODE\n")
       : dp3000_localised("NUR-DRUCKER-BETRIEB\n", "PRINTER-ONLY MODE\n");
    s += dp3000_localised("1.AUSDRUCK: %1\n", "FIRST PRINTOUT: %1\n")
             .arg(QString::fromLatin1(lengths[qBound(0,
                   dp3000_settings.first_print_length, 5)]));
    s += dp3000_localised("2.AUSDRUCK: %1\n", "SECOND PRINTOUT: %1\n")
             .arg(QString::fromLatin1(lengths[qBound(0,
                   dp3000_settings.second_print_length, 5)]));
    s += dp3000_localised("PC-BAUDRATE %1 BAUD\n", "PC BAUD RATE %1 BAUD\n")
             .arg(baud[qBound(0, dp3000_settings.pc_baud, 4)]);
    s += dp3000_localised("SCHLUESSEL: %1\n", "KEY CODE: %1\n")
             .arg(dp3000_settings.key_code);
    s += dp3000_localised("MEHRWERTSTEUER %1,%2%\n", "VAT RATE %1.%2%\n")
             .arg(dp3000_settings.vat_half_percent / 2)
             .arg((dp3000_settings.vat_half_percent & 1) ? 5 : 0);
    return s;
}

static QString
dp3000_setting_name(Dp3000Setting setting)
{
    static const char *const german[] = {
        "DATUM/UHRZEIT", "DATAPRINT NUMMER", "SPEICHERKARTEN NUMMER",
        "BETRIEBSART", "LAENGE 1. AUSDRUCK", "SELBSTDEF. 1. AUSDRUCK",
        "LAENGE 2. AUSDRUCK", "SELBSTDEF. 2. AUSDRUCK", "KASSENBELEG",
        "BETRIEB OHNE KARTE", "NUMMERN DRUCKEN", "GERAETEAUSWERTUNG",
        "GERAETEEINSTELLUNG UEBERSCHREIBEN", "PC-BAUDRATE", "SCHLUESSEL",
        "AUSWERTUNGSART", "GERAETEDATEN LOESCHEN", "MIT STATISTIK",
        "MIT KOPIE", "MIT LISTE", "MIT KONTROLLE", "IMPFCODE LOESCHEN",
        "UHR-STELLER", "MEHRWERTSTEUER"
    };
    static const char *const english[] = {
        "DATE/TIME", "DATAPRINT NUMBER", "MEMORY-CARD NUMBER",
        "OPERATING MODE", "FIRST PRINTOUT LENGTH", "CUSTOM FIRST PRINTOUT",
        "SECOND PRINTOUT LENGTH", "CUSTOM SECOND PRINTOUT", "CASH RECEIPT",
        "OPERATION WITHOUT CARD", "PRINT NUMBERS", "MACHINE EVALUATION",
        "OVERWRITE MACHINE SETTINGS", "PC BAUD RATE", "KEY CODE",
        "EVALUATION TYPE", "DELETE MACHINE DATA", "INCLUDE STATISTICS",
        "INCLUDE COPY", "INCLUDE LIST", "CHECK TRANSFER", "DELETE ADP CODE",
        "SET MACHINE CLOCK", "VAT RATE"
    };
    const int i = static_cast<int>(setting);
    return QString::fromUtf8((dp3000_english ? english : german)[i]);
}

static QString
dp3000_setting_value(Dp3000Setting setting)
{
    static const char *const lengths_de[] = { "MAXIMAL", "MITTEL", "KURZ", "GELDSACK", "SELBSTDEF.", "KEINER" };
    static const char *const lengths_en[] = { "MAXIMUM", "MEDIUM", "SHORT", "CASH-BAG", "CUSTOM", "NONE" };
    static const int baud[] = { 2400, 4800, 9600, 19200, 38400 };
    const char *const *lengths = dp3000_english ? lengths_en : lengths_de;
    auto yesno = [](bool v) {
        return v ? dp3000_localised("EIN", "ON")
                 : dp3000_localised("AUS", "OFF");
    };

    switch (setting) {
        case Dp3000Setting::DateTime: return dp3000_clock_now().toString(QStringLiteral("dd.MM.yy hh:mm"));
        case Dp3000Setting::DataprintNumber: return QStringLiteral("%1").arg(dp3000_settings.dataprint_number, 4, 10, QLatin1Char('0'));
        case Dp3000Setting::CardNumber: return QStringLiteral("%1").arg(dp3000_settings.card_number, 4, 10, QLatin1Char('0'));
        case Dp3000Setting::StorageMode: return dp3000_settings.storage_mode ? dp3000_localised("SPEICHER", "STORAGE") : dp3000_localised("NUR-DRUCKER", "PRINTER ONLY");
        case Dp3000Setting::FirstPrintLength: return QString::fromLatin1(lengths[dp3000_settings.first_print_length]);
        case Dp3000Setting::FirstCustom: return yesno(dp3000_settings.first_custom);
        case Dp3000Setting::SecondPrintLength: return QString::fromLatin1(lengths[dp3000_settings.second_print_length]);
        case Dp3000Setting::SecondCustom: return yesno(dp3000_settings.second_custom);
        case Dp3000Setting::CashReceipt: return yesno(dp3000_settings.cash_receipt);
        case Dp3000Setting::WithoutCard: return yesno(dp3000_settings.without_card);
        case Dp3000Setting::PrintNumbers: return yesno(dp3000_settings.print_numbers);
        case Dp3000Setting::EvaluationEnabled: return yesno(dp3000_settings.evaluation_enabled);
        case Dp3000Setting::OverwriteMachineSettings: return yesno(dp3000_settings.overwrite_machine_settings);
        case Dp3000Setting::PcBaud: return QString::number(baud[dp3000_settings.pc_baud]);
        case Dp3000Setting::KeyCode: return dp3000_settings.key_code;
        case Dp3000Setting::EvaluationType: return QString::number(dp3000_settings.evaluation_type + 1);
        case Dp3000Setting::DeleteMachineData: return yesno(dp3000_settings.delete_machine_data);
        case Dp3000Setting::Statistics: return yesno(dp3000_settings.statistics);
        case Dp3000Setting::Copy: return yesno(dp3000_settings.copy);
        case Dp3000Setting::List: return yesno(dp3000_settings.list);
        case Dp3000Setting::Control: return yesno(dp3000_settings.control);
        case Dp3000Setting::DeleteAdpCode: return yesno(dp3000_settings.delete_adp_code);
        case Dp3000Setting::ClockSetter: return yesno(dp3000_settings.clock_setter);
        case Dp3000Setting::Vat: return QStringLiteral("%1,%2%").arg(dp3000_settings.vat_half_percent / 2).arg((dp3000_settings.vat_half_percent & 1) ? 5 : 0);
        case Dp3000Setting::Count: break;
    }
    return QString();
}

static void
dp3000_operator_queue(const QString &message)
{
    QString text = message;

    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    cp80_queued += text;
    dp3000_keyboard_job = true;
    dp3000_keyboard_paused = false;
    dp3000_transfer_error = false;
    cp80_power = true;
    cp80_render();
}

static void
dp3000_finish_settings()
{
    dp3000_device_settings_save();
    dp3000_store_save();
    dp3000_keyboard_state = Dp3000KeyboardState::Idle;
    dp3000_cancel_input_timeout();
    dp3000_operator_queue(dp3000_settings_status(true)
        + QStringLiteral("------------------------\n")
        + dp3000_localised("ENDE", "END"));
}

static void
dp3000_settings_parameter_prompt()
{
    const Dp3000Setting setting = static_cast<Dp3000Setting>(dp3000_setting_index);
    dp3000_keyboard_state = Dp3000KeyboardState::SettingsParameter;
    dp3000_operator_queue(dp3000_setting_name(setting) + QLatin1Char('\n')
        + dp3000_setting_value(setting) + QLatin1Char('\n')
        + dp3000_localised("AENDERN? ...JA/NEIN", "CHANGE? ...YES/NO"));
    dp3000_arm_input_timeout();
}

static void
dp3000_settings_next_parameter()
{
    dp3000_setting_index++;
    const int evaluation_first = static_cast<int>(Dp3000Setting::KeyCode);
    const int count = static_cast<int>(Dp3000Setting::Count);

    if (dp3000_setting_index == evaluation_first) {
        dp3000_keyboard_state = Dp3000KeyboardState::SettingsEvaluationBlock;
        dp3000_operator_queue(dp3000_localised(
            "AUSWERTUNGS-PARAMETER\nAENDERN? ...JA/NEIN",
            "EVALUATION PARAMETERS\nCHANGE? ...YES/NO"));
        dp3000_arm_input_timeout();
    } else if (dp3000_setting_index >= count) {
        dp3000_finish_settings();
    } else {
        dp3000_settings_parameter_prompt();
    }
}

static void
dp3000_setting_adjust(int delta)
{
    const Dp3000Setting setting = static_cast<Dp3000Setting>(dp3000_setting_index);
    auto toggle = [delta](int &v) { if (delta != 0) v = !v; };

    switch (setting) {
        case Dp3000Setting::DateTime:
        {
            QDateTime clock = dp3000_clock_now();
            switch (dp3000_edit_subindex) {
                case 0: clock = clock.addYears(delta); break;
                case 1: clock = clock.addMonths(delta); break;
                case 2: clock = clock.addDays(delta); break;
                case 3: clock = clock.addSecs(delta * 3600); break;
                default: clock = clock.addSecs(delta * 60); break;
            }
            dp3000_clock_set(clock);
            break;
        }
        case Dp3000Setting::DataprintNumber: {
            static const int place[] = { 1000, 100, 10, 1 };
            const int p = place[qBound(0, dp3000_edit_subindex, 3)];
            int digit = (dp3000_settings.dataprint_number / p) % 10;
            digit = (digit + delta + 10) % 10;
            dp3000_settings.dataprint_number =
                dp3000_settings.dataprint_number - (((dp3000_settings.dataprint_number / p) % 10) * p) + (digit * p);
            break;
        }
        case Dp3000Setting::CardNumber: {
            static const int place[] = { 1000, 100, 10, 1 };
            const int p = place[qBound(0, dp3000_edit_subindex, 3)];
            int digit = (dp3000_settings.card_number / p) % 10;
            digit = (digit + delta + 10) % 10;
            dp3000_settings.card_number =
                dp3000_settings.card_number - (((dp3000_settings.card_number / p) % 10) * p) + (digit * p);
            break;
        }
        case Dp3000Setting::StorageMode: toggle(dp3000_settings.storage_mode); break;
        case Dp3000Setting::FirstPrintLength:
            dp3000_settings.first_print_length = (dp3000_settings.first_print_length + delta + 6) % 6;
            break;
        case Dp3000Setting::FirstCustom: toggle(dp3000_settings.first_custom); break;
        case Dp3000Setting::SecondPrintLength:
            dp3000_settings.second_print_length = (dp3000_settings.second_print_length + delta + 6) % 6;
            break;
        case Dp3000Setting::SecondCustom: toggle(dp3000_settings.second_custom); break;
        case Dp3000Setting::CashReceipt: toggle(dp3000_settings.cash_receipt); break;
        case Dp3000Setting::WithoutCard: toggle(dp3000_settings.without_card); break;
        case Dp3000Setting::PrintNumbers: toggle(dp3000_settings.print_numbers); break;
        case Dp3000Setting::EvaluationEnabled: toggle(dp3000_settings.evaluation_enabled); break;
        case Dp3000Setting::OverwriteMachineSettings: toggle(dp3000_settings.overwrite_machine_settings); break;
        case Dp3000Setting::PcBaud:
            dp3000_settings.pc_baud = (dp3000_settings.pc_baud + delta + 5) % 5;
            break;
        case Dp3000Setting::KeyCode: {
            const int at = qBound(0, dp3000_edit_subindex, 7);
            int digit = dp3000_settings.key_code.mid(at, 1).toInt();
            digit = (digit + delta + 10) % 10;
            dp3000_settings.key_code[at] = QChar(QLatin1Char('0').unicode() + digit);
            break;
        }
        case Dp3000Setting::EvaluationType:
            dp3000_settings.evaluation_type = (dp3000_settings.evaluation_type + delta + 6) % 6;
            break;
        case Dp3000Setting::DeleteMachineData: toggle(dp3000_settings.delete_machine_data); break;
        case Dp3000Setting::Statistics: toggle(dp3000_settings.statistics); break;
        case Dp3000Setting::Copy: toggle(dp3000_settings.copy); break;
        case Dp3000Setting::List: toggle(dp3000_settings.list); break;
        case Dp3000Setting::Control: toggle(dp3000_settings.control); break;
        case Dp3000Setting::DeleteAdpCode: toggle(dp3000_settings.delete_adp_code); break;
        case Dp3000Setting::ClockSetter: toggle(dp3000_settings.clock_setter); break;
        case Dp3000Setting::Vat:
            dp3000_settings.vat_half_percent = qBound(28,
                dp3000_settings.vat_half_percent + delta, 40);
            break;
        case Dp3000Setting::Count: break;
    }

    const int edit_parts = (setting == Dp3000Setting::DateTime) ? 5
                         : ((setting == Dp3000Setting::DataprintNumber)
                            || (setting == Dp3000Setting::CardNumber)) ? 4
                         : (setting == Dp3000Setting::KeyCode) ? 8 : 1;
    dp3000_operator_queue(dp3000_setting_name(setting) + QLatin1Char('\n')
        + dp3000_setting_value(setting) + QStringLiteral(" [%1/%2]")
              .arg(dp3000_edit_subindex + 1).arg(edit_parts)
        + dp3000_localised("\n+/- AENDERN, INIT WEITER",
                           "\n+/- CHANGE, INIT NEXT"));
    dp3000_arm_input_timeout();
}

static void
dp3000_begin_settings_after_status()
{
    dp3000_keyboard_state = Dp3000KeyboardState::SettingsShowCurrent;
    dp3000_operator_queue(dp3000_settings_status(false)
        + dp3000_localised("DIE AKTUELLE EINSTELLUNG\nZEIGEN? ...JA/NEIN",
                           "SHOW CURRENT SETTINGS?\n...YES/NO"));
    dp3000_arm_input_timeout();
}

static void
dp3000_begin_print_job(Dp3000PrintFormat format)
{
    dp3000_print_job_records.clear();
    dp3000_print_format = format;
    for (auto it = dp3000_records.crbegin(); it != dp3000_records.crend(); ++it)
        dp3000_print_job_records.append(dp3000_format_record(*it, format));

    dp3000_print_job_record = 0;
    dp3000_keyboard_state = Dp3000KeyboardState::Idle;
    if (dp3000_print_job_records.isEmpty()) {
        dp3000_operator_queue(dp3000_text(Dp3000Text::NoData));
        return;
    }
    dp3000_operator_queue(dp3000_print_job_records.constFirst());
}

static bool
dp3000_keyboard_begin()
{
    if (dp3000_keyboard_ready()) {
        dp3000_cancel_input_timeout();
        return true;
    }

    /* The manual requires both the keyboard and mains adapter, but it does not
       classify a key press without them as a transfer fault.  A disconnected
       keyboard cannot signal the DATAprint at all, and without the adapter the
       keyboard interface is simply unavailable: leave the key's mechanical
       click audible, but do not light FEHLER PC/AUTOMAT or start an alarm. */
    cp80_render();
    return false;
}

static bool
dp3000_is_print_prompt(Dp3000KeyboardState state)
{
    return (state == Dp3000KeyboardState::PrintMaximal)
        || (state == Dp3000KeyboardState::PrintMedium)
        || (state == Dp3000KeyboardState::PrintShort)
        || (state == Dp3000KeyboardState::PrintBag)
        || (state == Dp3000KeyboardState::PrintCustom);
}

static void
dp3000_print_prompt(Dp3000KeyboardState state, Dp3000Text question)
{
    dp3000_keyboard_state = state;
    dp3000_operator_queue(dp3000_text(question) + QLatin1Char('\n')
                          + dp3000_text(Dp3000Text::YesNo));
    dp3000_arm_input_timeout();
}

static void
dp3000_print_stored(Dp3000PrintFormat format)
{
    if (!dp3000_card_inserted) {
        dp3000_keyboard_state = Dp3000KeyboardState::Idle;
        dp3000_operator_queue(dp3000_text(Dp3000Text::NoCard));
    } else if (!dp3000_has_stored_data()) {
        dp3000_keyboard_state = Dp3000KeyboardState::Idle;
        dp3000_operator_queue(dp3000_text(Dp3000Text::NoData));
    } else {
        dp3000_begin_print_job(format);
        if (!dp3000_paper_loaded) {
            dp3000_keyboard_paused = true;
            dp3000_paper_restart_required = true;
        }
    }
}

static void
dp3000_keyboard_yes()
{
    if (!dp3000_keyboard_begin())
        return;

    /* Section 4.2.1 requires another press of DRUCKEN after a new roll is
       fitted.  Merely toggling the service control must not restart the motor. */
    if (dp3000_paper_restart_required) {
        if (!dp3000_paper_loaded) {
            cp80_render();
            return;
        }
        dp3000_paper_restart_required = false;
        dp3000_keyboard_paused = false;
        if ((dp3000_print_job_record >= 0)
            && (dp3000_print_job_record < dp3000_print_job_records.size()))
            cp80_queued = dp3000_print_job_records.at(dp3000_print_job_record);
        cp80_power = true;
        cp80_render();
        return;
    }

    switch (dp3000_keyboard_state) {
        case Dp3000KeyboardState::PrintMaximal:
            dp3000_print_stored(Dp3000PrintFormat::Maximum);
            break;
        case Dp3000KeyboardState::PrintMedium:
            dp3000_print_stored(Dp3000PrintFormat::Medium);
            break;
        case Dp3000KeyboardState::PrintShort:
            dp3000_print_stored(Dp3000PrintFormat::Short);
            break;
        case Dp3000KeyboardState::PrintBag:
            dp3000_print_stored(Dp3000PrintFormat::CashBag);
            break;
        case Dp3000KeyboardState::PrintCustom:
            dp3000_print_stored(Dp3000PrintFormat::Custom);
            break;

        case Dp3000KeyboardState::DeleteConfirm:
            dp3000_keyboard_state = Dp3000KeyboardState::DeleteAgain;
            dp3000_operator_queue(
                dp3000_text(Dp3000Text::PressDeleteAgain));
            break;

        case Dp3000KeyboardState::InitialiseConfirm:
            dp3000_keyboard_state = Dp3000KeyboardState::InitialiseAgain;
            dp3000_operator_queue(
                dp3000_text(Dp3000Text::InitialiseWarning));
            break;

        case Dp3000KeyboardState::InitialiseAgain:
        {
            const bool card_present = dp3000_card_inserted;

            if (dp3000_card_inserted && dp3000_card_write_protected) {
                dp3000_keyboard_state = Dp3000KeyboardState::Idle;
                dp3000_operator_queue(dp3000_localised(
                    "DIE SPEICHERKARTE IST\nSCHREIBGESCHUETZT!",
                    "MEMORY CARD IS\nWRITE-PROTECTED!"));
                break;
            }

            if (dp3000_card_inserted) {
                dp3000_records.clear();
                dp3000_deleted_records.clear();
                dp3000_memory_fault = false;
                dp3000_memory_full = false;
                dp3000_store_save();
                dp3000_recovery_save();
            }
            dp3000_settings = Dp3000Settings();
            dp3000_device_settings_save();
            dp3000_store_save();
            QString message = dp3000_text(Dp3000Text::Initialised)
                            + QLatin1Char('\n');
            message += card_present
                ? dp3000_text(Dp3000Text::DataDeleted)
                    + QLatin1Char('\n')
                : dp3000_text(Dp3000Text::NoCard) + QLatin1Char('\n');
            dp3000_operator_queue(message);
            dp3000_begin_settings_after_status();
            break;
        }

        case Dp3000KeyboardState::SettingsShowCurrent:
            dp3000_keyboard_state = Dp3000KeyboardState::SettingsOperatingBlock;
            dp3000_operator_queue(dp3000_settings_status(true)
                + dp3000_localised("BETRIEBS-PARAMETER\nAENDERN? ...JA/NEIN",
                                   "OPERATING PARAMETERS\nCHANGE? ...YES/NO"));
            dp3000_arm_input_timeout();
            break;

        case Dp3000KeyboardState::SettingsOperatingBlock:
            dp3000_setting_index = 0;
            dp3000_settings_parameter_prompt();
            break;

        case Dp3000KeyboardState::SettingsEvaluationBlock:
            dp3000_setting_index = static_cast<int>(Dp3000Setting::KeyCode);
            dp3000_settings_parameter_prompt();
            break;

        case Dp3000KeyboardState::SettingsParameter:
            dp3000_keyboard_state = Dp3000KeyboardState::SettingsEdit;
            dp3000_edit_subindex = 0;
            dp3000_setting_adjust(0);
            break;

        case Dp3000KeyboardState::SettingsEdit:
            dp3000_setting_adjust(+1);
            break;

        case Dp3000KeyboardState::DeleteAgain:
            /* The second confirmation is specifically the delete key. */
            dp3000_operator_queue(
                dp3000_text(Dp3000Text::PressDeleteAgain));
            break;

        case Dp3000KeyboardState::RecoverConfirm:
            dp3000_records = dp3000_deleted_records;
            dp3000_deleted_records.clear();
            dp3000_memory_full = dp3000_records_bytes(dp3000_records)
                               >= DP3000_STORE_MAX;
            dp3000_store_save();
            dp3000_recovery_save();
            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            dp3000_operator_queue(dp3000_text(Dp3000Text::DataRestored));
            break;

        case Dp3000KeyboardState::Idle:
        default:
            if (dp3000_keyboard_job) {
                /* Pause/resume is handled from press/release so a short click
                   cannot stop the mechanism; the manual requires holding the
                   key until the current line has completed. */
                return;
            } else if (!dp3000_card_inserted) {
                dp3000_operator_queue(dp3000_text(Dp3000Text::NoCard));
            } else if (!dp3000_has_stored_data()) {
                dp3000_operator_queue(dp3000_text(Dp3000Text::NoData));
            } else {
                dp3000_print_prompt(Dp3000KeyboardState::PrintMaximal,
                                    Dp3000Text::PrintMaximum);
            }
            break;
    }
}

static void
dp3000_keyboard_initialise()
{
    if (!dp3000_keyboard_begin())
        return;

    if (dp3000_is_print_prompt(dp3000_keyboard_state)) {
        dp3000_keyboard_state = Dp3000KeyboardState::Idle;
        dp3000_operator_queue(dp3000_text(Dp3000Text::Cancelled));
        return;
    }
    if (dp3000_card_inserted && dp3000_card_write_protected
        && (dp3000_keyboard_state == Dp3000KeyboardState::Idle)) {
        dp3000_operator_queue(dp3000_localised(
            "DIE SPEICHERKARTE IST\nSCHREIBGESCHUETZT!",
            "MEMORY CARD IS\nWRITE-PROTECTED!"));
        return;
    }
    if (dp3000_keyboard_state == Dp3000KeyboardState::SettingsEdit) {
        const Dp3000Setting setting =
            static_cast<Dp3000Setting>(dp3000_setting_index);
        const int parts = (setting == Dp3000Setting::DateTime) ? 5
                        : ((setting == Dp3000Setting::DataprintNumber)
                           || (setting == Dp3000Setting::CardNumber)) ? 4
                        : (setting == Dp3000Setting::KeyCode) ? 8 : 1;

        if (++dp3000_edit_subindex < parts)
            dp3000_setting_adjust(0);
        else
            dp3000_settings_next_parameter();
        return;
    }
    if (dp3000_keyboard_state == Dp3000KeyboardState::SettingsParameter) {
        dp3000_settings_next_parameter();
        return;
    }
    if ((dp3000_keyboard_state == Dp3000KeyboardState::SettingsShowCurrent)
        || (dp3000_keyboard_state == Dp3000KeyboardState::SettingsOperatingBlock)
        || (dp3000_keyboard_state == Dp3000KeyboardState::SettingsEvaluationBlock)) {
        dp3000_finish_settings();
        return;
    }
    if ((dp3000_keyboard_state == Dp3000KeyboardState::Idle)
        && dp3000_keyboard_job)
        return;

    dp3000_keyboard_state = Dp3000KeyboardState::InitialiseConfirm;
    dp3000_operator_queue(dp3000_text(Dp3000Text::InitialiseConfirm));
    dp3000_arm_input_timeout();
}

static void
dp3000_keyboard_no_delete()
{
    if (!dp3000_keyboard_begin())
        return;

    switch (dp3000_keyboard_state) {
        case Dp3000KeyboardState::PrintMaximal:
            dp3000_print_prompt(Dp3000KeyboardState::PrintMedium,
                                Dp3000Text::PrintMedium);
            break;

        case Dp3000KeyboardState::PrintMedium:
            dp3000_print_prompt(Dp3000KeyboardState::PrintShort,
                                Dp3000Text::PrintShort);
            break;

        case Dp3000KeyboardState::PrintShort:
            dp3000_print_prompt(Dp3000KeyboardState::PrintBag,
                                Dp3000Text::PrintBag);
            break;

        case Dp3000KeyboardState::PrintBag:
            dp3000_print_prompt(Dp3000KeyboardState::PrintCustom,
                                Dp3000Text::PrintCustom);
            break;

        case Dp3000KeyboardState::PrintCustom:
            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            break;

        case Dp3000KeyboardState::SettingsShowCurrent:
            dp3000_keyboard_state = Dp3000KeyboardState::SettingsOperatingBlock;
            dp3000_operator_queue(dp3000_localised(
                "BETRIEBS-PARAMETER\nAENDERN? ...JA/NEIN",
                "OPERATING PARAMETERS\nCHANGE? ...YES/NO"));
            dp3000_arm_input_timeout();
            break;

        case Dp3000KeyboardState::SettingsOperatingBlock:
            dp3000_keyboard_state = Dp3000KeyboardState::SettingsEvaluationBlock;
            dp3000_operator_queue(dp3000_localised(
                "AUSWERTUNGS-PARAMETER\nAENDERN? ...JA/NEIN",
                "EVALUATION PARAMETERS\nCHANGE? ...YES/NO"));
            dp3000_arm_input_timeout();
            break;

        case Dp3000KeyboardState::SettingsEvaluationBlock:
            dp3000_finish_settings();
            break;

        case Dp3000KeyboardState::SettingsParameter:
            dp3000_settings_next_parameter();
            break;

        case Dp3000KeyboardState::SettingsEdit:
            dp3000_setting_adjust(-1);
            break;

        case Dp3000KeyboardState::DeleteAgain:
            if (dp3000_card_write_protected) {
                dp3000_keyboard_state = Dp3000KeyboardState::Idle;
                dp3000_operator_queue(dp3000_localised(
                    "DIE SPEICHERKARTE IST\nSCHREIBGESCHUETZT!",
                    "MEMORY CARD IS\nWRITE-PROTECTED!"));
                break;
            }
            dp3000_deleted_records = dp3000_records;
            dp3000_records.clear();
            dp3000_memory_full = false;
            dp3000_store_save();
            dp3000_recovery_save();
            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            dp3000_operator_queue(dp3000_text(Dp3000Text::DataDeleted));
            break;

        case Dp3000KeyboardState::InitialiseConfirm:
        case Dp3000KeyboardState::InitialiseAgain:
            dp3000_begin_settings_after_status();
            break;

        case Dp3000KeyboardState::DeleteConfirm:
        case Dp3000KeyboardState::RecoverConfirm:
            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            dp3000_operator_queue(dp3000_text(Dp3000Text::Cancelled));
            break;

        case Dp3000KeyboardState::Idle:
        default:
            if (dp3000_keyboard_job) {
                return;
            } else if (!dp3000_card_inserted) {
                dp3000_operator_queue(dp3000_text(Dp3000Text::NoCard));
            } else if (!dp3000_has_stored_data()
                       && !dp3000_deleted_records.isEmpty()) {
                dp3000_keyboard_state = Dp3000KeyboardState::RecoverConfirm;
                dp3000_operator_queue(
                    dp3000_text(Dp3000Text::RestoreDeleted));
            } else if (!dp3000_has_stored_data()) {
                dp3000_operator_queue(dp3000_text(Dp3000Text::NoData));
            } else {
                dp3000_keyboard_state = Dp3000KeyboardState::DeleteConfirm;
                dp3000_operator_queue(
                    dp3000_text(Dp3000Text::DataStoredDelete));
            }
            break;
    }
}

static void
dp3000_keyboard_print_pressed()
{
    if ((dp3000_keyboard_state != Dp3000KeyboardState::Idle)
        || !dp3000_keyboard_job || dp3000_paper_restart_required
        || !dp3000_keyboard_ready())
        return;

    dp3000_print_press_handled = true;
    if (dp3000_keyboard_paused) {
        dp3000_keyboard_paused = false;
        dp3000_pause_requested = false;
    } else {
        dp3000_pause_requested = true;
    }
    cp80_render();
}

static void
dp3000_keyboard_print_released()
{
    /* Releasing before the mechanism reaches the line boundary is the short
       press which the manual says must not stop the total print. */
    if (!dp3000_keyboard_paused)
        dp3000_pause_requested = false;
    cp80_render();
}

static int
cp80_manual_feed_once()
{
    if (cp80_model == Cp80PrinterModel::Dataprint3000) {
        cp80_power = true;
        if (!cp80_can_print() || !dp3000_paper_loaded) {
            cp80_render();
            return 0;
        }

        cp80_printed += QLatin1Char('\n');
        cp80_batt_spend(QString(), false);
        cp80_line_batt.append(cp80_drive_level());
        if (dp3000_paper_lines > 0)
            dp3000_paper_lines--;
        if (dp3000_paper_lines == 0) {
            dp3000_paper_loaded = false;
            if (!cp80_queued.isEmpty()) {
                dp3000_paper_restart_required = true;
                dp3000_keyboard_paused = true;
            }
        }
        prn_dp3000_sound_feed(1000);
        cp80_begin_paper_motion();
        return DP3000_FEED_MS;
    }

    /* Holding FEED is a continuous paper operation, but only while OFFLINE.
       Each repeated advance still spends the motor's share of the pack and
       gets its own paper motion. */
    if (!cp80_can_print() || (prn_cp80_connected() != 0))
        return 0;

    cp80_printed += QLatin1Char('\n');
    const int speed = cp80_batt_spend(QString(), false);
    const int duration = qMax(CP80_PAPER_FRAME_MS * CP80_PAPER_FRAMES,
                              qRound(CP80_PAPER_MS * 1000.0
                                     / qMax(speed, 1)));

    prn_cp80_sound_feed(unsigned(speed));
    cp80_line_batt.append(cp80_drive_level());
    cp80_begin_paper_motion();
    return duration;
}

static void
cp80_feed_line()
{
    const bool dp3000 = (cp80_model == Cp80PrinterModel::Dataprint3000);
    const bool keyboard_path = dp3000 && dp3000_keyboard_job
                            && cp80_ac
                            && !dp3000_keyboard_paused;

    /* A flat pack prints nothing, and neither does a printer that is off or
       offline.  What has already arrived stays in the buffer -- that is the
       state the manual describes, with the ONLINE lamp blinking over it. */
    if (cp80_queued.isEmpty() || !cp80_power
        || ((prn_cp80_connected() == 0) && !keyboard_path))
        return;
    if (dp3000 && (!dp3000_paper_loaded
                   || dp3000_paper_restart_required))
        return;

    /* Put online on a pack that cannot drive the head: it goes straight back
       off, which is what the machine does and what the operator sees when they
       press ONLINE too early. */
    if (!cp80_can_print()) {
        prn_cp80_set_connected(0);
        pclog("CP80: pack at %.2f%% is below the %.0f%% needed to print; "
              "offline again with %d characters buffered\n",
              cp80_batt, CP80_BATT_RESUME, int(cp80_queued.size()));
        cp80_render();
        return;
    }

    const int at = cp80_queued.indexOf(QLatin1Char('\n'));

    /* Nothing but a partial line yet -- wait for the rest rather than tear one
       in half.  The report ends every record with LF. */
    if (at < 0)
        return;

    const QString line = cp80_queued.left(at + 1);
    const int     columns = cp80_print_columns(line);
    const int     ink     = cp80_print_ink(line);

    cp80_printed += line;
    cp80_queued.remove(0, at + 1);
    const int speed = cp80_batt_spend(line);

    if (dp3000 && (dp3000_paper_lines > 0)) {
        dp3000_paper_lines--;
        if (dp3000_paper_lines == 0) {
            dp3000_paper_loaded = false;
            if (!cp80_queued.isEmpty()) {
                dp3000_paper_restart_required = true;
                dp3000_keyboard_paused = true;
            }
        }
    }

    if (dp3000 && dp3000_pause_requested && keyboard_path) {
        /* A held DRUCKEN key takes effect only after the line that was already
           in progress has completed, exactly as the operator guide specifies. */
        dp3000_keyboard_paused = true;
        dp3000_pause_requested = false;
    }

    if (cp80_model == Cp80PrinterModel::Dpu414)
        prn_cp80_sound_line(unsigned(columns), unsigned(ink), unsigned(speed));
    else
        prn_dp3000_sound_line(unsigned(columns), unsigned(ink), unsigned(speed));
    cp80_line_batt.append(cp80_drive_level());
    if ((cp80_model == Cp80PrinterModel::Dpu414) && (columns > 0))
        cp80_head_away = !cp80_head_away;
    if (cp80_model == Cp80PrinterModel::Dpu414)
        cp80_schedule_home(unsigned(columns), unsigned(speed),
            ((cp80_feed != nullptr) ? cp80_feed->interval() : CP80_FIRST_LINE_MS)
            + CP80_HOME_DELAY_MS);
    if (dp3000 && keyboard_path && cp80_queued.isEmpty()) {
        if ((dp3000_print_job_record >= 0)
            && (++dp3000_print_job_record < dp3000_print_job_records.size())) {
            cp80_queued = dp3000_print_job_records.at(dp3000_print_job_record);
        } else {
            dp3000_print_job_records.clear();
            dp3000_print_job_record = -1;
            dp3000_keyboard_job = false;
            dp3000_keyboard_paused = false;
            if (dp3000_input_timeout_requested
                && (dp3000_keyboard_state != Dp3000KeyboardState::Idle)
                && (dp3000_input_timeout != nullptr))
                dp3000_input_timeout->start(60000);
            else if (dp3000_keyboard_state == Dp3000KeyboardState::Idle)
                dp3000_cancel_input_timeout();
        }
        if (!dp3000_paper_loaded && !cp80_queued.isEmpty()) {
            dp3000_paper_restart_required = true;
            dp3000_keyboard_paused = true;
        }
    } else if (dp3000 && !keyboard_path && cp80_queued.isEmpty()
               && (prn_cp80_connected() != 0)) {
        /* The queue is empty when the final mechanical event starts, not when
           it ends.  Delay the once-per-second "unplug now" signal until the
           last shuttle/feed cycle has actually stopped.  A generation token
           prevents a newer transfer from inheriting an older completion. */
        const unsigned generation = dp3000_job_generation;
        const int delay = columns ? DP3000_LINE_MS : DP3000_FEED_MS;

        QTimer::singleShot(delay, cp80_win, [generation]() {
            if ((generation == dp3000_job_generation)
                && (cp80_model == Cp80PrinterModel::Dataprint3000)
                && (prn_cp80_connected() != 0) && cp80_queued.isEmpty()
                && !dp3000_keyboard_job && !dp3000_memory_fault
                && !dp3000_transfer_error && dp3000_paper_loaded) {
                dp3000_complete_signal = true;
                cp80_render();
            }
        });
    }
    cp80_begin_paper_motion();
}

static void
dp3000_trim_wire_padding(QString *record)
{
    /* The command frame ends in two LFs which the generic paper parser quite
       correctly renders.  They are transport padding, not part of the stored
       device report. */
    while (record->startsWith(QLatin1Char('\n'))
           || record->startsWith(QLatin1Char('\r')))
        record->remove(0, 1);
    if (!record->isEmpty() && !record->endsWith(QLatin1Char('\n')))
        record->append(QLatin1Char('\n'));
}

static QString
dp3000_record_envelope(const QString &payload)
{
    QString record;
    const QDateTime stamp = dp3000_clock_now();

    dp3000_dataset_number++;
    if (dp3000_dataset_number == 0)
        dp3000_dataset_number = 1;
    record += stamp.toString(QStringLiteral("dd.MM.yy/hh:mm"))
           + QStringLiteral("     V4.00\n");
    record += dp3000_localised("DATENSATZ: %1\n", "DATA SET:  %1\n")
                  .arg(dp3000_dataset_number, 13, 10, QLatin1Char(' '));
    if (dp3000_settings.print_numbers) {
        record += dp3000_localised("DATAPRINT:          %1\n",
                                   "DATAPRINT:          %1\n")
                      .arg(dp3000_settings.dataprint_number, 4, 10,
                           QLatin1Char('0'));
        record += dp3000_localised("SPEICHERKARTE:      %1\n",
                                   "MEMORY CARD:        %1\n")
                      .arg(dp3000_settings.card_number, 4, 10,
                           QLatin1Char('0'));
    }
    record += payload;
    return record;
}

static QString
dp3000_photo_play_print(const QString &record, int setting)
{
    /* NONE and MAXIMUM are fully defined for the recovered Photo Play stream.
       Medium/short/cash-bag/custom need the still-unknown ESC K tag meanings;
       printing the full authentic report is preferable to silently inventing
       sections while retaining those choices for a future measured mapping. */
    if (setting == 5)
        return QString();
    return record;
}

static void
dp3000_finish_transfer(uint64_t serial, int result, uint16_t expected,
                       uint16_t calculated)
{
    if (serial != 0)
        dp3000_transfer_completed_seen = serial;
    if ((serial == 0) || (serial != dp3000_pending_serial)
        || (serial == dp3000_discard_transfer_serial)) {
        dp3000_pending_record.clear();
        dp3000_pending_serial = 0;
        return;
    }

    QString payload = dp3000_pending_record;
    dp3000_pending_record.clear();
    dp3000_pending_serial = 0;
    if (result <= 0) {
        dp3000_transfer_error = true;
        dp3000_complete_signal = false;
        pclog("DP3000: rejecting record %llu: checksum %04X, calculated %04X\n",
              (unsigned long long) serial, expected, calculated);
        cp80_render();
        return;
    }

    dp3000_trim_wire_padding(&payload);
    const bool card_writable = dp3000_card_inserted
                            && !dp3000_card_write_protected
                            && !dp3000_memory_fault;
    const bool cardless_ok = !dp3000_card_inserted
                          && dp3000_settings.without_card;
    const bool storage_fault_fallback = dp3000_card_inserted
                                     && dp3000_memory_fault;

    if (!card_writable && !cardless_ok && !storage_fault_fallback) {
        if (dp3000_card_write_protected
            && (serial != dp3000_wp_notified_serial)) {
            cp80_queued += dp3000_localised(
                "DIE SPEICHERKARTE IST\nSCHREIBGESCHUETZT!\n",
                "MEMORY CARD IS\nWRITE-PROTECTED!\n");
            dp3000_wp_notified_serial = serial;
        } else if (!dp3000_card_inserted
                   && (serial != dp3000_no_card_notified_serial)) {
            cp80_queued += dp3000_text(Dp3000Text::NoCard)
                          + QLatin1Char('\n');
            dp3000_no_card_notified_serial = serial;
        }
        dp3000_transfer_error = true;
        cp80_render();
        return;
    }

    const QString record = dp3000_record_envelope(payload);
    bool stored = false;
    bool force_full_print = cardless_ok || storage_fault_fallback
                         || dp3000_memory_full;

    if (card_writable && dp3000_settings.storage_mode
        && !dp3000_memory_full) {
        QStringList prospective = dp3000_records;
        prospective.append(record);
        if (dp3000_records_encode(prospective, true).size()
            <= DP3000_STORE_MAX) {
            if (!dp3000_deleted_records.isEmpty()) {
                /* Recovery is possible until the first complete new record is
                   committed, not merely until its first UART byte arrives. */
                dp3000_deleted_records.clear();
                dp3000_recovery_save();
            }
            dp3000_records = prospective;
            dp3000_store_save();
            stored = true;
        } else {
            dp3000_memory_full = true;
            force_full_print = true;
        }
    }

    /* The running data-set identity belongs to the inserted card even when
       this particular data set was configured for paper-only output. */
    if (dp3000_card_inserted && !stored && !dp3000_memory_fault
        && !dp3000_memory_full)
        dp3000_store_save();

    QString paper;
    if (force_full_print) {
        paper = record;
    } else {
        paper += dp3000_photo_play_print(
            record, dp3000_settings.first_print_length);
        paper += dp3000_photo_play_print(
            record, dp3000_settings.second_print_length);
    }
    if (!paper.isEmpty() && dp3000_card_inserted) {
        const int available = qMax(0, DP3000_STORE_MAX
            - dp3000_records_bytes(dp3000_records));
        paper += dp3000_localised("ABLAGESPEICHER FREI: ",
                                  "MEMORY AVAILABLE: ")
              + QString::number(available / 1024) + QStringLiteral(" KB\n");
    }
    cp80_queued += paper;
    dp3000_transfer_error = false;
    dp3000_complete_signal = paper.isEmpty();
    dp3000_job_generation++;
    if (!dp3000_paper_loaded && !paper.isEmpty())
        dp3000_paper_restart_required = true;
    pclog("DP3000: accepted record %llu as data set %u (%s, %s)\n",
          (unsigned long long) serial, dp3000_dataset_number,
          stored ? "stored" : "not stored",
          paper.isEmpty() ? "not printed" : "queued for print");
    cp80_render();
}

static void
cp80_pump()
{
    char buf[4097];

    if (cp80_win == nullptr)
        return;

    /* Only the paper.  The control-code trace is still kept by the device and
       still goes to the log and cp80-raw.bin; it was a second text box here
       while the protocol was being worked out, and now that it is understood it
       was two thirds of the window telling you nothing. */
    for (;;) {
        int          reset = 0;
        const size_t n     = prn_cp80_take(PRN_CP80_PAPER, &cp80_paper_at,
                                           &reset, buf, sizeof(buf) - 1);

        if (reset) {
            if (cp80_paper_motion != nullptr)
                cp80_paper_motion->stop();
            cp80_paper_frame = CP80_PAPER_FRAMES;
            cp80_queued.clear();
            cp80_printed.clear();
            cp80_line_batt.clear();
            dp3000_pending_record.clear();
            dp3000_pending_serial = 0;
            cp80_render();
        }
        if (n == 0)
            break;
        buf[n] = 0;
        const QString received = QString::fromUtf8(buf);

        if (cp80_model == Cp80PrinterModel::Dataprint3000) {
            const uint64_t transfer_serial = prn_cp80_transfer_serial();

            /* RESET electrically aborts the current evaluation.  The guest
               may already have placed more bytes in the UART, so discard the
               rest of that numbered record until a new XON starts a new one. */
            if ((dp3000_discard_transfer_serial != 0)
                && (transfer_serial == dp3000_discard_transfer_serial))
                continue;
            if ((dp3000_discard_transfer_serial != 0)
                && (transfer_serial != dp3000_discard_transfer_serial))
                dp3000_discard_transfer_serial = 0;

            if ((transfer_serial != 0)
                && (transfer_serial != dp3000_transfer_serial_seen)) {
                dp3000_transfer_serial_seen = transfer_serial;
                dp3000_pending_serial = transfer_serial;
                dp3000_pending_record.clear();
                if (dp3000_plug_error_pending) {
                    cp80_queued += dp3000_localised(
                        "STECKER-FEHLER !\n", "PLUG ERROR !\n");
                    dp3000_plug_error_pending = false;
                } else if (dp3000_abort_pending) {
                    cp80_queued += dp3000_localised(
                        "!!WAR ABBRUCH / RESET!!\n",
                        "!!WAS ABORTED / RESET!!\n");
                    dp3000_abort_pending = false;
                }
            }
            if (transfer_serial == dp3000_pending_serial)
                dp3000_pending_record += received;
            cp80_power = true;
            dp3000_complete_signal = false;
            prn_dp3000_sound_transfer();
        } else {
            cp80_queued += received;
        }

        /* 28 KB of it, per the manual.  Past that a real printer would be
           holding the host off with its flow control; this at least refuses to
           grow without limit and says so. */
        if (cp80_queued.size() > CP80_BUFFER_MAX) {
            static bool said = false;

            if (!said) {
                said = true;
                pclog("CP80: buffer full at %d characters; dropping the rest\n",
                      CP80_BUFFER_MAX);
            }
            cp80_queued.truncate(CP80_BUFFER_MAX);
        }
    }

    if (cp80_model == Cp80PrinterModel::Dataprint3000) {
        uint64_t completed = 0;
        uint16_t expected = 0;
        uint16_t calculated = 0;
        const int result = prn_cp80_transfer_result(
            &completed, &expected, &calculated);

        if ((completed != 0) && (completed != dp3000_transfer_completed_seen))
            dp3000_finish_transfer(completed, result, expected, calculated);

        if (cp80_queued.size() > CP80_BUFFER_MAX)
            cp80_queued.truncate(CP80_BUFFER_MAX);
    }
}

static void
dp3000_update_tooltips()
{
    if (dp3000_case_feed != nullptr) {
        dp3000_case_feed->setToolTip(dp3000_english
            ? QObject::tr("Paper feed — press or hold the green side switch")
            : QObject::tr("Papiervorschub — press or hold the green side switch"));
    }
    if (dp3000_case_reset != nullptr) {
        dp3000_case_reset->setToolTip(QObject::tr(
            "RESET — clear the current operation and switch off when idle"));
    }
    if (dp3000_key_yes != nullptr) {
        dp3000_key_yes->setToolTip(dp3000_english
            ? QObject::tr("+ / yes / print — requires Keyboard and Adapter")
            : QObject::tr("+ / ja / drucken — requires Keyboard and Adapter"));
    }
    if (dp3000_key_init != nullptr) {
        dp3000_key_init->setToolTip(QObject::tr(
            "init. — enter settings / confirm next field; requires Keyboard and Adapter"));
    }
    if (dp3000_key_no != nullptr) {
        dp3000_key_no->setToolTip(dp3000_english
            ? QObject::tr("− / no / delete — requires Keyboard and Adapter")
            : QObject::tr("− / nein / löschen — requires Keyboard and Adapter"));
    }
    if (dp3000_card != nullptr) {
        dp3000_card->setToolTip(QObject::tr(
            "256 kB SRAM card — switch the DATAprint off before removing it"));
    }
}

static void
dp3000_select_english(bool enabled, bool persist)
{
    if (persist) {
        photoplay_set_dataprint_english(enabled ? 1 : 0);
        config_changed = 2;
        config_save();
    }

    const bool changed = dp3000_english != enabled;
    dp3000_english = enabled;
    dp3000_update_tooltips();
    if (changed && (cp80_model == Cp80PrinterModel::Dataprint3000)
        && (cp80_win != nullptr))
        cp80_render();
}

static void
cp80_select_model(int index, bool persist)
{
    const Cp80PrinterModel next = (index == 1)
        ? Cp80PrinterModel::Dataprint3000 : Cp80PrinterModel::Dpu414;

    if (persist) {
        photoplay_set_printer(
            (next == Cp80PrinterModel::Dataprint3000)
                ? PHOTOPLAY_PRINTER_DP3000 : PHOTOPLAY_PRINTER_DPU414);
        config_changed = 2;
        config_save();
    }

    cp80_model_initialised = true;
    if (next == cp80_model)
        return;

    prn_dp3000_sound_signal(PRN_DP3000_SIGNAL_NONE);
    dp3000_complete_signal = false;
    dp3000_job_generation++;

    /* The Tools menu exists before the optional printer window.  In that case
       only choose the model; its battery/card state is loaded when it opens.
       This avoids overwriting an unopened printer's saved battery level. */
    if (cp80_win == nullptr) {
        cp80_model = next;
        if (!cp80_charge_overridden)
            cp80_charge = (cp80_model == Cp80PrinterModel::Dataprint3000)
                        ? DP3000_CHARGE_MINS : CP80_CHARGE_MINS;
        return;
    }

    cp80_batt_store();
    if (cp80_model == Cp80PrinterModel::Dataprint3000)
        dp3000_store_save();

    /* These are two different physical printers.  Never reinterpret paper or
       a live serial/mechanical job through the newly selected mechanism. */
    prn_cp80_set_connected(0);
    prn_cp80_clear();
    cp80_paper_at = 0;
    cp80_queued.clear();
    cp80_printed.clear();
    cp80_line_batt.clear();
    dp3000_keyboard_state = Dp3000KeyboardState::Idle;
    dp3000_cancel_input_timeout();
    dp3000_keyboard_job = false;
    dp3000_keyboard_paused = false;
    dp3000_pause_requested = false;
    dp3000_print_job_records.clear();
    dp3000_print_job_record = -1;
    dp3000_paper_restart_required = false;
    dp3000_transfer_error = false;
    dp3000_abort_pending = false;
    dp3000_plug_error_pending = false;
    dp3000_discard_transfer_serial = 0;
    dp3000_pending_serial = 0;
    dp3000_pending_record.clear();
    dp3000_transfer_serial_seen = prn_cp80_transfer_serial();
    dp3000_transfer_completed_seen = prn_cp80_transfer_completed();
    cp80_model = next;
    cp80_power = (cp80_model == Cp80PrinterModel::Dataprint3000)
               ? cp80_ac : true;
    cp80_batt = CP80_BATT_FULL;
    cp80_batt_load();
    if (cp80_model == Cp80PrinterModel::Dataprint3000) {
        dp3000_device_settings_load();
        if (dp3000_card_inserted)
            dp3000_store_load();
    }
    cp80_charging = false;
    cp80_head_away = false;
    if (cp80_home_timer != nullptr)
        cp80_home_timer->stop();
    if (!cp80_charge_overridden)
        cp80_charge = (cp80_model == Cp80PrinterModel::Dataprint3000)
                    ? DP3000_CHARGE_MINS : CP80_CHARGE_MINS;
    if (cp80_feed != nullptr)
        cp80_feed->setInterval(
            (cp80_model == Cp80PrinterModel::Dataprint3000)
                ? DP3000_LINE_MS : CP80_FIRST_LINE_MS);
    cp80_render();
    if (cp80_win->isVisible()) {
        /* A model change also changes the paper direction.  Move to the new
           stationary screen edge, then render again with the room available
           on the correct side of the printer. */
        cp80_anchor_vertical_edge();
        cp80_render();
    }
}

static void
cp80_show(QWidget *parent)
{
    bool created = false;

    if (!cp80_model_initialised) {
        const char *chosen = photoplay_printer();
        const char *override_model = getenv("PEEPEEBOX_PRN_MODEL");

        /* Retain the one-run developer override used by visual regression,
           while the user-facing and persistent source is the INI setting. */
        if ((override_model != nullptr)
            && ((strcmp(override_model, "3000") == 0)
                || (strcmp(override_model, "dataprint3000") == 0)
                || (strcmp(override_model, "DATAPRINT3000") == 0)))
            chosen = PHOTOPLAY_PRINTER_DP3000;

        if (!strcmp(chosen, PHOTOPLAY_PRINTER_DP3000))
            cp80_model = Cp80PrinterModel::Dataprint3000;
        if (cp80_model == Cp80PrinterModel::Dataprint3000)
            cp80_power = false;
        cp80_charge = (cp80_model == Cp80PrinterModel::Dataprint3000)
                    ? DP3000_CHARGE_MINS : CP80_CHARGE_MINS;
        cp80_model_initialised = true;
    }

    if (cp80_win == nullptr) {
        created = true;
        if (parent != nullptr) {
            const QRect parent_frame = parent->window()->frameGeometry();
            QScreen *screen = QGuiApplication::screenAt(parent_frame.center());

            if (screen != nullptr)
                cp80_available_hint = screen->availableGeometry();
        }

        cp80_win = new QDialog(parent);
        cp80_win->setWindowTitle(QObject::tr("Seiko DPU-414"));

        for (QScreen *screen : QGuiApplication::screens()) {
            QObject::connect(screen, &QScreen::availableGeometryChanged,
                             cp80_win, [](const QRect &) {
                cp80_render();
            });
        }

        QPixmap dpu(QStringLiteral(":/menuicons/qt/icons/dpu414.png"));

        cp80_body = new QPixmap(dpu.scaled(CP80_HEAD_W, CP80_HEAD_H,
                                           Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation));

        cp80_view = new QLabel(cp80_win);
        cp80_view->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        cp80_view->setMouseTracking(true);
        cp80_view->installEventFilter(new Cp80PaperEventFilter(cp80_view));

        /* The panel's own controls, over the buttons in the photograph.  They
           are children of the picture rather than items in a layout, so they
           travel with it and land on the buttons they are drawn on; cp80_render
           puts them where the machine currently is.  Invisible, because the
           button they operate is already in the picture. */
        const QString bare = QStringLiteral(
            "QPushButton { background: transparent; border: none; }");

        cp80_btn_on = new QPushButton(cp80_view);
        cp80_btn_on->setStyleSheet(bare);
        cp80_btn_on->setCursor(Qt::PointingHandCursor);
        cp80_btn_on->setToolTip(QObject::tr("ON LINE — connect or disconnect the "
                                            "Dataprint"));

        /* The roll's own scrollbar, a child of the picture so it can sit inside
           the paper.  A real one rather than a painted indicator, because the
           lines that have ridden off the top are worth being able to get back,
           and a widget brings the dragging with it. */
        cp80_scroll = new QScrollBar(Qt::Vertical, cp80_view);
        cp80_scroll->setStyleSheet(QStringLiteral(
            "QScrollBar:vertical { background: transparent; width: %1px; margin: 0; }"
            "QScrollBar::handle:vertical { background: rgba(122,118,104,150);"
            " border-radius: %2px; min-height: %3px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical"
            " { background: transparent; }")
            .arg(9)
            .arg(4)
            .arg(20));
        cp80_scroll->hide();

        QObject::connect(cp80_scroll, &QScrollBar::valueChanged, cp80_win, []() {
            cp80_render();
        });
        QObject::connect(cp80_scroll, &QScrollBar::sliderReleased,
                         cp80_win, []() {
            cp80_update_paper_hover();
        });

        cp80_btn_fd = new QPushButton(cp80_view);
        cp80_btn_fd->setStyleSheet(bare);
        cp80_btn_fd->setCursor(Qt::PointingHandCursor);
        cp80_btn_fd->setToolTip(QObject::tr(
            "FEED — press or hold to advance paper while OFFLINE"));

        dp3000_case_feed = new QPushButton(cp80_view);
        dp3000_case_feed->setStyleSheet(bare);
        dp3000_case_feed->setCursor(Qt::PointingHandCursor);
        dp3000_case_reset = new QPushButton(cp80_view);
        dp3000_case_reset->setStyleSheet(bare);
        dp3000_case_reset->setCursor(Qt::PointingHandCursor);
        dp3000_key_yes = new QPushButton(cp80_view);
        dp3000_key_yes->setStyleSheet(bare);
        dp3000_key_yes->setCursor(Qt::PointingHandCursor);
        dp3000_key_init = new QPushButton(cp80_view);
        dp3000_key_init->setStyleSheet(bare);
        dp3000_key_init->setCursor(Qt::PointingHandCursor);
        dp3000_key_no = new QPushButton(cp80_view);
        dp3000_key_no->setStyleSheet(bare);
        dp3000_key_no->setCursor(Qt::PointingHandCursor);
        dp3000_card = new QPushButton(cp80_view);
        dp3000_card->setStyleSheet(bare);
        dp3000_card->setCursor(Qt::PointingHandCursor);
        dp3000_update_tooltips();

        /* The physical momentary switches still snap when their electrical
           action is unavailable (power off or an exhausted pack), so trigger
           the synthesized click on depression, before the clicked handlers
           below decide whether the printer can do anything.  Their photographed
           caps are redrawn two pixels lower until release as well. */
        QObject::connect(cp80_btn_on, &QPushButton::pressed, cp80_win, []() {
            cp80_btn_on_down = true;
            prn_cp80_sound_button();
            cp80_render();
        });
        QObject::connect(cp80_btn_fd, &QPushButton::pressed, cp80_win, []() {
            cp80_btn_fd_down = true;
            prn_cp80_sound_button();
            const int repeat_ms = cp80_manual_feed_once();

            if ((repeat_ms > 0) && (cp80_manual_feed != nullptr))
                cp80_manual_feed->start(350);
            else
                cp80_render();
        });
        QObject::connect(cp80_btn_on, &QPushButton::released, cp80_win, []() {
            cp80_btn_on_down = false;
            cp80_render();
        });
        QObject::connect(cp80_btn_fd, &QPushButton::released, cp80_win, []() {
            cp80_btn_fd_down = false;
            if (cp80_manual_feed != nullptr)
                cp80_manual_feed->stop();
            cp80_render();
        });

        QObject::connect(dp3000_case_feed, &QPushButton::pressed,
                         cp80_win, []() {
            cp80_btn_fd_down = true;
            prn_dp3000_sound_button();
            const int repeat_ms = cp80_manual_feed_once();
            if ((repeat_ms > 0) && (cp80_manual_feed != nullptr))
                cp80_manual_feed->start(350);
        });
        QObject::connect(dp3000_case_feed, &QPushButton::released,
                         cp80_win, []() {
            cp80_btn_fd_down = false;
            if (cp80_manual_feed != nullptr)
                cp80_manual_feed->stop();
            cp80_render();
        });
        QObject::connect(dp3000_case_reset, &QPushButton::pressed,
                         cp80_win, []() {
            prn_dp3000_sound_button();
            cp80_render();
        });
        QObject::connect(dp3000_case_reset, &QPushButton::clicked,
                         cp80_win, []() {
            const bool transfer_interrupted = prn_cp80_transfer_active() != 0;
            const bool interrupted = transfer_interrupted
                                  || dp3000_keyboard_job;

            if (transfer_interrupted) {
                dp3000_discard_transfer_serial = prn_cp80_transfer_serial();
                dp3000_pending_record.clear();
                dp3000_pending_serial = 0;
                prn_cp80_abort_transfer();
            }

            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            dp3000_cancel_input_timeout();
            dp3000_keyboard_job = false;
            dp3000_keyboard_paused = false;
            dp3000_pause_requested = false;
            dp3000_print_job_records.clear();
            dp3000_print_job_record = -1;
            dp3000_paper_restart_required = false;
            dp3000_transfer_error = interrupted;
            dp3000_abort_pending = transfer_interrupted;
            if (transfer_interrupted)
                dp3000_plug_error_pending = false;
            dp3000_complete_signal = false;
            dp3000_job_generation++;
            cp80_queued.clear();
            if ((prn_cp80_connected() == 0) && !cp80_charging)
                cp80_power = false;
            cp80_render();
        });

        const auto press_key = []() {
            prn_dp3000_sound_button();
            cp80_render();
        };
        const auto repaint_key = []() { cp80_render(); };
        QObject::connect(dp3000_key_yes, &QPushButton::pressed,
                         cp80_win, [press_key]() {
            press_key();
            dp3000_keyboard_print_pressed();
        });
        QObject::connect(dp3000_key_yes, &QPushButton::released,
                         cp80_win, []() { dp3000_keyboard_print_released(); });
        QObject::connect(dp3000_key_init, &QPushButton::pressed,
                         cp80_win, press_key);
        QObject::connect(dp3000_key_init, &QPushButton::released,
                         cp80_win, repaint_key);
        QObject::connect(dp3000_key_no, &QPushButton::pressed,
                         cp80_win, press_key);
        QObject::connect(dp3000_key_no, &QPushButton::released,
                         cp80_win, repaint_key);
        QObject::connect(dp3000_key_yes, &QPushButton::clicked,
                         cp80_win, []() {
            if (dp3000_print_press_handled) {
                dp3000_print_press_handled = false;
                return;
            }
            dp3000_keyboard_yes();
        });
        QObject::connect(dp3000_key_init, &QPushButton::clicked,
                         cp80_win, []() { dp3000_keyboard_initialise(); });
        QObject::connect(dp3000_key_no, &QPushButton::clicked,
                         cp80_win, []() { dp3000_keyboard_no_delete(); });
        QObject::connect(dp3000_card, &QPushButton::clicked,
                         cp80_win, []() {
            prn_dp3000_sound_button();
            if (dp3000_card_inserted) {
                if (cp80_power) {
                    /* The manual forbids removing SRAM while GERÄT EIN is lit.
                       Refuse the destructive hot removal instead of silently
                       pretending that it succeeded.  This is not a serial
                       transfer fault, so it must not light FEHLER PC/AUTOMAT
                       or start the external-error alarm. */
                    cp80_render();
                    return;
                }
                dp3000_store_save();
                dp3000_recovery_save();
                dp3000_card_inserted = false;
            } else {
                dp3000_card_inserted = true;
                dp3000_memory_fault = false;
                dp3000_store_load();
            }
            cp80_render();
        });

        QObject::connect(cp80_btn_on, &QPushButton::clicked, cp80_win, []() {
            if (!prn_cp80_present() || !cp80_power)
                return;

            const bool on = (prn_cp80_connected() == 0);

            prn_cp80_set_connected(on ? 1 : 0);
            if (on && (cp80_home_timer != nullptr))
                cp80_home_timer->stop();
            else if (!on)
                cp80_schedule_home(cp80_last_head_columns,
                                   cp80_last_head_speed,
                                   CP80_HOME_DELAY_MS);
            cp80_render();                   /* the lamp follows */
        });

        cp80_manual_feed = new QTimer(cp80_win);
        cp80_manual_feed->setSingleShot(true);
        QObject::connect(cp80_manual_feed, &QTimer::timeout, cp80_win, []() {
            if (!cp80_btn_fd_down)
                return;
            const int repeat_ms = cp80_manual_feed_once();

            if ((repeat_ms > 0) && cp80_btn_fd_down)
                cp80_manual_feed->start(repeat_ms);
        });

        if (cp80_feed == nullptr) {
            cp80_feed = new QTimer(cp80_win);
            QObject::connect(cp80_feed, &QTimer::timeout, cp80_win, []() {
                cp80_feed_line();
            });
            cp80_feed->start(
                (cp80_model == Cp80PrinterModel::Dataprint3000)
                    ? DP3000_LINE_MS : CP80_FIRST_LINE_MS);
        }

        dp3000_input_timeout = new QTimer(cp80_win);
        dp3000_input_timeout->setSingleShot(true);
        QObject::connect(dp3000_input_timeout, &QTimer::timeout,
                         cp80_win, []() {
            dp3000_input_timeout_requested = false;
            if ((cp80_model != Cp80PrinterModel::Dataprint3000)
                || (dp3000_keyboard_state == Dp3000KeyboardState::Idle))
                return;
            dp3000_keyboard_state = Dp3000KeyboardState::Idle;
            dp3000_print_job_records.clear();
            dp3000_print_job_record = -1;
            dp3000_operator_queue(dp3000_localised(
                "KEINE TASTE: ABGESCHALTET",
                "NO KEY: SWITCHED OFF"));
        });

        /* There was a "Dataprint connected" checkbox and a line saying which
           port it listened on.  Both are gone: the ON LINE button on the
           machine is that switch, and the port is not a choice any more, so the
           line only ever said the same thing. */
        char where[96] = "";

        prn_cp80_where(where, sizeof(where));

        cp80_status = new QLabel(cp80_win);

        /* Only when there is no printer, and then only to say why -- a machine
           that cannot be switched on needs to explain itself.  With one present
           the picture says everything the sentence did. */
        cp80_status->setText(QString::fromUtf8(where));
        cp80_status->setEnabled(false);
        cp80_status->setWordWrap(true);
        cp80_status->setVisible(prn_cp80_present() == 0);

        cp80_tear_btn = new QPushButton(QObject::tr("Tear off"), cp80_win);
        auto *save = new QPushButton(QObject::tr("Save paper…"), cp80_win);
        cp80_link_btn = new QPushButton(QObject::tr("VDAI cable"), cp80_win);
        cp80_link_btn->setCheckable(true);
        cp80_link_btn->setToolTip(QObject::tr(
            "Plug or unplug the DATAprint's 9-pin VDAI cable at the Photo Play"));
        cp80_feed_btn = new QPushButton(QObject::tr("Keyboard"), cp80_win);
        cp80_feed_btn->setCheckable(true);
        cp80_feed_btn->setToolTip(QObject::tr(
            "Plug or unplug the three-button keyboard from the PC connector"));
        cp80_paper_btn = new QPushButton(QObject::tr("Paper roll"), cp80_win);
        cp80_paper_btn->setCheckable(true);
        cp80_paper_btn->setToolTip(QObject::tr(
            "Remove or fit a 57 mm, 40 m plain-paper roll"));
        dp3000_wp_btn = new QPushButton(QObject::tr("Card WP"), cp80_win);
        dp3000_wp_btn->setCheckable(true);
        dp3000_wp_btn->setToolTip(QObject::tr(
            "Memory-card write-protect switch; protected cards can be read but "
            "not evaluated, deleted, or initialized"));

        /* The DPU has a power switch.  DATAprint power is event-driven by its
           cable, keyboard, adapter, paper-feed and RESET controls, so this
           generic switch is hidden when that model is selected. */
        cp80_pwr_btn = new QPushButton(QObject::tr("Power: on"), cp80_win);

        QObject::connect(cp80_pwr_btn, &QPushButton::clicked, cp80_win, []() {
            cp80_power = !cp80_power;

            /* A printer that is switched off is not a printer that is quiet:
               the guest should see no cable at all.  And the manual is explicit
               that it does not charge with the power off. */
            if (!cp80_power) {
                prn_cp80_set_connected(0);
                cp80_charging = false;   /* the adapter stays plugged in */
                if (cp80_home_timer != nullptr)
                    cp80_home_timer->stop();
            }
            cp80_render();
        });

        QObject::connect(cp80_link_btn, &QPushButton::clicked, cp80_win, []() {
            if (!prn_cp80_present())
                return;
            prn_dp3000_sound_button();
            const bool connecting = (prn_cp80_connected() == 0);
            if (!connecting && (prn_cp80_transfer_active() != 0)) {
                dp3000_transfer_error = true;
                dp3000_plug_error_pending = true;
                dp3000_discard_transfer_serial = prn_cp80_transfer_serial();
                dp3000_pending_record.clear();
                dp3000_pending_serial = 0;
            }
            if (connecting) {
                cp80_power = true;
                dp3000_transfer_error = false;
            }
            dp3000_complete_signal = false;
            dp3000_job_generation++;
            prn_cp80_set_connected(connecting ? 1 : 0);
            cp80_render();
        });

        QObject::connect(cp80_feed_btn, &QPushButton::clicked, cp80_win, []() {
            if (cp80_model != Cp80PrinterModel::Dataprint3000)
                return;
            prn_dp3000_sound_button();
            dp3000_keyboard_connected = !dp3000_keyboard_connected;
            if (!dp3000_keyboard_connected) {
                const bool was_active = dp3000_keyboard_job
                                     || (dp3000_keyboard_state
                                         != Dp3000KeyboardState::Idle);
                cp80_queued.clear();
                dp3000_cancel_input_timeout();
                dp3000_keyboard_paused = false;
                dp3000_keyboard_state = Dp3000KeyboardState::Idle;
                dp3000_print_job_records.clear();
                dp3000_print_job_record = -1;
                if (was_active) {
                    dp3000_transfer_error = true;
                    dp3000_operator_queue(dp3000_localised(
                        "VERBINDUNGSFEHLER\nPC / TASTATUR !",
                        "CONNECTION ERROR\nPC / KEYBOARD !"));
                    dp3000_transfer_error = true;
                } else {
                    dp3000_keyboard_job = false;
                }
            } else if (cp80_ac) {
                cp80_power = true;
                dp3000_transfer_error = false;
            }
            cp80_render();
        });

        QObject::connect(cp80_paper_btn, &QPushButton::clicked, cp80_win, []() {
            if (cp80_model != Cp80PrinterModel::Dataprint3000)
                return;
            prn_dp3000_sound_button();
            dp3000_paper_loaded = !dp3000_paper_loaded;
            dp3000_paper_lines = dp3000_paper_loaded ? DP3000_BATT_LINES : 0;
            if (!dp3000_paper_loaded && !cp80_queued.isEmpty()) {
                dp3000_paper_restart_required = true;
                dp3000_keyboard_paused = true;
            } else if (dp3000_paper_loaded && cp80_queued.isEmpty()) {
                dp3000_paper_restart_required = false;
            }
            cp80_render();
        });

        QObject::connect(dp3000_wp_btn, &QPushButton::toggled,
                         cp80_win, [](bool enabled) {
            if ((cp80_model != Cp80PrinterModel::Dataprint3000)
                || !dp3000_card_inserted)
                return;
            dp3000_card_write_protected = enabled;
            prn_dp3000_sound_button();
            cp80_render();
        });

        /* Only there when it is needed, which is the point: a flat pack should
           be noticed because the paper came out blank, and the fix should then
           be obvious rather than hidden in a menu. */
        cp80_replace = new QPushButton(QObject::tr("Connect adapter"), cp80_win);
        cp80_replace->setCheckable(true);
        cp80_replace->setToolTip(QObject::tr(
            "Plug in or unplug the AC adapter. Full charge is 14 hours for the "
            "DATAprint 3000 and 10 hours for the DPU-414; "
            "PEEPEEBOX_PRN_CHARGE=<minutes> shortens it for testing"));

        QObject::connect(cp80_replace, &QPushButton::clicked, cp80_win, []() {
            if (cp80_model == Cp80PrinterModel::Dataprint3000)
                prn_dp3000_sound_button();
            cp80_ac = !cp80_ac;
            if (!cp80_ac) {
                cp80_charging = false;
                if (cp80_model == Cp80PrinterModel::Dataprint3000
                    && dp3000_keyboard_job) {
                    dp3000_keyboard_job = false;
                    dp3000_keyboard_paused = false;
                    dp3000_transfer_error = true;
                }
            } else if (cp80_model == Cp80PrinterModel::Dataprint3000) {
                cp80_power = true;
                dp3000_transfer_error = false;
            }
            /* Deliberately does not come back online by itself: the manual has
               the operator connect the adapter and then push ONLINE, and a
               printer that restarted a job on its own would be a surprise. */
            cp80_render();
        });

        /* The lamps blink, so something has to tick even when nothing is being
           printed; the same tick moves the charge along. */
        cp80_blink_t = new QTimer(cp80_win);
        QObject::connect(cp80_blink_t, &QTimer::timeout, cp80_win, []() {
            const bool was_charging = cp80_charging;

            cp80_blink++;

            /* Charging is a state the machine is in, not a button that was
               pressed: the adapter is plugged in, the power is on, the pack is
               not full, and it is not printing.  The manual has charging pause
               while printing and resume after, and printing is the printer being
               online with something still to print -- a buffer sitting there
               with the printer offline is not printing and should not hold the
               charge up. */
            const bool printing = (prn_cp80_connected() != 0)
                                && !cp80_queued.isEmpty();

            if (cp80_model == Cp80PrinterModel::Dataprint3000) {
                /* The 3000 is recharged by its adapter and, when the socket
                   supplies enough voltage, by the connected game itself. */
                cp80_charging = cp80_power
                              && (cp80_ac || (prn_cp80_connected() != 0))
                              && (cp80_batt < CP80_BATT_FULL);
            } else {
                cp80_charging = cp80_ac && cp80_power && !printing
                              && (cp80_batt < CP80_BATT_FULL);
            }

            if (cp80_charging) {
                cp80_batt += (CP80_BATT_FULL / (cp80_charge * 60.0))
                           * (CP80_BLINK_MS / 1000.0);
                cp80_batt_dirty = true;
                if (cp80_batt >= CP80_BATT_FULL) {
                    cp80_batt     = CP80_BATT_FULL;
                    cp80_charging = false;
                    if (cp80_feed != nullptr)
                        cp80_feed->setInterval(CP80_FIRST_LINE_MS);
                }
            }

            /* Written at most every couple of seconds rather than on every
               line: it is one small file, but a print is forty lines and a
               charge is a tick every quarter second. */
            if (cp80_batt_dirty && ((cp80_blink % 8) == 0))
                cp80_batt_store();

            /* Repaint only when something on the machine is actually moving. */
            if (was_charging || cp80_charging || cp80_ac
                || (cp80_batt <= CP80_BATT_FLAT) || !cp80_queued.isEmpty()
                || dp3000_memory_fault || dp3000_transfer_error
                || !dp3000_paper_loaded)
                cp80_render();
        });
        cp80_blink_t->start(CP80_BLINK_MS);

        /* Whatever the pack was left at, before anything can spend it. */
        cp80_batt_load();
        if (cp80_model == Cp80PrinterModel::Dataprint3000) {
            dp3000_device_settings_load();
            if (dp3000_card_inserted && !dp3000_store_initialised)
                dp3000_store_load();
        }

        {
            const char *drain  = getenv("PEEPEEBOX_PRN_DRAIN");
            const char *charge = getenv("PEEPEEBOX_PRN_CHARGE");

            if (drain != NULL) {
                const double v = atof(drain);

                if ((v > 0.0) && (v <= 100.0))
                    cp80_drain = v;
            }
            if (charge != NULL) {
                const double v = atof(charge);

                if (v > 0.0) {
                    cp80_charge = v;
                    cp80_charge_overridden = true;
                }
            }
        }

        cp80_tear_timer = new QTimer(cp80_win);
        cp80_tear_timer->setTimerType(Qt::PreciseTimer);
        cp80_tear_timer->setInterval(CP80_TEAR_FRAME_MS);
        QObject::connect(cp80_tear_timer, &QTimer::timeout, cp80_win, []() {
            if (!cp80_tearing) {
                cp80_tear_timer->stop();
                return;
            }

            cp80_tear_frame++;
            if (cp80_tear_frame >= CP80_TEAR_FRAMES) {
                cp80_tearing = false;
                cp80_torn_text.clear();
                cp80_torn_batt.clear();
                cp80_tear_timer->stop();
                if (cp80_tear_btn != nullptr)
                    cp80_tear_btn->setEnabled(true);
            }
            cp80_render();
        });

        cp80_paper_motion = new QTimer(cp80_win);
        cp80_paper_motion->setTimerType(Qt::PreciseTimer);
        cp80_paper_motion->setInterval(CP80_PAPER_FRAME_MS);
        QObject::connect(cp80_paper_motion, &QTimer::timeout, cp80_win, []() {
            cp80_paper_frame++;
            if (cp80_paper_frame >= CP80_PAPER_FRAMES) {
                cp80_paper_frame = CP80_PAPER_FRAMES;
                cp80_paper_motion->stop();
            }
            cp80_render();
        });

        cp80_home_timer = new QTimer(cp80_win);
        cp80_home_timer->setSingleShot(true);
        QObject::connect(cp80_home_timer, &QTimer::timeout,
                         cp80_win, []() {
            if (!cp80_power || !cp80_head_away
                || (cp80_last_head_columns == 0))
                return;

            cp80_head_away = false;
            prn_cp80_sound_home(cp80_last_head_columns,
                                cp80_last_head_speed);
        });

        QObject::connect(cp80_tear_btn, &QPushButton::clicked, cp80_win, []() {
            if (cp80_tearing)
                return;

            if (cp80_printed.isEmpty())
                return;

            /* Freeze only the exposed part of the old roll.  The queued text,
               feed timer, parser column and bytes that arrived since the last
               UI pump all survive: tearing paper is a mechanical action, not a
               cancel-print command. */
            cp80_torn_text  = cp80_printed;
            cp80_torn_batt  = cp80_line_batt;
            cp80_torn_first = (cp80_scroll != nullptr) ? cp80_scroll->value() : 0;
            cp80_tear_frame = 0;
            cp80_tearing    = true;
            cp80_tear_btn->setEnabled(false);
            if (cp80_model == Cp80PrinterModel::Dataprint3000)
                prn_dp3000_sound_tear();
            else
                prn_cp80_sound_tear();

            prn_cp80_tear(&cp80_paper_at);
            cp80_edge_generation++;
            cp80_printed.clear();
            cp80_line_batt.clear();
            cp80_render();
            cp80_tear_timer->start();
        });

        QObject::connect(save, &QPushButton::clicked, cp80_win, [parent]() {
            const QString to = QFileDialog::getSaveFileName(
                parent, QObject::tr("Save paper"), QStringLiteral("receipt.txt"),
                QObject::tr("Text files (*.txt);;All files (*)"));

            if (to.isEmpty())
                return;

            QFile out(to);

            /* Everything printed, not only the lines the current screen shows, and
               whatever is still feeding -- nobody wants to wait for the roll to
               catch up before saving. */
            if (out.open(QIODevice::WriteOnly | QIODevice::Text))
                out.write((cp80_printed + cp80_queued).toUtf8());
            else
                QMessageBox::warning(parent, QObject::tr("Save paper"),
                                     QObject::tr("Could not write %1").arg(to));
        });

        /* Testing only: the pack takes 3000 lines to run down and ten hours to
           charge, and neither is a thing to sit through while checking what a
           threshold looks like.  Dragging this is not something the machine can
           do, so it says so. */
        cp80_batt_sl = new Cp80BatterySlider(cp80_win);
        cp80_batt_sl->setRange(0, 100);
        cp80_batt_sl->setValue(int(cp80_batt + 0.5));
        cp80_batt_sl->setFixedSize(132, 30);
        cp80_batt_sl->setToolTip(QObject::tr(
            "Battery pack: drag to set the test level. The machine drains it a "
            "line at a time; full charge is 14 hours for DATAprint and 10 hours "
            "for DPU-414."));

        QObject::connect(cp80_batt_sl, &QSlider::valueChanged, cp80_win, [](int v) {
            cp80_batt       = double(v);
            cp80_batt_dirty = true;
            if (cp80_feed != nullptr)
                cp80_feed->setInterval(
                    (cp80_model == Cp80PrinterModel::Dataprint3000)
                        ? DP3000_LINE_MS
                        : int(CP80_FIRST_LINE_MS
                              + ((CP80_LINE_MS_FLAT - CP80_FIRST_LINE_MS)
                                 * cp80_fade_at(cp80_drive_level()))));
            cp80_batt_store();
            cp80_render();
        });

        cp80_controls = new QWidget(cp80_win);
        auto *controls_box = new QVBoxLayout(cp80_controls);
        auto *row = new QHBoxLayout();

        controls_box->setContentsMargins(0, 0, 0, 0);
        controls_box->setSpacing(5);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);

        const int control_height = 27;
        cp80_pwr_btn->setFixedHeight(control_height);
        cp80_replace->setFixedHeight(control_height);
        cp80_link_btn->setFixedHeight(control_height);
        cp80_feed_btn->setFixedHeight(control_height);
        cp80_paper_btn->setFixedHeight(control_height);
        dp3000_wp_btn->setFixedHeight(control_height);
        cp80_tear_btn->setFixedHeight(control_height);
        save->setFixedHeight(control_height);

        row->addWidget(cp80_link_btn);
        row->addWidget(cp80_feed_btn);
        row->addWidget(cp80_pwr_btn);
        row->addWidget(cp80_replace);
        row->addWidget(cp80_paper_btn);
        row->addWidget(dp3000_wp_btn);
        row->addWidget(cp80_batt_sl);
        row->addStretch(1);
        row->addWidget(cp80_tear_btn);
        row->addWidget(save);

        controls_box->addLayout(row);

        cp80_layout = new QVBoxLayout(cp80_win);
        const int outer_margin = 11;

        cp80_layout->setContentsMargins(outer_margin, outer_margin,
                                        outer_margin, outer_margin);
        cp80_layout->setSpacing(0);
        cp80_layout->addWidget(cp80_view, 0,
                               Qt::AlignHCenter | Qt::AlignBottom);
        cp80_layout->addSpacing(8);
        cp80_layout->addWidget(cp80_status);
        cp80_layout->addWidget(cp80_controls);

        cp80_render();
    }

    cp80_win->show();
    cp80_win->raise();

    /* Headless visual-regression hook.  It is intentionally opt-in and saves
       the complete simulator dialog, including native controls. */
    if (created) {
        const char *capture = getenv("PEEPEEBOX_PRN_SCREENSHOT");

        if ((capture != nullptr) && (*capture != 0)) {
            const QString path = QString::fromUtf8(capture);
            QTimer::singleShot(500, cp80_win, [path]() {
                if (cp80_win != nullptr)
                    cp80_win->grab().save(path, "PNG");
            });
        }
    }

    /* Beside the cabinet, with the printer resting above the taskbar.  Wait one
       event-loop turn so frameGeometry includes the platform title bar. */
    if (created)
        QTimer::singleShot(0, cp80_win, [parent]() {
            cp80_place_initial(parent);

            if (cp80_win->windowHandle() != nullptr) {
                QObject::connect(cp80_win->windowHandle(), &QWindow::screenChanged,
                                 cp80_win, [](QScreen *screen) {
                    if (screen != nullptr)
                        cp80_available_hint = screen->availableGeometry();
                    cp80_render();
                });
            }
        });
}

void
MainWindow::on_actionPrinter_paper_triggered()
{
    cp80_show(this);
    cp80_pump();
}

void
MainWindow::on_actionOperator_setup_triggered()
{
    funworld_io_pulse(FWIO_LINE_SETUP);
}

void
MainWindow::on_actionCalibrate_triggered()
{
    funworld_io_pulse(FWIO_LINE_DOOR2);
}

/* PeepeeBox: the touchscreen the cabinet is fitted with.  Same shape as the dongle
   button above -- pause, ask, and hard reset if the answer changed, because the
   device attaches to its serial port at init. */
void
MainWindow::on_actionTouchscreen_triggered()
{
    const int currentPause = dopause;

    plat_pause(1);
    if (pp_touchscreen_dialog(this)) {
        config_changed = 2;
        config_save();
        pc_reset_hard();
    }
    plat_pause(currentPause);
}

/* PeepeeBox: the modem on COM4.  Same shape as the touchscreen and dongle
   buttons -- pause, ask, and hard reset if the answer changed, because fitting
   the modem is a COM port appearing rather than a setting being adjusted. */
void
MainWindow::on_actionModem_triggered()
{
    const int currentPause = dopause;

    plat_pause(1);
    if (pp_modem_dialog(this)) {
        config_changed = 2;
        config_save();
        pc_reset_hard();
    }
    plat_pause(currentPause);
}

/* PeepeeBox: fun.link on COM1.  Same shape again -- pause, ask, and hard reset if
   the answer changed, because fitting the adapter puts a device on a COM port. */
void
MainWindow::on_actionFunlink_triggered()
{
    const int currentPause = dopause;

    plat_pause(1);
    if (pp_funlink_dialog(this)) {
        config_changed = 2;
        config_save();
        pc_reset_hard();
    }
    plat_pause(currentPause);
}

/* PeepeeBox: the cabinets are offline, but the network card stays selectable --
   it is the one piece of hardware a user might legitimately want to add. */
/* PeepeeBox: attaching or detaching a drive changes what hardware exists, so it
   can only take effect through a hard reset.  That is not obvious from a menu
   tick, and losing whatever the guest was doing without warning is unpleasant --
   so ask first, with the same "don't show this again" affordance the hard reset
   confirmation already uses.  The preference is global and can be turned back on
   under Preferences -> Emulator. */
bool
MainWindow::confirmDriveChange(const QString &drive, bool attaching)
{
    if (!confirm_drive_change)
        return true;

    const QString text = (attaching
        ? tr("Attaching the %1 will restart the emulated machine. Any unsaved work in the guest will be lost.")
        : tr("Removing the %1 will restart the emulated machine. Any unsaved work in the guest will be lost."))
            .arg(drive);

    QMessageBox questionbox(QMessageBox::Icon::Question, EMU_NAME, text,
                            QMessageBox::Yes | QMessageBox::No, this);
    const auto chkbox = new QCheckBox(tr("Don't show this message again"));
    questionbox.setCheckBox(chkbox);
    chkbox->setChecked(!confirm_drive_change);

    QObject::connect(chkbox, &QCheckBox::CHECK_STATE_CHANGED, [](int state) {
        confirm_drive_change = (state == Qt::CheckState::Unchecked);
    });
    questionbox.exec();

    if (questionbox.result() == QMessageBox::No) {
        /* A dismissed warning should not also silence future ones. */
        confirm_drive_change = true;
        return false;
    }

    config_save_global();
    return true;
}

/* PeepeeBox: the cabinets had no optical drive, but installation and service
   media exist, so one can be switched on.  What the drive is is not a choice --
   see src/photoplay.c -- only whether it is there.  Attaching or removing it
   changes the emulated hardware, so the machine restarts. */
/* PeepeeBox: same reasoning as the CD-ROM -- the cabinets had no floppy drive,
   but 1.44M service and installation disks exist, so one 3.5" drive can be
   attached as A:. */
void
MainWindow::on_actionFloppy_drive_triggered(bool checked)
{
    if (!confirmDriveChange(tr("floppy drive"), checked)) {
        ui->actionFloppy_drive->setChecked(!checked);
        return;
    }

    const int currentPause = dopause;

    plat_pause(1);
    photoplay_set_fdd_enabled(checked);
    config_changed = 2;
    config_save();
    pc_reset_hard();
    plat_pause(currentPause);

    refreshMediaMenu();
}

void
MainWindow::on_actionCDROM_drive_triggered(bool checked)
{
    if (!confirmDriveChange(tr("CD-ROM drive"), checked)) {
        ui->actionCDROM_drive->setChecked(!checked);
        return;
    }

    const int currentPause = dopause;

    plat_pause(1);
    photoplay_set_cdrom_enabled(checked);
    config_changed = 2;
    config_save();
    pc_reset_hard();
    plat_pause(currentPause);

    refreshMediaMenu();
}

void
MainWindow::on_actionNetwork_triggered()
{
    const int currentPause = dopause;

    plat_pause(1);

    NetworkSettings dialog(this);
    dialog.setModal(true);
    dialog.setWindowModality(Qt::WindowModal);
    if (dialog.exec() == QDialog::Accepted) {
        config_changed = 2;
        config_save();
        pc_reset_hard();
    }

    plat_pause(currentPause);
}

void
MainWindow::processKeyboardInput(bool down, uint32_t keycode)
{
#if defined(Q_OS_WINDOWS) /* non-raw input */
    keycode &= 0xffff;
#elif defined(Q_OS_MACOS)
    keycode = (keycode < 127) ? cocoa_keycodes[keycode] : 0;
#elif defined(__HAIKU__)
    keycode = be_keycodes[keycode];
#else
#    ifdef XKBCOMMON
    if (xkbcommon_keymap)
        keycode = xkbcommon_translate(keycode);
    else
#    endif
#    ifdef EVDEV_KEYBOARD_HPP
        keycode = evdev_translate(keycode - 8);
#    else
    keycode = 0;
#    endif
#endif

    bool skip = main_window_blocked || (keycode < 0) || (kbd_req_capture && !mouse_capture) || qt_osd_is_visible();

    if (skip)
        return;

    /* Apply special cases. */
    switch (keycode) {
        default:
            break;

        case 0x54: /* Alt + Print Screen (special case, i.e. evdev SELECTIVE_SCREENSHOT) */
            /* Send Alt as well. */
            if (down) {
                keyboard_input(down, 0x38);
            } else {
                keyboard_input(down, keycode);
                keycode = 0x38;
            }
            break;

        case 0x80 ... 0xff:   /* regular break codes */
        case 0x10b:           /* Microsoft scroll up normal */
        case 0x180 ... 0x1ff: /* E0 break codes (including Microsoft scroll down normal) */
            /* This key uses a break code as make. Send it manually, only on press. */
            if (down && (mouse_capture || !kbd_req_capture || (video_fullscreen && !fullscreen_ui_visible))) {
                if (keycode & 0x100)
                    keyboard_send(0xe0);
                keyboard_send(keycode & 0xff);
            }
            return;

        case 0x11d: /* Right Ctrl */
            if (rctrl_is_lalt)
                keycode = 0x38; /* map to Left Alt */
            break;

        case 0x137:                                                  /* Print Screen */
            if (keyboard_recv_ui(0x38) || keyboard_recv_ui(0x138)) { /* Alt+ */
                keycode = 0x54;
            } else if (down) {
                keyboard_input(down, 0x12a);
            } else {
                keyboard_input(down, keycode);
                keycode = 0x12a;
            }
            break;

        case 0x145:                                                  /* Pause */
            if (keyboard_recv_ui(0x1d) || keyboard_recv_ui(0x11d)) { /* Ctrl+ */
                keycode = 0x146;
            } else {
                keyboard_input(down, 0xe11d);
                keycode &= 0x00ff;
            }
            break;
    }

    keyboard_input(down, keycode);
}

#ifdef Q_OS_MACOS
// These modifiers are listed as "device-dependent" in IOLLEvent.h, but
// that's followed up with "(really?)". It's the only way to distinguish
// left and right modifiers with Qt 6 on macOS, so let's just roll with it.
static std::unordered_map<uint32_t, uint16_t> mac_modifiers_to_xt = {
    { NX_DEVICELCTLKEYMASK,                0x1D  },
    { NX_DEVICELSHIFTKEYMASK,              0x2A  },
    { NX_DEVICERSHIFTKEYMASK,              0x36  },
    { NX_DEVICELCMDKEYMASK,                0x15B },
    { NX_DEVICERCMDKEYMASK,                0x15C },
    { NX_DEVICELALTKEYMASK,                0x38  },
    { NX_DEVICERALTKEYMASK,                0x138 },
    { NX_DEVICE_ALPHASHIFT_STATELESS_MASK, 0x3A  },
    { NX_DEVICERCTLKEYMASK,                0x11D },
};
static bool mac_iso_swap = false;

void
MainWindow::processMacKeyboardInput(bool down, const QKeyEvent *event)
{
    // Per QTBUG-69608 (https://bugreports.qt.io/browse/QTBUG-69608),
    // QKeyEvents QKeyEvents for presses/releases of modifiers on macOS give
    // nativeVirtualKey() == 0 (at least in Qt 6). Handle this by manually
    // processing the nativeModifiers(). We need to check whether the key() is
    // a known modifier because because kVK_ANSI_A is also 0, so the
    // nativeVirtualKey() == 0 condition is ambiguous...
    if (event->nativeVirtualKey() == 0
        && (event->key() == Qt::Key_Shift
            || event->key() == Qt::Key_Control
            || event->key() == Qt::Key_Meta
            || event->key() == Qt::Key_Alt
            || event->key() == Qt::Key_AltGr
            || event->key() == Qt::Key_CapsLock)) {
        // We only process one modifier at a time since events from Qt seem to
        // always be non-coalesced (NX_NONCOALESCEDMASK is always set).
        uint32_t changed_modifiers = last_modifiers ^ event->nativeModifiers();
        for (auto const &pair : mac_modifiers_to_xt) {
            if (changed_modifiers & pair.first) {
                last_modifiers ^= pair.first;
                keyboard_input(down, pair.second);
                return;
            }
        }

        // Caps Lock seems to be delivered as a single key press event when
        // enabled and a single key release event when disabled, so we can't
        // detect Caps Lock being held down; just send an infinitesimally-long
        // press and release as a compromise.
        //
        // The event also doesn't get delivered if you turn Caps Lock off after
        // turning it on when the window isn't focused. Doing better than this
        // probably requires bypassing Qt input processing.
        //
        // It's possible that other lock keys get delivered in this way, but
        // standard Apple keyboards don't have them, so this is untested.
        if (event->key() == Qt::Key_CapsLock) {
            keyboard_input(1, 0x3a);
            keyboard_input(0, 0x3a);
        }
    } else {
        /* Apple ISO keyboards are notorious for swapping ISO_Section and ANSI_Grave
           on *some* layouts and/or models. While macOS can sort this mess out at
           keymap level, it still provides applications with unfiltered, ambiguous
           keycodes, so we have to disambiguate them by making some bold assumptions
           about the user's keyboard layout based on the OS-provided key mappings. */
        auto nvk = event->nativeVirtualKey();
        if ((nvk == 0x0a) || (nvk == 0x32)) {
            /* Flaws:
               - Layouts with `~ on ISO_Section are partially detected due to a conflict with ANSI
               - Czech and Slovak are not detected as they have <> ANSI_Grave and \| ISO_Section (differing from PC actually)
               - Italian is partially detected due to \| conflicting with Brazilian
               - Romanian third level ANSI_Grave is unknown
               - Russian clusters <>, plusminus and paragraph into a four-level ANSI_Grave, with the aforementioned `~ on ISO_Section */
            auto key = event->key();
            if ((nvk == 0x32) && (                                                                 /* system reports ANSI_Grave for ISO_Section keys: */
                                  (key == Qt::Key_Less) || (key == Qt::Key_Greater) ||             /* Croatian, French, German, Icelandic, Italian, Norwegian, Portuguese, Spanish, Spanish Latin America, Turkish Q */
                                  (key == Qt::Key_Ugrave) ||                                       /* French Canadian */
                                  (key == Qt::Key_Icircumflex) ||                                  /* Romanian */
                                  (key == Qt::Key_Iacute) ||                                       /* Hungarian */
                                  (key == Qt::Key_BracketLeft) || (key == Qt::Key_BracketRight) || /* Russian upper two levels */
                                  (key == Qt::Key_W)                                               /* Turkish F */
                                  ))
                mac_iso_swap = true;
            else if ((nvk == 0x0a) && (                                                              /* system reports ISO_Section for ANSI_Grave keys: */
                                       (key == Qt::Key_paragraph) || (key == Qt::Key_plusminus) ||   /* Arabic, British, Bulgarian, Danish shifted, Dutch, Greek, Hebrew, Hungarian shifted, International English, Norwegian shifted, Portuguese, Russian lower two levels, Swiss unshifted, Swedish unshifted, Turkish F */
                                       (key == Qt::Key_At) || (key == Qt::Key_NumberSign) ||         /* Belgian, French */
                                       (key == Qt::Key_Apostrophe) ||                                /* Brazilian unshifted */
                                       (key == Qt::Key_QuoteDbl) ||                                  /* Brazilian shifted, Turkish Q unshifted */
                                       (key == Qt::Key_QuoteLeft) ||                                 /* Croatian (right quote unknown) */
                                       (key == Qt::Key_Dollar) ||                                    /* Danish unshifted */
                                       (key == Qt::Key_AsciiCircum) || (key == 0x1ffffff) ||         /* German unshifted (0x1ffffff according to one tester), Polish unshifted */
                                       (key == Qt::Key_degree) ||                                    /* German shifted, Icelandic unshifted, Spanish Latin America shifted, Swiss shifted, Swedish shifted */
                                       (key == Qt::Key_0) ||                                         /* Hungarian unshifted */
                                       (key == Qt::Key_diaeresis) ||                                 /* Icelandic shifted */
                                       (key == Qt::Key_acute) ||                                     /* Norwegian unshifted */
                                       (key == Qt::Key_Asterisk) ||                                  /* Polish shifted */
                                       (key == Qt::Key_masculine) || (key == Qt::Key_ordfeminine) || /* Spanish (masculine unconfirmed) */
                                       (key == Qt::Key_Eacute) ||                                    /* Turkish Q shifted */
                                       (key == Qt::Key_Slash)                                        /* French Canadian unshifted, Ukrainian shifted */
                                       ))
                mac_iso_swap = true;
#    if 0
            if (down) {
                QMessageBox questionbox(QMessageBox::Icon::Information, QString("Mac key swap test"), QString("nativeVirtualKey 0x%1\nnativeScanCode 0x%2\nkey 0x%3\nmac_iso_swap %4").arg(nvk, 0, 16).arg(event->nativeScanCode(), 0, 16).arg(key, 0, 16).arg(mac_iso_swap ? "yes" : "no"), QMessageBox::Ok, this);
                questionbox.exec();
            }
#    endif
            if (mac_iso_swap)
                nvk = (nvk == 0x0a) ? 0x32 : 0x0a;
        }
        // Special case for command + forward delete to send insert.
        if ((event->nativeModifiers() & NSEventModifierFlagCommand) && ((event->nativeVirtualKey() == nvk_Delete) || event->key() == Qt::Key_Delete)) {
            nvk = nvk_Insert; // Qt::Key_Help according to event->key()
        }

        processKeyboardInput(down, nvk);
    }
}
#endif

void
MainWindow::on_actionFullscreen_triggered()
{
    if (video_fullscreen > 0) {
        video_fullscreen = 0;
        showNormal();
        ui->menubar->show();
        if (!hide_status_bar)
            ui->statusbar->show();
        if (!hide_tool_bar)
            ui->toolBar->show();
        fullscreen_ui_visible = 0;
        if (vid_resize != 1) {
            emit resizeContents(vid_resize == 2 ? fixed_size_x : monitors[0].mon_scrnsz_x, vid_resize == 2 ? fixed_size_y : monitors[0].mon_scrnsz_y);
        }
    } else {
        if ((mouse_type != MOUSE_TYPE_NONE) || machine_has_mouse())
            emit setMouseCapture(true);
        video_fullscreen = 1;
        pclog("Full screen: %ix%i\n", QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        ui->menubar->hide();
        ui->statusbar->hide();
        ui->toolBar->hide();
        ui->stackedWidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
    }
    fs_on_signal  = false;
    fs_off_signal = false;
    ui->stackedWidget->onResize(ui->stackedWidget->width(), ui->stackedWidget->height());
}

QString
MainWindow::getTitle()
{
    return status_text;
}

// Helper to find an accelerator key and return it's sequence
// TODO: Is there a more central place to put this?
QKeySequence
MainWindow::FindAcceleratorSeq(const char *name)
{
    int accID = FindAccelerator(name);
    if (accID == -1)
        return QKeySequence();

    return (QKeySequence::fromString(acc_keys[accID].seq));
}

#include <iostream>

bool
MainWindow::eventFilter(QObject *receiver, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto      *ke   = static_cast<QKeyEvent *>(event);
        const bool down = event->type() == QEvent::KeyPress;

        /* While the OSD is open, route all key input to it, except for the
         * toggle accelerator itself so it can still close the overlay. */
        if (qt_osd_is_visible()) {

            if (qt_osd_key(ke->key(), ke->modifiers(), down, ke->isAutoRepeat(), ke->text().toUtf8().data())) {
                event->accept();
                return true;
            }
        }
    }

    // Detect shortcuts when menubar is hidden
    // TODO: Could this be simplified by proxying the event and manually
    // shoving it into the menubar?
    if (event->type() == QEvent::KeyPress) {
        // We check for mouse release even if we aren't fullscreen,
        // because it's not a menu accelerator.
        QKeyEvent *ke = (QKeyEvent *) event;
        if (mouse_capture) {
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("release_mouse") || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("release_mouse")) {
                /* Prevent an Alt-based shortcut from looking like a standalone
                 * Alt press to the guest when the held modifiers are released. */
                this->keyReleaseEvent(ke);
                keyboard_all_up();
                plat_mouse_capture(0);
                event->accept();
                return true;
            }
        }

        this->keyPressEvent(ke);

        if (event->type() == QEvent::KeyPress && video_fullscreen != 0) {
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("fullscreen")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("fullscreen")) {
                ui->actionFullscreen->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("hard_reset")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("hard_reset")) {
                ui->actionHard_Reset->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("pause")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("pause")) {
                ui->actionPause->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("mute")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("mute")) {
                ui->actionMute_Unmute->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("exit")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("exit")) {
                ui->actionExit->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("toggle_ui_fullscreen")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("toggle_ui_fullscreen")) {
                toggleFullscreenUI();
            }

            return true;
        }
    }

    if (!main_window_blocked && !dopause && (!kbd_req_capture || mouse_capture)) {
#if 0
        if (event->type() == QEvent::Shortcut) {
            auto shortcutEvent = (QShortcutEvent *) event;
            if (shortcutEvent->key() == ui->actionExit->shortcut()) {
                event->accept();
                return true;
            }
        }
#endif
        if (event->type() == QEvent::KeyPress) {
            event->accept();

            return true;
        }
        if (event->type() == QEvent::KeyRelease) {
            event->accept();
            this->keyReleaseEvent((QKeyEvent *) event);
            return true;
        }
    }

    if (receiver == this) {
        static auto curdopause = dopause;
        if (event->type() == QEvent::WindowBlocked) {
            if (qt_osd_is_visible())
                qt_osd_toggle();
            window_blocked = true;
            mouse_was_captured = (mouse_capture != 0);
            if (do_auto_dialog_pause > 0) {
                curdopause = dopause;
                plat_pause(isNonPause ? dopause : (isShowMessage ? 2 : 1));
            }
            if (mouse_was_captured)
                plat_mouse_capture(0);
            releaseKeyboard();
            main_window_blocked = 1;
        } else if (event->type() == QEvent::WindowUnblocked) {
            window_blocked = false;
            if (do_auto_dialog_pause > 0)
                plat_pause(curdopause);
            if (mouse_was_captured) {
                plat_mouse_capture(1);
            }
            main_window_blocked = 0;
        } else if (event->type() == QEvent::WindowStateChange) {
            if ((this->isFullScreen() && (video_fullscreen == 0)) ||
                (!this->isFullScreen() && (video_fullscreen == 1)))
                this->on_actionFullscreen_triggered();
        }
    }

    return QMainWindow::eventFilter(receiver, event);
}

void
MainWindow::refreshMediaMenu()
{
    ui->actionCDROM_drive->setChecked(photoplay_cdrom_enabled());
    ui->actionFloppy_drive->setChecked(photoplay_fdd_enabled());
    mm->refresh(ui->menuMedia);
    status->setSoundMenu(ui->menuSound);
    status->refresh(ui->statusbar);
    ui->actionMCA_devices->setVisible(machine_has_bus(machine, MACHINE_BUS_MCA));
    /* PeepeeBox: the 4DPS is a 486 with APM and no ACPI, so this is always
       a hard power off. */
    ui->actionACPI_Shutdown->setText((confirm_exit && confirm_exit_cmdl) ? tr("Power &off…") : tr("Power &off"));
    ui->actionACPI_Shutdown->setToolTip(tr("Power off"));
    ui->actionACPI_Shutdown->setEnabled(true);
    ui_update_force_interpreter();

    num_label->setToolTip(QShortcut::tr("Num Lock"));
    num_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    scroll_label->setToolTip(QShortcut::tr("Scroll Lock"));
    scroll_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    caps_label->setToolTip(QShortcut::tr("Caps Lock"));
    caps_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    kana_label->setToolTip(QShortcut::tr("Kana Lock"));
    int ext_ax_kbd = machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD) && (keyboard_type == KEYBOARD_TYPE_AX);
    int int_ax_kbd = machine_has_flags(machine, MACHINE_KEYBOARD_JIS) && !machine_has_bus(machine, MACHINE_BUS_PS2_PORTS);
    kana_label->setVisible(ext_ax_kbd || int_ax_kbd);

}

void
MainWindow::showMessage(int flags, const QString &header, const QString &message, bool richText)
{
    if (QThread::currentThread() == this->thread()) {
        if (!cpu_thread_running) {
            showMessageForNonQtThread(flags, header, message, richText, nullptr);
        } else
            showMessage_(flags, header, message, richText);
    } else {
        std::atomic_bool done = false;
        emit             showMessageForNonQtThread(flags, header, message, richText, &done);
        while (!done) {
            QThread::msleep(1);
        }
    }
}

void
MainWindow::showMessage_(int flags, const QString &header, const QString &message, bool richText, std::atomic_bool *done)
{
    if (done) {
        *done = false;
    }
    isShowMessage = true;

    auto defaultheader = QString();
    if (header.isEmpty()) {
        if (flags & (MBX_ERROR | MBX_FATAL))
            defaultheader = (flags & MBX_FATAL) ? tr("Fatal error") : tr("Error");
        else
            defaultheader = EMU_NAME;
    }
    QMessageBox box(QMessageBox::Information, (defaultheader.isEmpty() ? header : defaultheader), message, QMessageBox::Ok, this);

    if (flags & (MBX_ERROR | MBX_FATAL)) {
        box.setIcon(QMessageBox::Critical);
    } else if (flags & MBX_WARNING) {
        box.setIcon(QMessageBox::Warning);
//    } else if (flags & MBX_QUESTION) {
//        box.setIcon(QMessageBox::Question);
    }
    if (richText)
        box.setTextFormat(Qt::TextFormat::RichText);
    box.exec();
    if (done) {
        *done = true;
    }
    isShowMessage = false;
    if (cpu_thread_run == 0)
        QApplication::exit(-1);
}

void
MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (send_keyboard_input) {
#ifdef Q_OS_MACOS
        processMacKeyboardInput(true, event);
#else
        processKeyboardInput(true, event->nativeScanCode());
#endif
    }

    event->accept();
}

void
MainWindow::blitToWidget(int x, int y, int w, int h, int monitor_index)
{
    if (monitor_index >= 1) {
        if (renderers[monitor_index] && renderers[monitor_index]->isVisible())
            renderers[monitor_index]->blit(x, y, w, h);
        else
            video_blit_complete_monitor(monitor_index);
    } else
        ui->stackedWidget->blit(x, y, w, h);
}

void
MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Pause) {
        if (keyboard_recv_ui(0x38) && keyboard_recv_ui(0x138)) {
            plat_pause(dopause ^ 1);
        }
    }

    if (send_keyboard_input && !event->isAutoRepeat()) {
#ifdef Q_OS_MACOS
        processMacKeyboardInput(false, event);
#else
        processKeyboardInput(false, event->nativeScanCode());
#endif
    }
}

QSize
MainWindow::getRenderWidgetSize()
{
    return ui->stackedWidget->size();
}

void
MainWindow::focusInEvent(QFocusEvent *event)
{
    // this->grabKeyboard();
}

void
MainWindow::focusOutEvent(QFocusEvent *event)
{
    // this->releaseKeyboard();
}

static void
update_scaled_checkboxes(Ui::MainWindow *ui, QAction *selected)
{
    ui->action0_5x->setChecked(ui->action0_5x == selected);
    ui->action1x->setChecked(ui->action1x == selected);
    ui->action1_5x->setChecked(ui->action1_5x == selected);
    ui->action2x->setChecked(ui->action2x == selected);
    ui->action3x->setChecked(ui->action3x == selected);
    ui->action4x->setChecked(ui->action4x == selected);
    ui->action5x->setChecked(ui->action5x == selected);
    ui->action6x->setChecked(ui->action6x == selected);
    ui->action7x->setChecked(ui->action7x == selected);
    ui->action8x->setChecked(ui->action8x == selected);

    reset_screen_size();
    device_force_redraw();
    for (int i = 0; i < MONITORS_NUM; i++) {
        if (monitors[i].target_buffer)
            video_force_resize_set_monitor(1, i);
    }
    config_save();
}

void
MainWindow::on_action0_5x_triggered()
{
    scale = 0;
    update_scaled_checkboxes(ui, ui->action0_5x);
}

void
MainWindow::on_action1x_triggered()
{
    scale = 1;
    update_scaled_checkboxes(ui, ui->action1x);
}

void
MainWindow::on_action1_5x_triggered()
{
    scale = 2;
    update_scaled_checkboxes(ui, ui->action1_5x);
}

void
MainWindow::on_action2x_triggered()
{
    scale = 3;
    update_scaled_checkboxes(ui, ui->action2x);
}

void
MainWindow::on_action3x_triggered()
{
    scale = 4;
    update_scaled_checkboxes(ui, ui->action3x);
}

void
MainWindow::on_action4x_triggered()
{
    scale = 5;
    update_scaled_checkboxes(ui, ui->action4x);
}

void
MainWindow::on_action5x_triggered()
{
    scale = 6;
    update_scaled_checkboxes(ui, ui->action5x);
}

void
MainWindow::on_action6x_triggered()
{
    scale = 7;
    update_scaled_checkboxes(ui, ui->action6x);
}

void
MainWindow::on_action7x_triggered()
{
    scale = 8;
    update_scaled_checkboxes(ui, ui->action7x);
}

void
MainWindow::on_action8x_triggered()
{
    scale = 9;
    update_scaled_checkboxes(ui, ui->action8x);
}

void
MainWindow::on_actionNearest_triggered()
{
    video_filter_method = 0;
    ui->actionLinear->setChecked(false);
}

void
MainWindow::on_actionLinear_triggered()
{
    video_filter_method = 1;
    ui->actionNearest->setChecked(false);
}

void
MainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

void
MainWindow::on_actionAbout_86Box_triggered()
{
    const auto msgBox = new About(this);
    msgBox->exec();
}

void
MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(EMU_DOCS_URL));
}

void
MainWindow::on_actionHiDPI_scaling_triggered()
{
    dpi_scale ^= 1;
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    emit resizeContents(monitors[0].mon_scrnsz_x, monitors[0].mon_scrnsz_y);
    for (int i = 1; i < MONITORS_NUM; i++) {
        if (renderers[i])
            emit resizeContentsMonitor(monitors[i].mon_scrnsz_x, monitors[i].mon_scrnsz_y, i);
    }
    config_save();
}

void
MainWindow::on_actionUpdate_status_bar_icons_triggered()
{
    update_icons ^= 1;
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);

    /* Prevent icons staying when disabled during activity. */
    status->clearActivity();

    config_save();
}

void
MainWindow::toggleFullscreenUI()
{
    if (video_fullscreen == 0)
        return;

    fullscreen_ui_visible ^= 1;

    if (fullscreen_ui_visible) {
        // UI is being shown - save mouse capture state and release if captured
        mouse_was_captured = (mouse_capture != 0);
        if (mouse_was_captured) {
            plat_mouse_capture(0);
        }
    } else {
        // UI is being hidden - restore previous mouse capture state
        if (mouse_was_captured) {
            plat_mouse_capture(1);
        }
    }

    ui->menubar->setVisible(fullscreen_ui_visible);
    ui->statusbar->setVisible(fullscreen_ui_visible && !hide_status_bar);
    ui->toolBar->setVisible(fullscreen_ui_visible && !hide_tool_bar);
}

void
MainWindow::on_actionMute_Unmute_triggered()
{
    sound_muted ^= 1;
    config_save();
    status->updateSoundIcon();
    ui->actionMute_Unmute->setText(sound_muted ? tr("&Unmute") : tr("&Mute"));
}

void
MainWindow::on_actionSound_gain_triggered()
{
    SoundGain gain(this);
    gain.exec();
}

void
MainWindow::setSendKeyboardInput(bool enabled)
{
    send_keyboard_input = enabled;
}

void
MainWindow::updateUiPauseState()
{
    const auto pause_icon   = dopause ? QIcon(":/menuicons/qt/icons/run.ico") : QIcon(":/menuicons/qt/icons/pause.ico");
    const auto tooltip_text = dopause ? QString(tr("Resume execution")) : QString(tr("Pause execution"));
    const auto menu_text    = dopause ? QString(tr("Re&sume")) : QString(tr("&Pause"));
    ui->actionPause->setIcon(pause_icon);
    ui->actionPause->setToolTip(tooltip_text);
    ui->actionPause->setText(menu_text);
    emit vmmRunningStateChanged(static_cast<VMManagerProtocol::RunningState>(window_blocked ? (dopause ? VMManagerProtocol::RunningState::PausedWaiting : VMManagerProtocol::RunningState::RunningWaiting) : (VMManagerProtocol::RunningState) dopause));
}

void
MainWindow::updateStatusEmptyIcons()
{
    if (status != nullptr)
        status->refreshEmptyIcons();
}

void
MainWindow::on_actionPreferences_triggered()
{
    Preferences preferences(this);
    preferences.setModal(true);
    preferences.setWindowModality(Qt::WindowModal);
    preferences.setWindowFlag(Qt::CustomizeWindowHint, true);
    preferences.setWindowFlag(Qt::WindowTitleHint, true);
    preferences.setWindowFlag(Qt::WindowSystemMenuHint, false);
    preferences.exec();

    switch (preferences.result()) {
        default:
            break;
        case QDialog::Accepted:
            updateShortcuts();
            ui->actionHDD_manager->setEnabled(hdd_manager > 0);
            emit vmmGlobalConfigurationChanged();
            break;
        case QDialog::Rejected:
            break;
    }
}

void
MainWindow::showSettings()
{
    ui->actionSettings->trigger();
}

void
MainWindow::hardReset()
{
    ui->actionHard_Reset->trigger();
}

void
MainWindow::togglePause()
{
    ui->actionPause->trigger();
}

void
MainWindow::changeEvent(QEvent *event)
{
#ifdef Q_OS_WINDOWS
    if (event->type() == QEvent::LanguageChange) {
        auto size = this->centralWidget()->size();
        QApplication::setFont(Preferences::getUIFont());
        processEventsOnlyWhenPausedOrModal();
        main_window->centralWidget()->setFixedSize(size);
        processEventsOnlyWhenPausedOrModal();
        if (vid_resize == 1) {
            main_window->centralWidget()->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    }
#endif
    QWidget::changeEvent(event);
    if (isVisible()) {
        monitor_settings[0].mon_window_maximized = isMaximized();
        config_save();
    }
}

void
MainWindow::reloadAllRenderers()
{
    reload_renderers = true;
}

void
MainWindow::on_actionRenderer_options_triggered()
{
    if (const auto dlg = ui->stackedWidget->getOptions(this)) {
        if (dlg->exec() == QDialog::Accepted) {
            if (ui->stackedWidget->reloadRendererOption()) {
                ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                if (show_second_monitors) {
                    for (int i = 1; i < MONITORS_NUM; i++) {
                        if (renderers[i] && renderers[i]->reloadRendererOption() && renderers[i]->hasOptions()) {
                            ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                        }
                    }
                }
            } else
                for (int i = 1; i < MONITORS_NUM; i++) {
                    if (renderers[i] && renderers[i]->hasOptions())
                        renderers[i]->reloadOptions();
                }
            config_save();
        } else if (reload_renderers && ui->stackedWidget->reloadRendererOption()) {
            reload_renderers = false;
            ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
            if (show_second_monitors) {
                for (int i = 1; i < MONITORS_NUM; i++) {
                    if (renderers[i]) {
                        renderers[i]->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                    }
                }
            }
        }
    }
}

void
MainWindow::on_actionMCA_devices_triggered()
{
    if (const auto dlg = new MCADeviceList(this))
        dlg->exec();
}

void
MainWindow::on_actionMouse_triggered()
{
    mouse_input_mode = 0;
    mouse_input_mode_initial = 0;
    config_save();
}

void
MainWindow::on_actionTablet_triggered()
{
    mouse_input_mode = 1;
    mouse_input_mode_initial = 1;
    config_save();
}

void
MainWindow::on_actionTablet_Crosshair_triggered()
{
    mouse_input_mode = 2;
    mouse_input_mode_initial = 2;
    config_save();
}

void
MainWindow::on_actionACPI_Shutdown_triggered()
{
    if (confirm_exit && confirm_exit_cmdl) {
        QMessageBox questionbox(QMessageBox::Icon::Warning, EMU_NAME, tr("Powering off the emulated machine may cause data loss. Are you sure you want to continue?"), QMessageBox::Yes | QMessageBox::No, this);
        questionbox.setDefaultButton(QMessageBox::No);
        questionbox.exec();
        if (questionbox.result() != QMessageBox::Yes)
            return;
    }

    skip_exit_confirmation = true;
    on_actionExit_triggered();
}

