#include "gpubenchmark.h"
#include "qtcompat.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kMaximumLogBytes = 16 * 1024;
constexpr int kMaximumParserCharacters = 32 * 1024;
constexpr int kElapsedUpdateIntervalMs = 250;
constexpr int kStopGracePeriodMs = 1000;

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool testModeEnabled()
{
    return qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1");
}

bool isBuildrootRuntime()
{
    // Keep the override test-only so an arbitrary production environment
    // variable can never switch a board to the wrong graphics path.
    if (testModeEnabled() && qEnvironmentVariableIsSet("BOARDLAB_GPU_TEST_RUNTIME")) {
        return qEnvironmentVariable("BOARDLAB_GPU_TEST_RUNTIME").trimmed().compare(
                   QStringLiteral("buildroot"), Qt::CaseInsensitive)
            == 0;
    }

    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QStringList lines = QString::fromLocal8Bit(osRelease.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed();
        if (key != QStringLiteral("ID") && key != QStringLiteral("ID_LIKE")) {
            continue;
        }
        QString value = line.mid(separator + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        if (value.compare(QStringLiteral("buildroot"), Qt::CaseInsensitive) == 0
            || value.split(QRegularExpression(QStringLiteral("\\s+")), boardlab::kSkipEmptyParts)
                   .contains(QStringLiteral("buildroot"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString procRootPath()
{
    // Unit tests use a synthetic /proc tree so the same command-line, comm,
    // and X11-socket path taken on a board can be covered without launching a
    // real VNC/Xorg server.  Production always reads the kernel's /proc.
    if (testModeEnabled()) {
        const QString testProcRoot = qEnvironmentVariable("BOARDLAB_GPU_TEST_PROC_ROOT").trimmed();
        if (!testProcRoot.isEmpty()) {
            return testProcRoot;
        }
    }
    return QStringLiteral("/proc");
}

QString x11SocketDirectoryPath()
{
    if (testModeEnabled()) {
        const QString testSocketDirectory = qEnvironmentVariable(
            "BOARDLAB_GPU_TEST_X11_SOCKET_DIR").trimmed();
        if (!testSocketDirectory.isEmpty()) {
            return testSocketDirectory;
        }
    }
    return QStringLiteral("/tmp/.X11-unix");
}

QString executableFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isExecutable()) {
        return QString();
    }
    return info.absoluteFilePath();
}

QString normalizedXDisplay(const QString &display)
{
    static const QRegularExpression expression(
        QStringLiteral("^:([0-9]+)(?:\\.[0-9]+)?$"));
    const QRegularExpressionMatch match = expression.match(display.trimmed());
    return match.hasMatch() ? QStringLiteral(":%1").arg(match.captured(1)) : QString();
}

QStringList processIds()
{
    return QDir(procRootPath()).entryList(
        QStringList() << QStringLiteral("[0-9]*"), QDir::Dirs | QDir::NoDotAndDotDot);
}

QString processCommandLineText(const QString &processId)
{
    QFile commandLineFile(QDir(procRootPath()).filePath(processId + QStringLiteral("/cmdline")));
    if (!commandLineFile.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray commandLine = commandLineFile.readAll();
    commandLine.replace('\0', ' ');
    return QString::fromLocal8Bit(commandLine);
}

QStringList processCommandLineArguments(const QString &processId)
{
    QFile commandLineFile(QDir(procRootPath()).filePath(processId + QStringLiteral("/cmdline")));
    if (!commandLineFile.open(QIODevice::ReadOnly)) {
        return QStringList();
    }

    const QList<QByteArray> rawArguments = commandLineFile.readAll().split('\0');
    QStringList arguments;
    for (const QByteArray &argument : rawArguments) {
        if (!argument.isEmpty()) {
            arguments.append(QString::fromLocal8Bit(argument));
        }
    }
    return arguments;
}

QString processComm(const QString &processId)
{
    QFile commFile(QDir(procRootPath()).filePath(processId + QStringLiteral("/comm")));
    if (!commFile.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLocal8Bit(commFile.readAll()).trimmed();
}

bool processNameMatches(const QString &commandLine, const QString &commName,
                        const QStringList &needles)
{
    const QString executable = commandLine.section(QLatin1Char(' '), 0, 0);
    const QString executableName = QFileInfo(executable).fileName();
    for (const QString &needle : needles) {
        if (executableName.compare(needle, Qt::CaseInsensitive) == 0
            || commName.compare(needle, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool commandLineUsesXDisplay(const QString &commandLine, const QString &display)
{
    const QString normalizedDisplay = normalizedXDisplay(display);
    if (normalizedDisplay.isEmpty()) {
        return false;
    }

    const QRegularExpression displayExpression(
        QStringLiteral("(?:^|\\s)%1(?:\\.0)?(?:\\s|$)")
            .arg(QRegularExpression::escape(normalizedDisplay)));
    return displayExpression.match(commandLine).hasMatch();
}

bool commandLineMentionsXDisplay(const QString &commandLine)
{
    static const QRegularExpression displayExpression(
        QStringLiteral("(?:^|\\s):[0-9]+(?:\\.[0-9]+)?(?:\\s|$)"));
    return displayExpression.match(commandLine).hasMatch();
}

bool x11DisplaySocketPresent(const QString &display)
{
    const QString normalizedDisplay = normalizedXDisplay(display);
    if (normalizedDisplay.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(QDir(x11SocketDirectoryPath()).filePath(
                                 QStringLiteral("X%1").arg(normalizedDisplay.mid(1))));
}

bool processOnXDisplay(const QStringList &needles, const QString &display)
{
    for (const QString &processId : processIds()) {
        const QString commandLine = processCommandLineText(processId);
        if (processNameMatches(commandLine, processComm(processId), needles)
            && commandLineUsesXDisplay(commandLine, display)) {
            return true;
        }
    }
    return false;
}

bool currentDisplayIsVnc(const QString &display)
{
    const QString normalizedDisplay = normalizedXDisplay(display);
    if (normalizedDisplay.isEmpty()) {
        return false;
    }

    const QStringList vncProcessNames{
        QStringLiteral("Xvnc"),
        QStringLiteral("Xtigervnc"),
        QStringLiteral("tigervnc")
    };
    if (processOnXDisplay(vncProcessNames, normalizedDisplay)) {
        return true;
    }

    // Recent TigerVNC variants can expose only a short /proc/<pid>/comm or
    // hide argv from another user.  Do not guess from a VNC process alone:
    // require the active non-zero X socket as well, and reject a process whose
    // readable argv explicitly names some other X display.
    if (normalizedDisplay == QStringLiteral(":0")
        || !x11DisplaySocketPresent(normalizedDisplay)) {
        return false;
    }
    for (const QString &processId : processIds()) {
        const QString commandLine = processCommandLineText(processId);
        if (!processNameMatches(commandLine, processComm(processId), vncProcessNames)) {
            continue;
        }
        if (!commandLineMentionsXDisplay(commandLine)) {
            return true;
        }
    }
    return false;
}

struct LocalXorgSession {
    bool available = false;
    bool authoritySpecified = false;
    QString authorityPath;
};

QString xorgAuthorityPath(const QString &processId, bool *specified)
{
    if (specified) {
        *specified = false;
    }

    const QStringList arguments = processCommandLineArguments(processId);
    for (int index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        QString authority;
        if (argument == QStringLiteral("-auth")) {
            if (specified) {
                *specified = true;
            }
            if (index + 1 < arguments.size()) {
                authority = arguments.at(index + 1).trimmed();
            }
        } else if (argument.startsWith(QStringLiteral("-auth="))) {
            if (specified) {
                *specified = true;
            }
            authority = argument.mid(QStringLiteral("-auth=").size()).trimmed();
        } else {
            continue;
        }

        if (authority.isEmpty()) {
            return QString();
        }
        if (QDir::isRelativePath(authority)) {
            const QFileInfo processCwd(
                QDir(procRootPath()).filePath(processId + QStringLiteral("/cwd")));
            const QString workingDirectory = processCwd.symLinkTarget();
            if (workingDirectory.isEmpty()) {
                return QString();
            }
            authority = QDir(workingDirectory).absoluteFilePath(authority);
        }

        const QFileInfo authorityFile(authority);
        return authorityFile.isFile() && authorityFile.isReadable()
            ? authorityFile.absoluteFilePath() : QString();
    }
    return QString();
}

LocalXorgSession localXorgSession()
{
    LocalXorgSession session;
    if (!x11DisplaySocketPresent(QStringLiteral(":0"))) {
        return session;
    }

    const QStringList xorgProcessNames{QStringLiteral("Xorg")};
    for (const QString &processId : processIds()) {
        const QString commandLine = processCommandLineText(processId);
        const QString commName = processComm(processId);
        if (!processNameMatches(commandLine, commName, xorgProcessNames)) {
            continue;
        }
        // GDM often starts Xorg with -displayfd, so its argv has no literal
        // :0.  The existing X0 socket plus the exact Xorg comm name identifies
        // that server without treating a generic process as a display server.
        if (!commandLineUsesXDisplay(commandLine, QStringLiteral(":0"))
            && commName != QStringLiteral("Xorg")) {
            continue;
        }

        session.available = true;
        session.authorityPath = xorgAuthorityPath(processId, &session.authoritySpecified);
        return session;
    }
    return session;
}

bool isRetainedTerminalStatus(const QVariantMap &status, const QString &key)
{
    const QString value = status.value(key).toString();
    return value == QStringLiteral("completed")
        || value == QStringLiteral("failed")
        || value == QStringLiteral("stopped");
}

QString graphicsRuntimeProfile()
{
    return isBuildrootRuntime() ? QStringLiteral("buildroot-wayland")
                                : QStringLiteral("desktop-x11");
}

QString graphicsBackend()
{
    return isBuildrootRuntime() ? QStringLiteral("wayland")
                                : QStringLiteral("x11");
}

bool isSoftwareRendererText(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    const QString lowered = text.toLower();
    return lowered.contains(QStringLiteral("llvmpipe"))
        || lowered.contains(QStringLiteral("softpipe"))
        || lowered.contains(QStringLiteral("swiftshader"));
}

QString missingGlmark2Message()
{
    if (isBuildrootRuntime()) {
        return QStringLiteral(
            "Buildroot product images require glmark2-es2-wayland under the Weston/Wayland session.");
    }
    return QStringLiteral(
        "Ubuntu/Debian X11 requires glmark2-es2 (or glmark2-es).");
}

} // namespace

GpuBenchmark::GpuBenchmark(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_elapsedTimer(new QTimer(this))
    , m_stopGraceTimer(new QTimer(this))
    , m_previewElapsedTimer(new QTimer(this))
{
    m_elapsedTimer->setInterval(kElapsedUpdateIntervalMs);
    m_stopGraceTimer->setSingleShot(true);
    m_previewElapsedTimer->setInterval(kElapsedUpdateIntervalMs);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &GpuBenchmark::onStandardOutputReady);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &GpuBenchmark::onStandardErrorReady);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GpuBenchmark::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &GpuBenchmark::onProcessError);
    connect(m_elapsedTimer, &QTimer::timeout,
            this, &GpuBenchmark::onElapsedTimer);
    connect(m_stopGraceTimer, &QTimer::timeout,
            this, &GpuBenchmark::onStopGraceExpired);
    connect(m_previewElapsedTimer, &QTimer::timeout,
            this, &GpuBenchmark::onPreviewElapsedTimer);

    m_status.insert(QStringLiteral("available"), false);
    m_status.insert(QStringLiteral("running"), false);
    m_status.insert(QStringLiteral("binary"), QString());
    m_status.insert(QStringLiteral("candidates"), candidatePrograms());
    m_status.insert(QStringLiteral("arguments"), fixedArguments());
    m_status.insert(QStringLiteral("display"), QString());
    m_status.insert(QStringLiteral("displayStrategy"), QStringLiteral("inherited"));
    m_status.insert(QStringLiteral("runtimeProfile"), graphicsRuntimeProfile());
    m_status.insert(QStringLiteral("graphicsBackend"), graphicsBackend());
    m_status.insert(QStringLiteral("graphicsMessage"), QString());
    m_status.insert(QStringLiteral("status"), QStringLiteral("idle"));
    m_status.insert(QStringLiteral("startedAt"), QString());
    m_status.insert(QStringLiteral("finishedAt"), QString());
    m_status.insert(QStringLiteral("elapsedMs"), 0);
    m_status.insert(QStringLiteral("elapsedSeconds"), 0.0);
    m_status.insert(QStringLiteral("score"), -1);
    m_status.insert(QStringLiteral("renderer"), QString());
    m_status.insert(QStringLiteral("exitCode"), -1);
    m_status.insert(QStringLiteral("exitStatus"), QStringLiteral("not-started"));
    m_status.insert(QStringLiteral("log"), QString());
    m_status.insert(QStringLiteral("logTruncated"), false);
    m_status.insert(QStringLiteral("error"), QString());
    m_status.insert(QStringLiteral("previewAvailable"), false);
    m_status.insert(QStringLiteral("previewRunning"), false);
    m_status.insert(QStringLiteral("previewArguments"), fixedPreviewArguments());
    m_status.insert(QStringLiteral("previewEmbedded"), true);
    m_status.insert(QStringLiteral("previewMode"), QStringLiteral("qtquick-shader"));
    m_status.insert(QStringLiteral("previewDisplay"), QStringLiteral("GPU 测试页"));
    m_status.insert(QStringLiteral("previewDisplayStrategy"), QStringLiteral("embedded-qt-quick"));
    m_status.insert(QStringLiteral("previewTarget"), QStringLiteral("gpu-test-page"));
    m_status.insert(QStringLiteral("previewMessage"),
                    QStringLiteral("GPU 图形渲染正在迅为开发板测试平台的 Qt GPU 测试页内进行。"));
    m_status.insert(QStringLiteral("previewWindowExpected"), false);
    m_status.insert(QStringLiteral("previewStatus"), QStringLiteral("idle"));
    m_status.insert(QStringLiteral("previewStartedAt"), QString());
    m_status.insert(QStringLiteral("previewFinishedAt"), QString());
    m_status.insert(QStringLiteral("previewElapsedMs"), 0);
    m_status.insert(QStringLiteral("previewElapsedSeconds"), 0.0);
    m_status.insert(QStringLiteral("previewScore"), -1);
    m_status.insert(QStringLiteral("previewRenderer"), QString());
    m_status.insert(QStringLiteral("previewExitCode"), -1);
    m_status.insert(QStringLiteral("previewExitStatus"), QStringLiteral("not-started"));
    m_status.insert(QStringLiteral("previewLog"), QString());
    m_status.insert(QStringLiteral("previewLogTruncated"), false);
    m_status.insert(QStringLiteral("previewError"), QString());
}

GpuBenchmark::~GpuBenchmark()
{
    m_elapsedTimer->stop();
    m_stopGraceTimer->stop();
    m_previewElapsedTimer->stop();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(kStopGracePeriodMs);
    }
}

QVariantMap GpuBenchmark::status() const
{
    return m_status;
}

QVariantMap GpuBenchmark::state() const
{
    return status();
}

bool GpuBenchmark::running() const
{
    return m_status.value(QStringLiteral("running")).toBool();
}

bool GpuBenchmark::previewRunning() const
{
    return m_status.value(QStringLiteral("previewRunning")).toBool();
}

QString GpuBenchmark::log() const
{
    return m_status.value(QStringLiteral("log")).toString();
}

QStringList GpuBenchmark::candidatePrograms()
{
    // Buildroot product images always run BoardLab under the packaged Weston
    // compositor. Do not fall back to a generic binary that could have been
    // built for X11: the product test must use the Wayland flavor.
    if (isBuildrootRuntime()) {
        return QStringList{QStringLiteral("glmark2-es2-wayland")};
    }

    // Every supported Ubuntu/Debian image uses X11.  Keep this list fixed even
    // when a login manager or another user leaves a Wayland socket visible.
    return QStringList{QStringLiteral("glmark2-es2"), QStringLiteral("glmark2-es")};
}

QStringList GpuBenchmark::fixedArguments()
{
    return QStringList{
        QStringLiteral("--off-screen"),
        QStringLiteral("--size"),
        QStringLiteral("640x480"),
        QStringLiteral("--benchmark"),
        QStringLiteral("refract:duration=3")
    };
}

QStringList GpuBenchmark::fixedPreviewArguments()
{
    // Metadata exposed through the same status map as the benchmark. The
    // preview is intentionally a Qt Quick scene-graph effect embedded in the
    // current page, not a glmark2 command line or a new compositor surface.
    return QStringList{
        QStringLiteral("embedded-qt-quick"),
        QStringLiteral("ShaderEffect"),
        QStringLiteral("continuous-until-stopped")
    };
}

int GpuBenchmark::parseScore(const QString &text)
{
    static const QRegularExpression expression(
        QStringLiteral("\\bglmark2\\s+score\\s*:\\s*([0-9]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    int score = -1;
    QRegularExpressionMatchIterator matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        bool ok = false;
        const int parsed = match.captured(1).toInt(&ok);
        if (ok) {
            score = parsed;
        }
    }
    return score;
}

QString GpuBenchmark::parseRenderer(const QString &text)
{
    static const QRegularExpression expression(
        QStringLiteral("\\bGL_RENDERER\\s*:\\s*([^\\r\\n]+)"),
        QRegularExpression::CaseInsensitiveOption);

    QString renderer;
    QRegularExpressionMatchIterator matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QString candidate = matches.next().captured(1).trimmed();
        if (!candidate.isEmpty()) {
            renderer = candidate;
        }
    }
    return renderer;
}

QVariantMap GpuBenchmark::discover()
{
    if (running() || previewRunning()) {
        return status();
    }

    const QString program = resolveProgram();
    const DisplayPlan plan = displayPlan();
    const bool retainRunResult = !program.isEmpty()
        && isRetainedTerminalStatus(m_status, QStringLiteral("status"));
    const bool retainPreviewResult = isRetainedTerminalStatus(m_status, QStringLiteral("previewStatus"));
    QVariantMap changes;
    changes.insert(QStringLiteral("available"), !program.isEmpty());
    changes.insert(QStringLiteral("binary"), program);
    changes.insert(QStringLiteral("candidates"), candidatePrograms());
    changes.insert(QStringLiteral("arguments"), fixedArguments());
    changes.insert(QStringLiteral("display"), plan.display);
    changes.insert(QStringLiteral("displayStrategy"), plan.strategy);
    changes.insert(QStringLiteral("runtimeProfile"), graphicsRuntimeProfile());
    changes.insert(QStringLiteral("graphicsBackend"), plan.target);
    changes.insert(QStringLiteral("graphicsMessage"), plan.message);
    if (!retainRunResult) {
        changes.insert(QStringLiteral("status"), program.isEmpty()
                       ? QStringLiteral("unavailable")
                       : QStringLiteral("ready"));
        changes.insert(QStringLiteral("error"), program.isEmpty()
                       ? missingGlmark2Message()
                       : QString());
    }
    // The embedded ShaderEffect is part of the already running Qt Quick
    // application. It must not depend on glmark2 or on a second Wayland/X11
    // display, because its entire purpose is to remain inside this page.
    changes.insert(QStringLiteral("previewAvailable"), true);
    changes.insert(QStringLiteral("previewArguments"), fixedPreviewArguments());
    changes.insert(QStringLiteral("previewEmbedded"), true);
    changes.insert(QStringLiteral("previewMode"), QStringLiteral("qtquick-shader"));
    changes.insert(QStringLiteral("previewDisplay"), QStringLiteral("GPU 测试页"));
    changes.insert(QStringLiteral("previewDisplayStrategy"), QStringLiteral("embedded-qt-quick"));
    changes.insert(QStringLiteral("previewTarget"), QStringLiteral("gpu-test-page"));
    changes.insert(QStringLiteral("previewMessage"),
                   QStringLiteral("GPU 图形渲染正在迅为开发板测试平台的 Qt GPU 测试页内进行。"));
    changes.insert(QStringLiteral("previewWindowExpected"), false);
    if (!retainPreviewResult) {
        changes.insert(QStringLiteral("previewStatus"), QStringLiteral("ready"));
        changes.insert(QStringLiteral("previewError"), QString());
    }
    updateStatus(changes);
    return status();
}

bool GpuBenchmark::start()
{
    if (running() || previewRunning() || m_process->state() != QProcess::NotRunning) {
        if (previewRunning()) {
            updateStatus(QVariantMap{
                {QStringLiteral("error"),
                 QStringLiteral("Embedded GPU preview is running; stop it before starting the score benchmark.")}
            });
        }
        return false;
    }

    const QString program = resolveProgram();
    if (program.isEmpty()) {
        discover();
        return false;
    }

    const DisplayPlan plan = displayPlan();
    resetRunState(program, plan);
    m_process->setProgram(program);
    m_process->setArguments(fixedArguments());
    m_process->setProcessEnvironment(plan.environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();
    return true;
}

bool GpuBenchmark::stop()
{
    if (!running() || m_stopRequested) {
        return false;
    }

    m_stopRequested = true;
    publishElapsed();
    updateStatus(QVariantMap{
        {QStringLiteral("status"), QStringLiteral("stopping")},
        {QStringLiteral("error"), QString()}
    });

    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_stopGraceTimer->start(kStopGracePeriodMs);
    }
    return true;
}

bool GpuBenchmark::startPreview()
{
    if (previewRunning() || running() || m_process->state() != QProcess::NotRunning) {
        const QString reason = running()
            ? QStringLiteral("GPU score benchmark is running; wait for it to finish before starting the embedded preview.")
            : QStringLiteral("Embedded GPU preview is already running.");
        updateStatus(QVariantMap{
            {QStringLiteral("previewStatus"), QStringLiteral("blocked")},
            {QStringLiteral("previewError"), reason},
            {QStringLiteral("previewWindowExpected"), false}
        });
        return false;
    }

    resetEmbeddedPreviewState();
    return true;
}

bool GpuBenchmark::stopPreview()
{
    if (!previewRunning()) {
        return false;
    }

    publishPreviewElapsed();
    updateStatus(QVariantMap{
        {QStringLiteral("previewStatus"), QStringLiteral("stopping")},
        {QStringLiteral("previewError"), QString()},
        {QStringLiteral("previewMessage"),
         QStringLiteral("Embedded GPU preview stop requested.")}
    });
    finishPreview(true, QStringLiteral("stopped"), QString(), 0, QProcess::NormalExit);
    return true;
}

void GpuBenchmark::onStandardOutputReady()
{
    appendLog(m_process->readAllStandardOutput());
}

void GpuBenchmark::onStandardErrorReady()
{
    appendLog(m_process->readAllStandardError());
}

void GpuBenchmark::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!running()) {
        return;
    }

    appendLog(m_process->readAllStandardOutput());
    appendLog(m_process->readAllStandardError());

    const bool stopped = m_stopRequested;
    if (stopped) {
        finishRun(true, QStringLiteral("stopped"), QString(), exitCode, exitStatus);
        return;
    }

    if (exitStatus != QProcess::NormalExit) {
        finishRun(false, QStringLiteral("failed"),
                  QStringLiteral("GPU benchmark crashed before it completed."),
                  exitCode, exitStatus);
        return;
    }
    if (exitCode != 0) {
        finishRun(false, QStringLiteral("failed"),
                  QStringLiteral("GPU benchmark exited with code %1.").arg(exitCode),
                  exitCode, exitStatus);
        return;
    }
    if (m_status.value(QStringLiteral("score")).toInt() < 0) {
        finishRun(false, QStringLiteral("failed"),
                  QStringLiteral("GPU benchmark completed without a glmark2 score."),
                  exitCode, exitStatus);
        return;
    }
    // A completed score on llvmpipe/softpipe/SwiftShader is not a valid board
    // GPU result for product acceptance.
    const QString renderer = m_status.value(QStringLiteral("renderer")).toString();
    if (isSoftwareRendererText(renderer) || isSoftwareRendererText(m_parseBuffer)) {
        finishRun(false, QStringLiteral("failed"),
                  QStringLiteral("GPU benchmark used a software renderer (%1); hardware GLES is required.")
                      .arg(renderer.isEmpty() ? QStringLiteral("llvmpipe/softpipe/SwiftShader")
                                              : renderer),
                  exitCode, exitStatus);
        return;
    }
    finishRun(true, QStringLiteral("completed"), QString(), exitCode, exitStatus);
}

void GpuBenchmark::onProcessError(QProcess::ProcessError error)
{
    if (!running() || error != QProcess::FailedToStart) {
        return;
    }

    finishRun(false, QStringLiteral("failed"),
              QStringLiteral("Unable to start the GPU benchmark executable."),
              -1, QProcess::CrashExit);
}

void GpuBenchmark::onElapsedTimer()
{
    publishElapsed();
}

void GpuBenchmark::onStopGraceExpired()
{
    if (running() && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

void GpuBenchmark::onPreviewElapsedTimer()
{
    publishPreviewElapsed();
}

QString GpuBenchmark::resolveProgram() const
{
    // A fake executable is permitted only for the standalone test fixture.
    // It must be absolute so test input cannot redirect PATH resolution.
    const QString injected = qEnvironmentVariable("BOARDLAB_GPU_BENCHMARK_PROGRAM").trimmed();
    if (testModeEnabled() && !injected.isEmpty()) {
        if (!QDir::isAbsolutePath(injected)) {
            return QString();
        }
        return executableFile(injected);
    }

    for (const QString &candidate : candidatePrograms()) {
        const QString located = QStandardPaths::findExecutable(candidate);
        if (!located.isEmpty()) {
            return located;
        }
    }
    return QString();
}

GpuBenchmark::DisplayPlan GpuBenchmark::displayPlan() const
{
    DisplayPlan plan;
    plan.environment = QProcessEnvironment::systemEnvironment();

    if (isBuildrootRuntime()) {
        // The Buildroot package starts BoardLab after Weston. Keep the child
        // benchmark on that socket even when a stale DISPLAY leaks in from an
        // unrelated shell or desktop environment.
        const QString runtimeDirectory = plan.environment.value(QStringLiteral("XDG_RUNTIME_DIR")).trimmed().isEmpty()
            ? QStringLiteral("/run")
            : plan.environment.value(QStringLiteral("XDG_RUNTIME_DIR")).trimmed();
        const QString waylandDisplay = plan.environment.value(QStringLiteral("WAYLAND_DISPLAY")).trimmed().isEmpty()
            ? QStringLiteral("wayland-0")
            : plan.environment.value(QStringLiteral("WAYLAND_DISPLAY")).trimmed();
        plan.environment.remove(QStringLiteral("DISPLAY"));
        plan.environment.remove(QStringLiteral("XAUTHORITY"));
        plan.environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtimeDirectory);
        plan.environment.insert(QStringLiteral("WAYLAND_DISPLAY"), waylandDisplay);
        plan.display = waylandDisplay;
        plan.strategy = QStringLiteral("buildroot-wayland");
        plan.target = QStringLiteral("wayland");
        plan.message = QStringLiteral("Buildroot product benchmark uses the Weston/Wayland session.");
        return plan;
    }

    const QString inheritedDisplay = plan.environment.value(QStringLiteral("DISPLAY")).trimmed();
    plan.display = inheritedDisplay;
    plan.strategy = inheritedDisplay.isEmpty()
        ? QStringLiteral("x11-display-missing")
        : QStringLiteral("inherited-x11");
    plan.target = QStringLiteral("x11");
    plan.message = inheritedDisplay.isEmpty()
        ? QStringLiteral("Ubuntu/Debian requires an active X11 DISPLAY for the GPU benchmark.")
        : QStringLiteral("Desktop benchmark uses the current X11 display.");

    // An administrator can select a known X display for a local benchmark.
    // Only a conventional local X display is accepted; this never becomes a
    // caller-controlled program argument or shell expression.
    const QString configuredDisplay = normalizedXDisplay(
        qEnvironmentVariable("BOARDLAB_GPU_DISPLAY"));
    if (!configuredDisplay.isEmpty()) {
        plan.environment.insert(QStringLiteral("DISPLAY"), configuredDisplay);
        plan.display = configuredDisplay;
        plan.strategy = QStringLiteral("configured-display");
        plan.target = QStringLiteral("x11");
        plan.message = QStringLiteral("Desktop benchmark uses the administrator-selected X11 display.");
        return plan;
    }

    // Xvnc commonly provides a software X11 display that cannot initialize
    // Rockchip's EGL implementation.  When that exact situation is detected
    // and the physical Xorg display is present, run the off-screen workload
    // against Xorg instead.  It is still the same fixed glmark2 workload and
    // does not open a visible benchmark window.  Take Xorg's own authority
    // file when it is readable: a VNC session's cookie normally does not
    // authorize the local display.
    const bool inheritedDisplayIsVnc = currentDisplayIsVnc(inheritedDisplay);
    const LocalXorgSession localXorg = localXorgSession();
    if (inheritedDisplayIsVnc && localXorg.available
        && (!localXorg.authoritySpecified || !localXorg.authorityPath.isEmpty())) {
        plan.environment.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
        if (!localXorg.authorityPath.isEmpty()) {
            plan.environment.insert(QStringLiteral("XAUTHORITY"), localXorg.authorityPath);
        }
        plan.display = QStringLiteral(":0");
        plan.strategy = QStringLiteral("vnc-local-xorg");
        plan.target = QStringLiteral("x11");
        plan.message = localXorg.authorityPath.isEmpty()
            ? QStringLiteral("Desktop benchmark uses local Xorg instead of the software VNC display.")
            : QStringLiteral("Desktop benchmark uses local Xorg and its Xauthority instead of the software VNC display.");
        return plan;
    }

    if (inheritedDisplayIsVnc && localXorg.available
        && localXorg.authoritySpecified) {
        plan.message = QStringLiteral(
            "Local Xorg was detected, but its Xauthority is not readable; retaining the current VNC display.");
    }

    return plan;
}

void GpuBenchmark::resetRunState(const QString &program, const DisplayPlan &plan)
{
    m_stopRequested = false;
    m_stopGraceTimer->stop();
    m_elapsedClock.start();
    m_elapsedTimer->start();
    m_logBytes.clear();
    m_parseBuffer.clear();

    QVariantMap changes;
    changes.insert(QStringLiteral("available"), true);
    changes.insert(QStringLiteral("running"), true);
    changes.insert(QStringLiteral("binary"), program);
    changes.insert(QStringLiteral("candidates"), candidatePrograms());
    changes.insert(QStringLiteral("arguments"), fixedArguments());
    changes.insert(QStringLiteral("display"), plan.display);
    changes.insert(QStringLiteral("displayStrategy"), plan.strategy);
    changes.insert(QStringLiteral("status"), QStringLiteral("running"));
    changes.insert(QStringLiteral("startedAt"), nowIso());
    changes.insert(QStringLiteral("finishedAt"), QString());
    changes.insert(QStringLiteral("elapsedMs"), 0);
    changes.insert(QStringLiteral("elapsedSeconds"), 0.0);
    changes.insert(QStringLiteral("score"), -1);
    changes.insert(QStringLiteral("renderer"), QString());
    changes.insert(QStringLiteral("exitCode"), -1);
    changes.insert(QStringLiteral("exitStatus"), QStringLiteral("running"));
    changes.insert(QStringLiteral("log"), QString());
    changes.insert(QStringLiteral("logTruncated"), false);
    changes.insert(QStringLiteral("error"), QString());
    updateStatus(changes);
}

void GpuBenchmark::resetEmbeddedPreviewState()
{
    m_previewElapsedClock.start();
    m_previewElapsedTimer->start();

    QVariantMap changes;
    changes.insert(QStringLiteral("previewAvailable"), true);
    changes.insert(QStringLiteral("previewRunning"), true);
    changes.insert(QStringLiteral("previewArguments"), fixedPreviewArguments());
    changes.insert(QStringLiteral("previewEmbedded"), true);
    changes.insert(QStringLiteral("previewMode"), QStringLiteral("qtquick-shader"));
    changes.insert(QStringLiteral("previewDisplay"), QStringLiteral("GPU 测试页"));
    changes.insert(QStringLiteral("previewDisplayStrategy"), QStringLiteral("embedded-qt-quick"));
    changes.insert(QStringLiteral("previewTarget"), QStringLiteral("gpu-test-page"));
    changes.insert(QStringLiteral("previewMessage"),
                   QStringLiteral("实时 GPU 图形正在迅为开发板测试平台的 Qt GPU 测试页内渲染。"));
    // This is a child item in BoardLab's existing scene graph, not a separate
    // toplevel surface. Keep the legacy field false so API consumers cannot
    // mistake it for a compositor window.
    changes.insert(QStringLiteral("previewWindowExpected"), false);
    changes.insert(QStringLiteral("previewStatus"), QStringLiteral("running"));
    changes.insert(QStringLiteral("previewStartedAt"), nowIso());
    changes.insert(QStringLiteral("previewFinishedAt"), QString());
    changes.insert(QStringLiteral("previewElapsedMs"), 0);
    changes.insert(QStringLiteral("previewElapsedSeconds"), 0.0);
    changes.insert(QStringLiteral("previewScore"), -1);
    changes.insert(QStringLiteral("previewRenderer"), QStringLiteral("Qt Quick ShaderEffect"));
    changes.insert(QStringLiteral("previewExitCode"), -1);
    changes.insert(QStringLiteral("previewExitStatus"), QStringLiteral("running"));
    changes.insert(QStringLiteral("previewLog"),
                   QStringLiteral("Embedded Qt Quick ShaderEffect is active. Mali driver load is sampled in the GPU status chart."));
    changes.insert(QStringLiteral("previewLogTruncated"), false);
    changes.insert(QStringLiteral("previewError"), QString());
    updateStatus(changes);
}

void GpuBenchmark::appendLog(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return;
    }

    m_logBytes.append(bytes);
    bool truncated = m_status.value(QStringLiteral("logTruncated")).toBool();
    if (m_logBytes.size() > kMaximumLogBytes) {
        m_logBytes.remove(0, m_logBytes.size() - kMaximumLogBytes);
        // Avoid starting halfway through an UTF-8 continuation sequence.
        while (!m_logBytes.isEmpty()
               && (static_cast<unsigned char>(m_logBytes.at(0)) & 0xc0U) == 0x80U) {
            m_logBytes.remove(0, 1);
        }
        truncated = true;
    }

    m_parseBuffer.append(QString::fromLocal8Bit(bytes));
    if (m_parseBuffer.size() > kMaximumParserCharacters) {
        m_parseBuffer.remove(0, m_parseBuffer.size() - kMaximumParserCharacters);
    }

    QVariantMap changes;
    changes.insert(QStringLiteral("log"), QString::fromLocal8Bit(m_logBytes));
    changes.insert(QStringLiteral("logTruncated"), truncated);
    updateStatus(changes);
    refreshParsedOutput();
}

void GpuBenchmark::refreshParsedOutput()
{
    QVariantMap changes;
    const int score = parseScore(m_parseBuffer);
    if (score >= 0 && score != m_status.value(QStringLiteral("score")).toInt()) {
        changes.insert(QStringLiteral("score"), score);
    }
    const QString renderer = parseRenderer(m_parseBuffer);
    if (!renderer.isEmpty() && renderer != m_status.value(QStringLiteral("renderer")).toString()) {
        changes.insert(QStringLiteral("renderer"), renderer);
    }
    if (!changes.isEmpty()) {
        updateStatus(changes);
    }
}

void GpuBenchmark::publishElapsed()
{
    if (!running() || !m_elapsedClock.isValid()) {
        return;
    }

    const qint64 elapsedMs = m_elapsedClock.elapsed();
    updateStatus(QVariantMap{
        {QStringLiteral("elapsedMs"), elapsedMs},
        {QStringLiteral("elapsedSeconds"), static_cast<double>(elapsedMs) / 1000.0}
    });
}

void GpuBenchmark::publishPreviewElapsed()
{
    if (!previewRunning() || !m_previewElapsedClock.isValid()) {
        return;
    }

    const qint64 elapsedMs = m_previewElapsedClock.elapsed();
    updateStatus(QVariantMap{
        {QStringLiteral("previewElapsedMs"), elapsedMs},
        {QStringLiteral("previewElapsedSeconds"), static_cast<double>(elapsedMs) / 1000.0}
    });
}

void GpuBenchmark::updateStatus(const QVariantMap &changes)
{
    QVariantMap next = m_status;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        next.insert(it.key(), it.value());
    }
    if (next == m_status) {
        return;
    }

    const bool didRunningChange = next.value(QStringLiteral("running")).toBool()
        != m_status.value(QStringLiteral("running")).toBool();
    const bool didPreviewRunningChange = next.value(QStringLiteral("previewRunning")).toBool()
        != m_status.value(QStringLiteral("previewRunning")).toBool();
    const bool didLogChange = next.value(QStringLiteral("log")).toString()
        != m_status.value(QStringLiteral("log")).toString();
    m_status = next;
    if (didRunningChange) {
        emit runningChanged();
    }
    if (didPreviewRunningChange) {
        emit previewRunningChanged();
    }
    if (didLogChange) {
        emit logChanged();
    }
    emit statusChanged();
}

void GpuBenchmark::finishRun(bool success, const QString &nextStatus, const QString &error,
                             int exitCode, QProcess::ExitStatus exitStatus)
{
    m_elapsedTimer->stop();
    m_stopGraceTimer->stop();
    publishElapsed();

    QVariantMap changes;
    changes.insert(QStringLiteral("running"), false);
    changes.insert(QStringLiteral("status"), nextStatus);
    changes.insert(QStringLiteral("finishedAt"), nowIso());
    changes.insert(QStringLiteral("exitCode"), exitCode);
    changes.insert(QStringLiteral("exitStatus"), exitStatus == QProcess::NormalExit
                   ? QStringLiteral("normal")
                   : QStringLiteral("crashed"));
    changes.insert(QStringLiteral("error"), error);
    updateStatus(changes);

    m_stopRequested = false;
    emit finished(success, status());
}

void GpuBenchmark::finishPreview(bool success, const QString &nextStatus, const QString &error,
                                 int exitCode, QProcess::ExitStatus exitStatus)
{
    m_previewElapsedTimer->stop();
    publishPreviewElapsed();

    QVariantMap changes;
    changes.insert(QStringLiteral("previewRunning"), false);
    changes.insert(QStringLiteral("previewWindowExpected"), false);
    changes.insert(QStringLiteral("previewStatus"), nextStatus);
    changes.insert(QStringLiteral("previewFinishedAt"), nowIso());
    changes.insert(QStringLiteral("previewExitCode"), exitCode);
    changes.insert(QStringLiteral("previewExitStatus"), exitStatus == QProcess::NormalExit
                   ? QStringLiteral("normal")
                   : QStringLiteral("crashed"));
    changes.insert(QStringLiteral("previewError"), error);
    updateStatus(changes);

    emit previewFinished(success, status());
}
