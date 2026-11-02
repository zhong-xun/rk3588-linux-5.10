#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHostAddress>
#include <QImage>
#include <QIcon>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <cstdio>
#include "boardbackend.h"
#include "boardprofile.h"
#include "gstvideopreviewitem.h"
#include "localwebserver.h"
#include "npu/npu_yolo_demo.h"
#include "qrimageprovider.h"
#include "qtcompat.h"
#include "resourcemonitor.h"
#include "systeminfo.h"

namespace {

bool argumentSelectsQtPlatform(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("-platform")
            || argument == QStringLiteral("--platform")
            || argument.startsWith(QStringLiteral("-platform="))
            || argument.startsWith(QStringLiteral("--platform="))) {
            return true;
        }
    }
    return false;
}

bool environmentValueSet(const char *name)
{
    return !qgetenv(name).trimmed().isEmpty();
}

QByteArray runtimeOsId()
{
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QByteArray();
    }

    const QList<QByteArray> lines = osRelease.readAll().split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (!line.startsWith("ID=")) {
            continue;
        }
        QByteArray value = line.mid(3).trimmed();
        if (value.size() >= 2
            && ((value.startsWith('"') && value.endsWith('"'))
                || (value.startsWith('\'') && value.endsWith('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        return value.trimmed().toLower();
    }
    return QByteArray();
}

void setEnvironmentDefault(const char *name, const char *value)
{
    if (!environmentValueSet(name)) {
        qputenv(name, value);
    }
}

void configureDefaultQtPlatform(int argc, char *argv[])
{
    if (environmentValueSet("QT_QPA_PLATFORM") || argumentSelectsQtPlatform(argc, argv)) {
        return;
    }

    // The supported product images have two deliberate graphics stacks:
    // Buildroot is a Weston/Wayland appliance, while every supported Ubuntu
    // and Debian image is an X11 desktop.  Do not let stale login-manager
    // variables silently move an Ubuntu/Debian launch onto Wayland, or a
    // Buildroot launch onto X11/DRM.
    const QByteArray platform = runtimeOsId() == QByteArrayLiteral("buildroot")
        ? QByteArrayLiteral("wayland")
        : QByteArrayLiteral("xcb");
    qputenv("QT_QPA_PLATFORM", platform);
}

void configureDefaultQtQuickRenderer()
{
    // Xvnc and several vendor X11 images expose an EGL path that is backed by
    // Mesa llvmpipe.  Let an explicit user choice win, but keep the default
    // X11 launch on Qt Quick's software renderer so an idle kiosk does not
    // consume all CPU cores just repainting a static dashboard.
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM").trimmed().toLower();
    if (platform != QByteArrayLiteral("xcb")) {
        return;
    }

    // Vendor environments often pre-set xcb_egl globally.  It is precisely
    // the unsafe path on Xvnc, so do not inherit it for BoardLab's X11 UI.
    qputenv("QT_XCB_GL_INTEGRATION", "none");
    setEnvironmentDefault("QT_QUICK_BACKEND", "software");
    setEnvironmentDefault("QSG_RENDER_LOOP", "basic");
}

QByteArray configuredWebAccessToken()
{
    const QByteArray environmentToken = qgetenv("BOARDLAB_WEB_TOKEN").trimmed();
    if (!environmentToken.isEmpty()) {
        return environmentToken;
    }

    QString tokenPath = QString::fromLocal8Bit(qgetenv("BOARDLAB_WEB_TOKEN_FILE")).trimmed();
    if (tokenPath.isEmpty()) {
        tokenPath = QStringLiteral("/etc/boardlab/web.token");
    }
    QFile tokenFile(tokenPath);
    if (!tokenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QByteArray();
    }
    return tokenFile.readAll().trimmed();
}

QObject *findQmlObjectByName(QObject *root, const QString &objectName, QSet<QObject *> *visited)
{
    if (!root || !visited || visited->contains(root)) {
        return nullptr;
    }
    visited->insert(root);

    if (root->objectName() == objectName) {
        return root;
    }

    if (QQuickItem *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> itemChildren = item->childItems();
        for (QQuickItem *child : itemChildren) {
            if (QObject *match = findQmlObjectByName(child, objectName, visited)) {
                return match;
            }
        }
    }

    const QObjectList objectChildren = root->children();
    for (QObject *child : objectChildren) {
        if (QObject *match = findQmlObjectByName(child, objectName, visited)) {
            return match;
        }
    }

    return nullptr;
}

bool visibleQmlObjectExists(QObject *root, const QString &objectName)
{
    if (!root || objectName.trimmed().isEmpty()) {
        return false;
    }

    QSet<QObject *> visited;
    QObject *object = findQmlObjectByName(root, objectName.trimmed(), &visited);
    if (!object) {
        return false;
    }

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        return false;
    }

    return item->isVisible()
           && item->opacity() > 0.01
           && item->width() > 0
           && item->height() > 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    configureDefaultQtPlatform(argc, argv);
    configureDefaultQtQuickRenderer();

    QGuiApplication app(argc, argv);
    // Keep the technical application id stable so existing desktop launchers,
    // cache locations and system integration continue to work after the
    // customer-facing product name changed.
    app.setApplicationName(QStringLiteral("BoardLab"));
    app.setApplicationDisplayName(QStringLiteral("迅为开发板测试平台"));
    app.setOrganizationName(QStringLiteral("北京迅为电子"));
    const QIcon applicationIcon(QStringLiteral(":/icons/boardlab.svg"));
    if (!applicationIcon.isNull()) {
        app.setWindowIcon(applicationIcon);
    }
    const QStringList fontFamilies = QFontDatabase().families();
    const QStringList preferredFonts = {
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans CJK"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("Droid Sans Fallback")
    };
    for (const QString &family : preferredFonts) {
        if (fontFamilies.contains(family)) {
            app.setFont(QFont(family));
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("北京迅为电子 · 迅为开发板测试平台（Qt Quick），适用于 RK3562/RK3568/RK3576/RK3588 Linux 开发板"));
    parser.addHelpOption();
    QCommandLineOption kioskOption(QStringList() << QStringLiteral("k") << QStringLiteral("kiosk"),
                                   QStringLiteral("Run full-screen without the desktop phone frame."));
    QCommandLineOption framedOption(QStringList() << QStringLiteral("framed"),
                                    QStringLiteral("Force desktop phone-frame preview mode."));
    QCommandLineOption reduceMotionOption(QStringList() << QStringLiteral("reduce-motion"),
                                          QStringLiteral("Disable non-essential interface animation."));
    QCommandLineOption screenshotOption(QStringList() << QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot to <path> and exit."),
                                        QStringLiteral("path"));
    QCommandLineOption screenshotDelayOption(QStringList() << QStringLiteral("screenshot-delay"),
                                             QStringLiteral("Delay screenshot capture by <ms>."),
                                             QStringLiteral("ms"),
                                             QStringLiteral("900"));
    QCommandLineOption windowSizeOption(QStringList() << QStringLiteral("window-size"),
                                        QStringLiteral("Resize the window to <width>x<height> before screenshot capture."),
                                        QStringLiteral("size"));
    QCommandLineOption lockedOption(QStringList() << QStringLiteral("locked"),
                                    QStringLiteral("Start on the lock screen for visual QA."));
    QCommandLineOption unlockedOption(QStringList() << QStringLiteral("unlocked"),
                                      QStringLiteral("Start on the home screen for visual QA. This is the default."));
    QCommandLineOption openAppOption(QStringList() << QStringLiteral("open-app"),
                                     QStringLiteral("Open a workspace surface after startup for visual QA."),
                                     QStringLiteral("title"));
    QCommandLineOption openToolOption(QStringList() << QStringLiteral("open-tool"),
                                      QStringLiteral("Open a board tools page after startup for visual QA."),
                                      QStringLiteral("tool"));
    QCommandLineOption startPreviewOption(QStringList() << QStringLiteral("start-preview"),
                                          QStringLiteral("Start an automatically negotiated live preview in a visual-QA instance "
                                                         "(camera or hdmiin)."),
                                          QStringLiteral("source"));
    QCommandLineOption openDropdownOption(QStringList() << QStringLiteral("open-dropdown"),
                                          QStringLiteral("Open a named dropdown after startup for visual QA."),
                                          QStringLiteral("name"));
    QCommandLineOption homePageOption(QStringList() << QStringLiteral("home-page"),
                                      QStringLiteral("Open home launcher page <index> for visual QA."),
                                      QStringLiteral("index"),
                                      QStringLiteral("0"));
    QCommandLineOption assertVisibleObjectOption(QStringList() << QStringLiteral("assert-visible-object"),
                                                 QStringLiteral("Fail visual QA unless the named QML object is visible after startup delay."),
                                                 QStringLiteral("objectName"));
    QCommandLineOption webOption(QStringList() << QStringLiteral("web"),
                                 QStringLiteral("Start the local Web hardware control console."));
    QCommandLineOption noWebOption(QStringList() << QStringLiteral("no-web"),
                                   QStringLiteral("Disable the local Web hardware control console."));
    QCommandLineOption webPortOption(QStringList() << QStringLiteral("web-port"),
                                     QStringLiteral("Local Web control port. Use 0 for an ephemeral port."),
                                     QStringLiteral("port"),
                                     QStringLiteral("17999"));
    QCommandLineOption webBindOption(QStringList() << QStringLiteral("web-bind"),
                                     QStringLiteral("Local Web bind address. Defaults to 0.0.0.0 for LAN access."),
                                     QStringLiteral("address"),
                                     QStringLiteral("0.0.0.0"));
    parser.addOption(kioskOption);
    parser.addOption(framedOption);
    parser.addOption(reduceMotionOption);
    parser.addOption(screenshotOption);
    parser.addOption(screenshotDelayOption);
    parser.addOption(windowSizeOption);
    parser.addOption(lockedOption);
    parser.addOption(unlockedOption);
    parser.addOption(openAppOption);
    parser.addOption(openToolOption);
    parser.addOption(startPreviewOption);
    parser.addOption(openDropdownOption);
    parser.addOption(homePageOption);
    parser.addOption(assertVisibleObjectOption);
    parser.addOption(webOption);
    parser.addOption(noWebOption);
    parser.addOption(webPortOption);
    parser.addOption(webBindOption);
    parser.process(app);

    const QString startupPreviewSource = parser.value(startPreviewOption).trimmed().toLower();
    if (!startupPreviewSource.isEmpty()
        && startupPreviewSource != QLatin1String("camera")
        && startupPreviewSource != QLatin1String("hdmiin")) {
        std::fprintf(stderr, "--start-preview only accepts camera or hdmiin\n");
        return 2;
    }

    QQuickStyle::setStyle(QStringLiteral("Material"));
    qmlRegisterType<GstVideoPreviewItem>("BoardLab", 1, 0, "GstVideoPreview");

    BoardProfile boardProfile;
    BoardBackend boardBackend(&boardProfile);
    SystemInfo systemInfo;
    ResourceMonitor resourceMonitor;
    NpuYoloDemo npuYoloDemo(&boardProfile);
    LocalWebServer localWebServer(&systemInfo, &boardBackend, &boardProfile,
                                  &resourceMonitor, &npuYoloDemo);
    // BoardLab is a local hardware workbench, so the desktop launch always
    // brings up its paired LAN console.  --no-web remains available for
    // isolated VNC and visual-QA instances that must not own the Web port.
    if (!parser.isSet(noWebOption)) {
        bool ok = false;
        const int parsedPort = parser.value(webPortOption).toInt(&ok);
        const quint16 webPort = ok && parsedPort >= 0 && parsedPort <= 65535 ? static_cast<quint16>(parsedPort) : 17999;
        const QHostAddress webBind(parser.value(webBindOption));
        if (!localWebServer.start(webBind.isNull() ? QHostAddress::Any : webBind,
                                  webPort,
                                  configuredWebAccessToken())) {
            std::fprintf(stderr, "%s\n", qPrintable(localWebServer.statusText()));
        }
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("qr"), new QrImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("boardBackend"), &boardBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("boardProfile"), &boardProfile);
    engine.rootContext()->setContextProperty(QStringLiteral("systemInfo"), &systemInfo);
    engine.rootContext()->setContextProperty(QStringLiteral("resourceMonitor"), &resourceMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("npuYoloDemo"), &npuYoloDemo);
    engine.rootContext()->setContextProperty(QStringLiteral("localWebServer"), &localWebServer);
    engine.rootContext()->setContextProperty(QStringLiteral("kioskMode"),
                                             !parser.isSet(framedOption) || parser.isSet(kioskOption));
    engine.rootContext()->setContextProperty(QStringLiteral("boardLabMotionEnabled"),
                                             !parser.isSet(reduceMotionOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startUnlocked"),
                                             parser.isSet(unlockedOption) || !parser.isSet(lockedOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startupApp"), parser.value(openAppOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startupTool"), parser.value(openToolOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startupPreviewSource"), startupPreviewSource);
    engine.rootContext()->setContextProperty(QStringLiteral("startupDropdown"), parser.value(openDropdownOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startupHomePageArgument"), parser.value(homePageOption));

    const QUrl mainUrl(QStringLiteral("qrc:/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [mainUrl](QObject *object, const QUrl &url) {
            if (!object && url == mainUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(mainUrl);
    if (parser.isSet(screenshotOption)) {
        const QList<QObject *> roots = engine.rootObjects();
        QQuickWindow *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.constFirst());
        if (!window) {
            return 2;
        }
        if (parser.isSet(windowSizeOption)) {
            const QStringList parts = parser.value(windowSizeOption).toLower().split(QLatin1Char('x'), boardlab::kSkipEmptyParts);
            bool widthOk = false;
            bool heightOk = false;
            const int width = parts.size() == 2 ? parts.at(0).toInt(&widthOk) : 0;
            const int height = parts.size() == 2 ? parts.at(1).toInt(&heightOk) : 0;
            if (widthOk && heightOk && width > 0 && height > 0) {
                window->setVisibility(QWindow::Windowed);
                window->setGeometry(0, 0, width, height);
                window->resize(width, height);
            }
        }

        const QString screenshotPath = parser.value(screenshotOption);
        const int delayMs = qMax(0, parser.value(screenshotDelayOption).toInt());
        const QStringList requiredVisibleObjects = parser.values(assertVisibleObjectOption);
        QTimer::singleShot(delayMs, &app, [window, screenshotPath, requiredVisibleObjects, &app]() {
            for (const QString &objectName : requiredVisibleObjects) {
                if (!visibleQmlObjectExists(window->contentItem(), objectName)) {
                    std::fprintf(stderr, "Visual smoke failed: QML object is not visible: %s\n",
                                 qPrintable(objectName));
                    QCoreApplication::exit(4);
                    return;
                }
            }

            const QFileInfo info(screenshotPath);
            if (!info.absolutePath().isEmpty()) {
                QDir().mkpath(info.absolutePath());
            }
            QSharedPointer<QQuickItemGrabResult> grab = window->contentItem()->grabToImage();
            if (!grab) {
                QCoreApplication::exit(3);
                return;
            }
            QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &app, [grab, screenshotPath]() {
                // Prefer the const overload; the non-const saveToFile is deprecated in Qt 5.15+.
                QCoreApplication::exit(const_cast<const QQuickItemGrabResult *>(grab.data())->saveToFile(screenshotPath) ? 0 : 3);
            });
        });
    }
    return app.exec();
}
