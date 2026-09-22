#include "bootstrapcontroller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTcpSocket>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace {
constexpr auto kBeginMarkerPrefix = "# BEGIN CODEX-REMOTE-BOOTSTRAP: ";
constexpr auto kEndMarkerPrefix   = "# END CODEX-REMOTE-BOOTSTRAP: ";
constexpr auto kProxyBegin        = "# BEGIN CODEX-REMOTE-BOOTSTRAP-PROXY";
constexpr auto kProxyEnd          = "# END CODEX-REMOTE-BOOTSTRAP-PROXY";
constexpr auto kGithubApiBase     = "https://api.github.com/repos/openai/codex";

QString cleanOutput(const QByteArray &data)
{
    return QString::fromUtf8(data).trimmed();
}

bool isPatternHost(const QString &value)
{
    return value.startsWith(QLatin1Char('!'))
        || value.contains(QLatin1Char('*'))
        || value.contains(QLatin1Char('?'))
        || value.contains(QLatin1Char('['));
}

QStringList commonSshArgs(bool passwordAuth)
{
    QStringList args;
    args << QStringLiteral("-T")
         << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=10")
         << QStringLiteral("-o") << QStringLiteral("ConnectionAttempts=1")
         << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new")
         << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=15")
         << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=2");

    if (passwordAuth) {
        args << QStringLiteral("-o") << QStringLiteral("PubkeyAuthentication=no")
             << QStringLiteral("-o") << QStringLiteral("PasswordAuthentication=yes")
             << QStringLiteral("-o") << QStringLiteral("KbdInteractiveAuthentication=yes")
             << QStringLiteral("-o") << QStringLiteral("PreferredAuthentications=keyboard-interactive,password")
             << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=2");
    } else {
        args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
             << QStringLiteral("-o") << QStringLiteral("PasswordAuthentication=no")
             << QStringLiteral("-o") << QStringLiteral("KbdInteractiveAuthentication=no")
             << QStringLiteral("-o") << QStringLiteral("PreferredAuthentications=publickey")
             << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=0");
    }
    return args;
}

QString sanitizeSshOutput(const QString &text)
{
    QStringList kept;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.contains(QStringLiteral("Permanently added"), Qt::CaseInsensitive)
            && trimmed.contains(QStringLiteral("known hosts"), Qt::CaseInsensitive)) {
            continue;
        }
        kept.push_back(trimmed);
    }
    return kept.join(QLatin1Char('\n'));
}

QString describeProxy(const QNetworkProxy &proxy)
{
    if (proxy.type() == QNetworkProxy::NoProxy)
        return QStringLiteral("直连");
    if (proxy.type() == QNetworkProxy::DefaultProxy)
        return QStringLiteral("系统默认");
    const QString kind = proxy.type() == QNetworkProxy::Socks5Proxy
            ? QStringLiteral("socks5")
            : QStringLiteral("http");
    if (proxy.hostName().isEmpty())
        return kind;
    return QStringLiteral("%1://%2:%3").arg(kind, proxy.hostName()).arg(proxy.port());
}

QNetworkProxy proxyFromSpec(const QString &value)
{
    QString spec = value.trimmed();
    if (spec.isEmpty())
        return {QNetworkProxy::NoProxy};

    if (!spec.contains(QStringLiteral("://")))
        spec.prepend(QStringLiteral("http://"));

    const QUrl url(spec);
    if (!url.isValid() || url.host().isEmpty())
        return {QNetworkProxy::NoProxy};

    QNetworkProxy proxy;
    const QString scheme = url.scheme().toLower();
    proxy.setType((scheme == QLatin1String("socks5") || scheme == QLatin1String("socks5h"))
                      ? QNetworkProxy::Socks5Proxy
                      : QNetworkProxy::HttpProxy);
    proxy.setHostName(url.host());
    proxy.setPort(static_cast<quint16>(url.port(scheme.startsWith(QLatin1String("socks")) ? 1080 : 8080)));
    if (!url.userName().isEmpty())
        proxy.setUser(url.userName());
    if (!url.password().isEmpty())
        proxy.setPassword(url.password());
    return proxy;
}

bool proxyPortOpen(const QString &host, quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(host, port);
    const bool ok = socket.waitForConnected(250);
    socket.abort();
    return ok;
}

QNetworkProxy envHttpProxy()
{
    static const char *const keys[] = {
        "HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"
    };
    for (const char *key : keys) {
        const QNetworkProxy proxy = proxyFromSpec(qEnvironmentVariable(key));
        if (proxy.type() != QNetworkProxy::NoProxy)
            return proxy;
    }
    return {QNetworkProxy::NoProxy};
}

QNetworkProxy windowsSystemProxy()
{
    const QNetworkProxyQuery query(QUrl(QStringLiteral("https://api.github.com")));
    const QList<QNetworkProxy> list = QNetworkProxyFactory::systemProxyForQuery(query);
    for (const QNetworkProxy &proxy : list) {
        if (proxy.type() != QNetworkProxy::NoProxy && proxy.type() != QNetworkProxy::DefaultProxy)
            return proxy;
    }
    return {QNetworkProxy::NoProxy};
}

QNetworkProxy probeLocalProxies()
{
    struct Candidate {
        QNetworkProxy::ProxyType type;
        quint16 port;
    };
    const Candidate candidates[] = {
        {QNetworkProxy::HttpProxy, 7890},
        {QNetworkProxy::HttpProxy, 7897},
        {QNetworkProxy::HttpProxy, 10809},
        {QNetworkProxy::HttpProxy, 10808},
        {QNetworkProxy::HttpProxy, 20171},
        {QNetworkProxy::Socks5Proxy, 7891},
        {QNetworkProxy::Socks5Proxy, 1080},
        {QNetworkProxy::Socks5Proxy, 10808},
    };
    for (const Candidate &c : candidates) {
        if (!proxyPortOpen(QStringLiteral("127.0.0.1"), c.port))
            continue;
        QNetworkProxy proxy(c.type, QStringLiteral("127.0.0.1"), c.port);
        return proxy;
    }
    return {QNetworkProxy::NoProxy};
}

QNetworkProxy &cachedGithubProxy()
{
    static QNetworkProxy proxy(QNetworkProxy::DefaultProxy);
    return proxy;
}

QNetworkProxy resolveGithubProxy()
{
    QNetworkProxy &cached = cachedGithubProxy();
    if (cached.type() != QNetworkProxy::DefaultProxy)
        return cached;

    QNetworkProxy proxy = envHttpProxy();
    if (proxy.type() == QNetworkProxy::NoProxy)
        proxy = probeLocalProxies();
    if (proxy.type() == QNetworkProxy::NoProxy)
        proxy = windowsSystemProxy();

    cached = proxy;
    return cached;
}

void applyGithubProxy(QNetworkAccessManager *manager)
{
    const QNetworkProxy proxy = resolveGithubProxy();
    if (proxy.type() == QNetworkProxy::NoProxy || proxy.type() == QNetworkProxy::DefaultProxy)
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    else
        manager->setProxy(proxy);
}

QString &cachedCurlProxy()
{
    static QString proxy = QStringLiteral("__unset__");
    return proxy;
}

QString curlProxyLabel(const QString &proxyUrl)
{
    return proxyUrl.isEmpty() ? QStringLiteral("系统/直连") : proxyUrl;
}
} // namespace

struct BootstrapController::Options
{
    QString host;
    int port = 22;
    QString user;
    QString password;
    QString alias;
    QString identityPath;
    QString packageDir;
    QString packageVersion = QStringLiteral("latest-stable");
    bool autoDownloadPackage = true;
    bool installCodex = true;
    bool syncAuthState = false;
    bool proxyEnabled = false;
    QString proxyHost = QStringLiteral("127.0.0.1");
    int proxyPort = 7890;
    int remoteProxyPort = 17890;
    bool useSshConfigHost = false;
    bool manageSshConfig = true;
};

struct BootstrapController::ProcessResult
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;

    bool ok() const { return started && !timedOut && exitCode == 0; }

    QString errorText() const
    {
        const QString err = sanitizeSshOutput(cleanOutput(stdErr));
        const QString out = sanitizeSshOutput(cleanOutput(stdOut));
        if (timedOut)
            return QStringLiteral("操作超时");
        if (!err.isEmpty())
            return err;
        if (!out.isEmpty())
            return out;
        return QStringLiteral("进程退出码 %1").arg(exitCode);
    }
};

struct BootstrapController::ReleaseAsset
{
    QString name;
    QString url;
    QString digest;
    qint64 size = 0;
};

struct BootstrapController::ReleaseInfo
{
    QString tag;
    QString name;
    QString publishedAt;
    bool prerelease = false;
    QList<ReleaseAsset> assets;
};

struct BootstrapController::HttpResult
{
    bool ok = false;
    int status = 0;
    QByteArray data;
    QString error;
};

BootstrapController::BootstrapController(QObject *parent)
    : QObject(parent)
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QDir().mkpath(defaultPackageDir());
    refreshSshHosts();
    refreshLocalPackages();
}

BootstrapController::~BootstrapController()
{
    if (m_future.isRunning())
        m_future.waitForFinished();
    if (m_packageFuture.isRunning())
        m_packageFuture.waitForFinished();
}

QString BootstrapController::defaultIdentityPath() const
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".ssh/codex_remote_ed25519"));
}

QString BootstrapController::defaultPackageDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("packages"));
}

QString BootstrapController::sshConfigPath() const
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".ssh/config"));
}

QString BootstrapController::suggestedAlias(const QString &host) const
{
    QString value = host.trimmed();
    value.replace('.', '-');
    value.replace(':', '-');
    value = safeAlias(value);
    if (value.isEmpty())
        value = QStringLiteral("remote");
    return QStringLiteral("codex-%1").arg(value);
}

QString BootstrapController::localPathFromUrl(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

void BootstrapController::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void BootstrapController::setPackageRunning(bool running)
{
    if (m_packageRunning == running)
        return;
    m_packageRunning = running;
    emit packageRunningChanged();
}

void BootstrapController::postLog(const QString &line)
{
    QMetaObject::invokeMethod(this, [this, line] { emit logLine(line); }, Qt::QueuedConnection);
}

void BootstrapController::postStep(const QString &step, int percent)
{
    QMetaObject::invokeMethod(this, [this, step, percent] { emit stepChanged(step, percent); }, Qt::QueuedConnection);
}

void BootstrapController::finish(bool ok, const QString &message)
{
    QMetaObject::invokeMethod(this, [this, ok, message] {
        setRunning(false);
        if (ok)
            refreshSshHosts();
        emit completed(ok, message);
    }, Qt::QueuedConnection);
}

void BootstrapController::finishPackage(bool ok, const QString &message)
{
    QMetaObject::invokeMethod(this, [this, ok, message] {
        setPackageRunning(false);
        emit packageCompleted(ok, message);
    }, Qt::QueuedConnection);
}

bool BootstrapController::parseOptions(const QVariantMap &map, Options *out, QString *error) const
{
    Options o;
    o.host = map.value(QStringLiteral("host")).toString().trimmed();
    o.port = map.value(QStringLiteral("port"), 22).toInt();
    o.user = map.value(QStringLiteral("user")).toString().trimmed();
    o.password = map.value(QStringLiteral("password")).toString();
    o.alias = safeAlias(map.value(QStringLiteral("alias")).toString().trimmed());
    o.identityPath = QDir::fromNativeSeparators(
        map.value(QStringLiteral("identityPath"), defaultIdentityPath()).toString().trimmed());
    o.packageDir = QDir::fromNativeSeparators(
        map.value(QStringLiteral("packageDir"), defaultPackageDir()).toString().trimmed());
    o.packageVersion = map.value(QStringLiteral("packageVersion"), QStringLiteral("latest-stable")).toString().trimmed();
    if (o.packageVersion.isEmpty())
        o.packageVersion = QStringLiteral("latest-stable");
    o.autoDownloadPackage = map.value(QStringLiteral("autoDownloadPackage"), true).toBool();
    o.installCodex = map.value(QStringLiteral("installCodex"), true).toBool();
    o.syncAuthState = map.value(QStringLiteral("syncAuthState"), false).toBool();
    o.proxyEnabled = map.value(QStringLiteral("proxyEnabled"), false).toBool();
    o.proxyHost = map.value(QStringLiteral("proxyHost"), QStringLiteral("127.0.0.1")).toString().trimmed();
    o.proxyPort = map.value(QStringLiteral("proxyPort"), 7890).toInt();
    o.remoteProxyPort = map.value(QStringLiteral("remoteProxyPort"), 17890).toInt();
    o.useSshConfigHost = map.value(QStringLiteral("useSshConfigHost"), false).toBool();
    o.manageSshConfig = map.value(QStringLiteral("manageSshConfig"), !o.useSshConfigHost).toBool();

    if (o.host.isEmpty()) {
        *error = QStringLiteral("请输入远端 IP/主机名");
        return false;
    }
    if (o.user.isEmpty()) {
        *error = QStringLiteral("请输入 SSH 用户名");
        return false;
    }
    if (o.alias.isEmpty()) {
        *error = QStringLiteral("请输入合法的 SSH 别名");
        return false;
    }
    if (o.port < 1 || o.port > 65535) {
        *error = QStringLiteral("SSH 端口范围应为 1~65535");
        return false;
    }
    if (o.identityPath.isEmpty())
        o.identityPath = defaultIdentityPath();
    if (o.proxyEnabled && o.useSshConfigHost && !o.manageSshConfig) {
        *error = QStringLiteral("从现有 SSH config 选择设备时不会改写该 Host；如需持久代理转发，请使用“手动新增设备”创建托管 Host");
        return false;
    }
    if (o.proxyEnabled && (o.proxyHost.isEmpty() || o.proxyPort < 1 || o.proxyPort > 65535
                           || o.remoteProxyPort < 1 || o.remoteProxyPort > 65535)) {
        *error = QStringLiteral("代理参数不合法");
        return false;
    }

    *out = o;
    return true;
}

void BootstrapController::testConnection(const QVariantMap &options)
{
    if (m_running)
        return;

    Options o;
    QString error;
    if (!parseOptions(options, &o, &error)) {
        emit completed(false, error);
        return;
    }

    setRunning(true);
    emit stepChanged(QStringLiteral("测试 SSH 连接"), 5);
    m_future = QtConcurrent::run([this, o] { runTest(o); });
}

void BootstrapController::configure(const QVariantMap &options)
{
    if (m_running)
        return;

    Options o;
    QString error;
    if (!parseOptions(options, &o, &error)) {
        emit completed(false, error);
        return;
    }

    setRunning(true);
    emit stepChanged(QStringLiteral("准备配置"), 1);
    m_future = QtConcurrent::run([this, o] { runConfigure(o); });
}

void BootstrapController::runTest(Options o)
{
    QString error;
    if (!ensureLocalTools(&error)) {
        finish(false, error);
        return;
    }

    postLog(QStringLiteral("[SSH] 测试 %1 (%2@%3:%4)").arg(o.alias, o.user, o.host).arg(o.port));
    const QString probe = QStringLiteral("printf 'OS='; uname -s; printf 'ARCH='; uname -m; printf 'HOME='; printf '%s\\n' \"$HOME\"");

    auto r = runSsh(o, probe, false, 25000);
    if (r.ok()) {
        postLog(QStringLiteral("[SSH] 已通过密钥/现有 SSH 配置登录"));
        postLog(cleanOutput(r.stdOut));
        postStep(QStringLiteral("SSH 连接正常"), 100);
        finish(true, QStringLiteral("SSH 连接正常（免密/现有配置可用）"));
        return;
    }

    if (o.password.isEmpty()) {
        finish(false, QStringLiteral("免密登录失败，且未填写 SSH 密码：%1").arg(r.errorText()));
        return;
    }

    postLog(QStringLiteral("[SSH] 免密不可用，尝试密码登录"));
    r = runSsh(o, probe, true, 25000);
    if (!r.ok()) {
        finish(false, QStringLiteral("SSH 密码登录失败：%1").arg(r.errorText()));
        return;
    }

    postLog(cleanOutput(r.stdOut));
    postStep(QStringLiteral("SSH 连接正常"), 100);
    finish(true, QStringLiteral("SSH 密码登录正常，可以开始配置免密"));
}

void BootstrapController::runConfigure(Options o)
{
    QString error;
    if (!ensureLocalTools(&error)) {
        finish(false, error);
        return;
    }

    postStep(QStringLiteral("检查免密登录"), 10);
    if (!verifyKeyLogin(o, nullptr)) {
        if (o.password.isEmpty()) {
            finish(false, QStringLiteral("当前 Host 尚不能免密登录，请填写 SSH 密码后重试"));
            return;
        }

        postStep(QStringLiteral("准备 SSH 密钥"), 16);
        QString publicKey;
        if (!ensureKeyPair(&o, &publicKey, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[KEY] 身份文件：%1").arg(QDir::toNativeSeparators(o.identityPath)));
        postLog(QStringLiteral("[KEY] 正在通过密码写入远端 authorized_keys"));
        if (!installPublicKey(o, publicKey, &error)) {
            finish(false, error);
            return;
        }
        if (!verifyKeyLogin(o, &error)) {
            QThread::msleep(600);
        }
        if (!verifyKeyLogin(o, &error)) {
            logRemoteKeyDiagnostics(o, publicKey);
            const QString fallback = defaultIdentityPath();
            if (QDir::cleanPath(o.identityPath)
                    .compare(QDir::cleanPath(fallback), Qt::CaseInsensitive) != 0) {
                postLog(QStringLiteral("[KEY] 当前密钥登录被拒绝，改用工具专用 ED25519 密钥重试"));
                o.identityPath = fallback;
                QString fallbackKey;
                if (ensureKeyPair(&o, &fallbackKey, &error)
                    && installPublicKey(o, fallbackKey, &error)
                    && verifyKeyLogin(o, &error)) {
                    postLog(QStringLiteral("[KEY] 已改用 %1")
                                .arg(QDir::toNativeSeparators(o.identityPath)));
                    if (o.useSshConfigHost) {
                        postLog(QStringLiteral("[KEY] 该 Host 来自 ~/.ssh/config，请把 IdentityFile 改为上述路径，否则 Codex 仍可能用旧密钥"));
                    }
                } else {
                    finish(false, error);
                    return;
                }
            } else {
                finish(false, error);
                return;
            }
        }
    }
    postLog(QStringLiteral("[KEY] 免密登录验证通过"));

    postStep(QStringLiteral("检测远端系统"), 27);
    QString osName;
    QString arch;
    if (!detectRemote(o, &osName, &arch, &error)) {
        finish(false, error);
        return;
    }
    postLog(QStringLiteral("[REMOTE] OS=%1, ARCH=%2").arg(osName, arch));
    if (osName.compare(QStringLiteral("Linux"), Qt::CaseInsensitive) != 0) {
        finish(false, QStringLiteral("当前仅支持 Linux 远端，检测到：%1").arg(osName));
        return;
    }

    const QString normalizedArch = normalizeArch(arch);
    if (normalizedArch.isEmpty()) {
        finish(false, QStringLiteral("暂不支持架构 %1；当前支持 x86_64/amd64 与 aarch64/arm64").arg(arch));
        return;
    }

    if (o.manageSshConfig) {
        postStep(QStringLiteral("写入 SSH 配置"), 36);
        if (!updateSshConfig(o, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[SSH] Host %1 -> %2@%3:%4").arg(o.alias, o.user, o.host).arg(o.port));
    } else {
        postLog(QStringLiteral("[SSH] 使用现有 ~/.ssh/config Host：%1（不改写）").arg(o.alias));
    }

    if (o.proxyEnabled) {
        postStep(QStringLiteral("配置远端代理环境"), 44);
        if (!configureRemoteProxyEnvironment(o, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[PROXY] 远端 127.0.0.1:%1 -> Windows %2:%3")
                    .arg(o.remoteProxyPort).arg(o.proxyHost).arg(o.proxyPort));
    }

    QString installedVersion;
    if (o.installCodex) {
        postStep(QStringLiteral("准备 Codex 安装包"), 52);
        QString packagePath;
        QString resolvedTag;
        if (!ensureCodexPackage(o, normalizedArch, &packagePath, &resolvedTag, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[PKG] 版本：%1").arg(resolvedTag));
        postLog(QStringLiteral("[PKG] 使用：%1").arg(QDir::toNativeSeparators(packagePath)));

        postStep(QStringLiteral("上传并安装 Codex CLI"), 68);
        QString installedPath;
        if (!uploadAndInstallCodex(o, packagePath, &installedPath, &installedVersion, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[CODEX] 安装路径：%1").arg(installedPath));
        postLog(QStringLiteral("[CODEX] %1").arg(installedVersion));
    } else {
        postStep(QStringLiteral("验证现有 Codex CLI"), 70);
        if (!verifyCodex(o, &installedVersion, &error)) {
            finish(false, QStringLiteral("未安装 Codex CLI，且已关闭安装选项：%1").arg(error));
            return;
        }
    }

    if (o.syncAuthState) {
        postStep(QStringLiteral("同步 Codex 登录态"), 84);
        if (!syncAuth(o, &error)) {
            finish(false, error);
            return;
        }
        postLog(QStringLiteral("[AUTH] 已同步 ~/.codex/auth.json"));
    }

    postStep(QStringLiteral("最终验证"), 94);
    QString finalVersion;
    if (!verifyCodex(o, &finalVersion, &error)) {
        finish(false, error);
        return;
    }
    postLog(QStringLiteral("[VERIFY] %1").arg(finalVersion));

    postStep(QStringLiteral("配置完成"), 100);
    finish(true, QStringLiteral("配置完成：Codex 中可使用 SSH 主机 “%1”").arg(o.alias));
}

BootstrapController::ProcessResult BootstrapController::runProcess(const QString &program,
                                                                   const QStringList &arguments,
                                                                   int timeoutMs,
                                                                   const QByteArray &stdinData,
                                                                   bool useAskPass,
                                                                   const QString &password) const
{
    ProcessResult result;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);

    if (useAskPass) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("SSH_ASKPASS"), QCoreApplication::applicationFilePath());
        env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        env.insert(QStringLiteral("DISPLAY"), QStringLiteral("codex-remote-bootstrap:0"));
        env.insert(QStringLiteral("CRB_ASKPASS_MODE"), QStringLiteral("1"));
        env.insert(QStringLiteral("CRB_SSH_PASSWORD"), password);
        process.setProcessEnvironment(env);
    }

    process.start(program, arguments, QIODevice::ReadWrite);
    result.started = process.waitForStarted(10000);
    if (!result.started) {
        result.stdErr = process.errorString().toUtf8();
        return result;
    }

    if (!stdinData.isEmpty()) {
        process.write(stdinData);
        process.waitForBytesWritten(5000);
        process.closeWriteChannel();
    } else if (useAskPass) {
        process.closeWriteChannel();
    }

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }

    result.exitCode = process.exitCode();
    result.stdOut = process.readAllStandardOutput();
    result.stdErr = process.readAllStandardError();
    return result;
}

BootstrapController::ProcessResult BootstrapController::runSsh(const Options &o,
                                                               const QString &remoteCommand,
                                                               bool passwordAuth,
                                                               int timeoutMs,
                                                               const QByteArray &stdinData) const
{
    const QString ssh = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    QStringList args = commonSshArgs(passwordAuth);

    if (o.useSshConfigHost) {
        if (o.proxyEnabled) {
            args << QStringLiteral("-R")
                 << QStringLiteral("127.0.0.1:%1:%2:%3")
                        .arg(o.remoteProxyPort).arg(o.proxyHost).arg(o.proxyPort);
        }
        if (!passwordAuth)
            appendIdentityArgs(&args, o);
        args << o.alias;
    } else {
        args << QStringLiteral("-p") << QString::number(o.port);
        if (!passwordAuth)
            appendIdentityArgs(&args, o);
        if (o.proxyEnabled) {
            args << QStringLiteral("-R")
                 << QStringLiteral("127.0.0.1:%1:%2:%3")
                        .arg(o.remoteProxyPort).arg(o.proxyHost).arg(o.proxyPort);
        }
        args << QStringLiteral("%1@%2").arg(o.user, o.host);
    }

    args << remoteCommand;
    return runProcess(ssh, args, timeoutMs, stdinData, passwordAuth, o.password);
}

BootstrapController::ProcessResult BootstrapController::runRemoteScript(const Options &o,
                                                                        const QString &script,
                                                                        bool passwordAuth,
                                                                        int timeoutMs) const
{
    // Send the script on the SSH channel. Windows QProcess quoting destroys
    // nested "$HOME" / "$(...)" when they sit in the ssh command line.
    QByteArray payload = script.toUtf8();
    if (!payload.endsWith('\n'))
        payload += '\n';
    return runSsh(o, QStringLiteral("sh -s"), passwordAuth, timeoutMs, payload);
}

bool BootstrapController::ensureLocalTools(QString *error) const
{
    const QStringList tools = {QStringLiteral("ssh"), QStringLiteral("scp"), QStringLiteral("ssh-keygen")};
    for (const QString &tool : tools) {
        if (QStandardPaths::findExecutable(tool).isEmpty()) {
            if (error) {
                *error = QStringLiteral("未找到 %1；请先安装/启用 Windows OpenSSH Client，并确保它在 PATH 中")
                             .arg(tool);
            }
            return false;
        }
    }
    return true;
}

void BootstrapController::appendIdentityArgs(QStringList *args, const Options &o) const
{
    if (!args || o.identityPath.isEmpty() || !QFileInfo::exists(o.identityPath))
        return;
    *args << QStringLiteral("-o") << QStringLiteral("IdentitiesOnly=yes")
          << QStringLiteral("-i") << o.identityPath;
}

bool BootstrapController::identityIsUsable(const QString &identityPath, QString *reason) const
{
    const QString keygen = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    const auto derived = runProcess(keygen,
                                    {QStringLiteral("-y"), QStringLiteral("-P"), QString(),
                                     QStringLiteral("-f"), identityPath},
                                    15000);
    if (!derived.ok()) {
        if (reason) {
            *reason = QStringLiteral("私钥 %1 无法免密使用（可能设置了 passphrase）")
                          .arg(QDir::toNativeSeparators(identityPath));
        }
        return false;
    }

    const auto listed = runProcess(keygen, {QStringLiteral("-l"), QStringLiteral("-f"), identityPath}, 15000);
    if (listed.ok()) {
        const QString text = cleanOutput(listed.stdOut);
        const int bits = text.section(QLatin1Char(' '), 0, 0).toInt();
        const bool rsa = text.contains(QStringLiteral("(RSA)"), Qt::CaseInsensitive);
        const bool dsa = text.contains(QStringLiteral("(DSA)"), Qt::CaseInsensitive);
        if (dsa) {
            if (reason)
                *reason = QStringLiteral("DSA 密钥已被现代 SSH 服务器拒绝");
            return false;
        }
        if (rsa && bits > 0 && bits < 2048) {
            if (reason) {
                *reason = QStringLiteral("RSA 密钥仅 %1 位，OpenSSH 9.6+ 会拒绝（要求 ≥2048）")
                              .arg(bits);
            }
            return false;
        }
    }
    return true;
}

bool BootstrapController::ensureKeyPair(Options *o, QString *publicKey, QString *error)
{
    QString identityPath = o->identityPath;
    if (identityPath.isEmpty())
        identityPath = defaultIdentityPath();

    QString reason;
    if (QFileInfo::exists(identityPath) && !identityIsUsable(identityPath, &reason)) {
        const QString fallback = defaultIdentityPath();
        if (QDir::cleanPath(identityPath).compare(QDir::cleanPath(fallback), Qt::CaseInsensitive) != 0) {
            postLog(QStringLiteral("[KEY] %1，改用 %2")
                        .arg(reason, QDir::toNativeSeparators(fallback)));
            identityPath = fallback;
        } else {
            *error = reason;
            return false;
        }
    }

    QDir().mkpath(QFileInfo(identityPath).absolutePath());
    const QString pubPath = identityPath + QStringLiteral(".pub");
    const QString keygen = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));

    if (!QFileInfo::exists(identityPath)) {
        postLog(QStringLiteral("[KEY] 正在生成 ED25519 密钥"));
        const auto r = runProcess(keygen,
                                  {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                                   QStringLiteral("-N"), QString(), QStringLiteral("-C"),
                                   QStringLiteral("codex-remote-bootstrap"), QStringLiteral("-f"), identityPath},
                                  30000);
        if (!r.ok()) {
            *error = QStringLiteral("生成 SSH 密钥失败：%1").arg(r.errorText());
            return false;
        }
    }

    const auto derived = runProcess(keygen,
                                    {QStringLiteral("-y"), QStringLiteral("-P"), QString(),
                                     QStringLiteral("-f"), identityPath},
                                    15000);
    if (!derived.ok()) {
        *error = QStringLiteral("无法从私钥生成公钥：%1").arg(derived.errorText());
        return false;
    }

    QByteArray content = derived.stdOut.trimmed();
    if (content.isEmpty()) {
        *error = QStringLiteral("从私钥导出的公钥为空：%1").arg(identityPath);
        return false;
    }
    if (!content.endsWith('\n'))
        content += '\n';

    QSaveFile pub(pubPath);
    if (!pub.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("无法写入公钥文件：%1").arg(pub.errorString());
        return false;
    }
    pub.write(content);
    if (!pub.commit()) {
        *error = QStringLiteral("保存公钥失败：%1").arg(pub.errorString());
        return false;
    }

    *publicKey = QString::fromUtf8(content).trimmed();
    if (publicKey->isEmpty()) {
        *error = QStringLiteral("公钥文件为空：%1").arg(pubPath);
        return false;
    }

    const auto fingerprint = runProcess(keygen, {QStringLiteral("-l"), QStringLiteral("-f"), pubPath}, 10000);
    if (fingerprint.ok())
        postLog(QStringLiteral("[KEY] 指纹：%1").arg(cleanOutput(fingerprint.stdOut)));

    o->identityPath = identityPath;
    return true;
}

bool BootstrapController::installPublicKey(const Options &o, const QString &publicKey, QString *error)
{
    // The script travels on SSH stdin (`sh -s`). Putting $HOME / quotes / the
    // public key on the Windows ssh command line is what left authorized_keys
    // at 3 bytes on Jetson.
    QString script;
    script += QStringLiteral("umask 077\n");
    script += QStringLiteral("uid=`id -u`\n");
    script += QStringLiteral("gid=`id -g`\n");
    script += QStringLiteral("home=$HOME\n");
    script += QStringLiteral("if [ -z \"$home\" ]; then home=`awk -F: -v u=\"$uid\" '$3==u {print $6; exit}' /etc/passwd`; fi\n");
    script += QStringLiteral("if [ -z \"$home\" ]; then home=/root; fi\n");
    script += QStringLiteral("key=");
    script += shellQuote(publicKey);
    script += QLatin1Char('\n');
    script += QStringLiteral(
        "if [ -z \"$key\" ]; then echo 'CRB: empty public key' >&2; exit 2; fi\n"
        "owner=`ls -ld \"$home\" 2>/dev/null | awk '{print $3\":\"$4}'`\n"
        "echo \"CRB_HOME=$home uid=$uid owner=$owner\"\n"
        "# sshd StrictModes rejects keys when $HOME is owned by someone else\n"
        "# (Jetson image often has /root owned by jetson).\n"
        "if [ \"$uid\" -eq 0 ] || [ -O \"$home\" ]; then\n"
        "  chmod go-w \"$home\" 2>/dev/null || true\n"
        "  chown \"$uid:$gid\" \"$home\" 2>/dev/null || true\n"
        "fi\n"
        "mkdir -p \"$home/.ssh\" || exit 2\n"
        "chmod 700 \"$home/.ssh\"\n"
        "chown \"$uid:$gid\" \"$home/.ssh\" 2>/dev/null || true\n"
        "ak=\"$home/.ssh/authorized_keys\"\n"
        "touch \"$ak\"\n"
        "if [ -s \"$ak\" ]; then\n"
        "  grep -E '^(ssh-rsa|ssh-ed25519|ecdsa-sha2-|sk-ssh-|sk-ecdsa-)' \"$ak\" > \"$ak.crb\" || :\n"
        "  mv \"$ak.crb\" \"$ak\"\n"
        "fi\n"
        "chmod 600 \"$ak\"\n"
        "chown \"$uid:$gid\" \"$ak\" 2>/dev/null || true\n"
        "grep -qxF \"$key\" \"$ak\" || printf '%s\\n' \"$key\" >> \"$ak\"\n"
        "if [ -d /etc/dropbear ]; then\n"
        "  touch /etc/dropbear/authorized_keys\n"
        "  chmod 600 /etc/dropbear/authorized_keys\n"
        "  grep -qxF \"$key\" /etc/dropbear/authorized_keys || printf '%s\\n' \"$key\" >> /etc/dropbear/authorized_keys\n"
        "fi\n"
        "if command -v restorecon >/dev/null 2>&1; then restorecon -RF \"$home/.ssh\" 2>/dev/null || true; fi\n"
        "if ! grep -qxF \"$key\" \"$ak\"; then echo 'CRB: key missing after install' >&2; ls -ld \"$home\" \"$home/.ssh\" \"$ak\" >&2; exit 3; fi\n"
        "bytes=`wc -c < \"$ak\" | tr -d ' '`\n"
        "if [ \"$bytes\" -lt 40 ]; then echo \"CRB: authorized_keys too small ($bytes)\" >&2; exit 4; fi\n"
        "newowner=`ls -ld \"$home\" | awk '{print $3\":\"$4}'`\n"
        "akowner=`ls -l \"$ak\" | awk '{print $3\":\"$4}'`\n"
        "echo CRB_OK bytes=$bytes home_owner=$newowner ak_owner=$akowner\n"
        "if command -v sshd >/dev/null 2>&1; then\n"
        "  sshd -T 2>/dev/null | awk 'tolower($1)==\"pubkeyauthentication\"||tolower($1)==\"authorizedkeysfile\"||tolower($1)==\"strictmodes\"||tolower($1)==\"permitrootlogin\" {print \"CRB_SSHD \"$0}'\n"
        "fi\n");

    const auto r = runRemoteScript(o, script, true, 30000);
    const QString out = cleanOutput(r.stdOut);
    if (!out.isEmpty()) {
        QString line = out;
        line.replace(QLatin1Char('\n'), QStringLiteral(" | "));
        postLog(QStringLiteral("[KEY] %1").arg(line));
    }
    if (!r.ok() || !out.contains(QStringLiteral("CRB_OK"))) {
        *error = QStringLiteral("安装 SSH 公钥失败：%1").arg(r.errorText());
        return false;
    }

    if (out.contains(QStringLiteral("pubkeyauthentication no"), Qt::CaseInsensitive)) {
        postLog(QStringLiteral("[KEY] sshd 关闭了公钥登录，正在打开 PubkeyAuthentication"));
        if (!ensureSshdPubkeyAuth(o, error))
            return false;
    }
    return true;
}

bool BootstrapController::ensureSshdPubkeyAuth(const Options &o, QString *error)
{
    const QString script = QStringLiteral(
        "set -e\n"
        "cfg=/etc/ssh/sshd_config\n"
        "if [ ! -f \"$cfg\" ]; then echo 'CRB: no sshd_config' >&2; exit 2; fi\n"
        "if grep -qiE '^[[:space:]]*PubkeyAuthentication[[:space:]]+yes' \"$cfg\"; then\n"
        "  echo CRB_SSHD_ALREADY_YES\n"
        "else\n"
        "  cp -a \"$cfg\" \"$cfg.crb.bak\" 2>/dev/null || true\n"
        "  if grep -qiE '^[[:space:]]*PubkeyAuthentication[[:space:]]+' \"$cfg\"; then\n"
        "    sed -i 's/^[[:space:]]*#\\?[[:space:]]*PubkeyAuthentication[[:space:]].*/PubkeyAuthentication yes/' \"$cfg\"\n"
        "  else\n"
        "    printf '\\n# Codex Remote Bootstrap\\nPubkeyAuthentication yes\\n' >> \"$cfg\"\n"
        "  fi\n"
        "fi\n"
        "if command -v sshd >/dev/null 2>&1; then sshd -t; fi\n"
        "if command -v systemctl >/dev/null 2>&1; then\n"
        "  systemctl reload ssh 2>/dev/null || systemctl reload sshd 2>/dev/null || true\n"
        "fi\n"
        "pid=`pidof sshd 2>/dev/null | awk '{print $1}'`\n"
        "if [ -n \"$pid\" ]; then kill -HUP \"$pid\" 2>/dev/null || true; fi\n"
        "echo CRB_SSHD_RELOADED\n");

    const auto r = runRemoteScript(o, script, true, 30000);
    if (!r.ok()) {
        *error = QStringLiteral("无法打开 sshd PubkeyAuthentication：%1").arg(r.errorText());
        return false;
    }
    postLog(QStringLiteral("[KEY] %1").arg(cleanOutput(r.stdOut)));
    QThread::msleep(800);
    return true;
}

void BootstrapController::logRemoteKeyDiagnostics(const Options &o, const QString &publicKey)
{
    if (o.password.isEmpty())
        return;

    const QStringList parts = publicKey.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString blobTail = parts.value(1).right(24);

    QString script;
    script += QStringLiteral("home=$HOME\n");
    script += QStringLiteral("echo HOME=$home UID=`id -u` USER=`id -un`\n");
    script += QStringLiteral("ls -ld \"$home\" \"$home/.ssh\" \"$home/.ssh/authorized_keys\" 2>&1\n");
    script += QStringLiteral("if [ -f \"$home/.ssh/authorized_keys\" ]; then\n");
    script += QStringLiteral("  echo AK_BYTES=`wc -c < \"$home/.ssh/authorized_keys\" | tr -d ' '`\n");
    script += QStringLiteral("  echo AK_LINES=`wc -l < \"$home/.ssh/authorized_keys\" | tr -d ' '`\n");
    script += QStringLiteral("  echo AK_HAS_BLOB=`grep -c ");
    script += shellQuote(blobTail);
    script += QStringLiteral(" \"$home/.ssh/authorized_keys\" 2>/dev/null || echo 0`\n");
    script += QStringLiteral("fi\n");
    script += QStringLiteral(
        "if command -v sshd >/dev/null 2>&1; then\n"
        "  sshd -T 2>/dev/null | awk 'tolower($1)==\"pubkeyauthentication\"||tolower($1)==\"authorizedkeysfile\"||tolower($1)==\"strictmodes\"||tolower($1)==\"permitrootlogin\" {print}'\n"
        "fi\n");

    const auto r = runRemoteScript(o, script, true, 20000);
    QString out = cleanOutput(r.stdOut);
    if (!out.isEmpty()) {
        out.replace(QLatin1Char('\n'), QStringLiteral(" | "));
        postLog(QStringLiteral("[KEY] 远端诊断：%1").arg(out));
    }
}

bool BootstrapController::verifyKeyLogin(const Options &o, QString *error)
{
    const auto r = runSsh(o, QStringLiteral("printf 'KEY_OK'"), false, 20000);
    const bool ok = r.ok() && cleanOutput(r.stdOut).contains(QStringLiteral("KEY_OK"));
    if (!ok && error)
        *error = QStringLiteral("免密登录验证失败：%1").arg(r.errorText());
    return ok;
}

bool BootstrapController::detectRemote(const Options &o, QString *osName, QString *arch, QString *error)
{
    const auto r = runSsh(o, QStringLiteral("uname -s; uname -m"), false, 20000);
    if (!r.ok()) {
        *error = QStringLiteral("检测远端系统失败：%1").arg(r.errorText());
        return false;
    }
    const QStringList lines = cleanOutput(r.stdOut).split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    if (lines.size() < 2) {
        *error = QStringLiteral("无法解析远端系统信息：%1").arg(cleanOutput(r.stdOut));
        return false;
    }
    *osName = lines.at(0).trimmed();
    *arch = lines.at(1).trimmed();
    return true;
}

bool BootstrapController::updateSshConfig(const Options &o, QString *error)
{
    const QString configPath = sshConfigPath();
    QDir().mkpath(QFileInfo(configPath).absolutePath());

    QString content;
    QFile in(configPath);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *error = QStringLiteral("无法读取 SSH config：%1").arg(in.errorString());
            return false;
        }
        content = QString::fromUtf8(in.readAll());
    }

    const QString begin = QString::fromLatin1(kBeginMarkerPrefix) + o.alias;
    const QString end = QString::fromLatin1(kEndMarkerPrefix) + o.alias;
    const QString newBlock = managedBlock(o);

    const int beginPos = content.indexOf(begin);
    if (beginPos >= 0) {
        const int endPos = content.indexOf(end, beginPos);
        if (endPos < 0) {
            *error = QStringLiteral("SSH config 中存在不完整的工具管理区块：%1").arg(o.alias);
            return false;
        }
        int replaceEnd = endPos + end.length();
        while (replaceEnd < content.size()
               && (content.at(replaceEnd) == QLatin1Char('\r') || content.at(replaceEnd) == QLatin1Char('\n'))) {
            ++replaceEnd;
        }
        content.replace(beginPos, replaceEnd - beginPos, newBlock + QLatin1Char('\n'));
    } else {
        const QRegularExpression hostRe(QStringLiteral("(?im)^\\s*Host\\s+([^\\r\\n#]+)$"));
        auto it = hostRe.globalMatch(content);
        while (it.hasNext()) {
            const QStringList hosts = it.next().captured(1).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (hosts.contains(o.alias, Qt::CaseInsensitive)) {
                *error = QStringLiteral("SSH config 已存在非本工具管理的 Host “%1”；请选择该 Host 使用，或换一个新别名")
                             .arg(o.alias);
                return false;
            }
        }

        if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
            content += QLatin1Char('\n');
        if (!content.isEmpty())
            content += QLatin1Char('\n');
        content += newBlock;
        content += QLatin1Char('\n');
    }

    if (QFileInfo::exists(configPath)) {
        QFile::remove(configPath + QStringLiteral(".crb.bak"));
        QFile::copy(configPath, configPath + QStringLiteral(".crb.bak"));
    }

    QSaveFile out(configPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("无法写入 SSH config：%1").arg(out.errorString());
        return false;
    }
    out.write(content.toUtf8());
    if (!out.commit()) {
        *error = QStringLiteral("保存 SSH config 失败：%1").arg(out.errorString());
        return false;
    }
    return true;
}

bool BootstrapController::configureRemoteProxyEnvironment(const Options &o, QString *error)
{
    const QString block = proxyProfileBlock(o);
    const QString command = QStringLiteral(
        "PROFILE=\"$HOME/.profile\"; touch \"$PROFILE\"; "
        "if command -v sed >/dev/null 2>&1; then "
        "sed -i '/^# BEGIN CODEX-REMOTE-BOOTSTRAP-PROXY$/,/^# END CODEX-REMOTE-BOOTSTRAP-PROXY$/d' \"$PROFILE\"; "
        "fi; printf '\\n%%s\\n' %1 >> \"$PROFILE\"")
        .arg(shellQuote(block));

    const auto r = runSsh(o, command, false, 20000);
    if (!r.ok()) {
        *error = QStringLiteral("配置远端代理环境失败：%1").arg(r.errorText());
        return false;
    }
    return true;
}

QString BootstrapController::findCodexPackage(const QString &packageDir,
                                              const QString &releaseSelector,
                                              const QString &arch) const
{
    const QString asset = packageAssetName(arch);
    if (asset.isEmpty())
        return {};

    QDir root(packageDir);
    if (!root.exists())
        return {};

    const bool specific = releaseSelector != QStringLiteral("latest-stable")
                       && releaseSelector != QStringLiteral("latest-any")
                       && !releaseSelector.isEmpty();
    if (specific) {
        const QString p = root.filePath(sanitizeTag(releaseSelector) + QLatin1Char('/') + asset);
        if (QFileInfo::exists(p))
            return p;
        return {};
    }

    const QString legacy = root.filePath(asset);
    QFileInfo newest;
    if (QFileInfo::exists(legacy))
        newest = QFileInfo(legacy);

    QDirIterator it(packageDir, QStringList{asset}, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!newest.exists() || info.lastModified() > newest.lastModified())
            newest = info;
    }
    return newest.exists() ? newest.absoluteFilePath() : QString();
}

bool BootstrapController::ensureCodexPackage(const Options &o,
                                             const QString &arch,
                                             QString *packagePath,
                                             QString *resolvedTag,
                                             QString *error)
{
    QDir().mkpath(o.packageDir);

    const bool specific = o.packageVersion != QStringLiteral("latest-stable")
                       && o.packageVersion != QStringLiteral("latest-any");
    if (specific) {
        const QString local = findCodexPackage(o.packageDir, o.packageVersion, arch);
        if (!local.isEmpty()) {
            *packagePath = local;
            *resolvedTag = o.packageVersion;
            return true;
        }
    }

    if (!o.autoDownloadPackage) {
        const QString local = findCodexPackage(o.packageDir, o.packageVersion, arch);
        if (local.isEmpty()) {
            *error = QStringLiteral("本地包库中没有 %1 的 %2 安装包；请到“安装包管理”下载，或开启自动下载")
                         .arg(o.packageVersion, arch);
            return false;
        }
        *packagePath = local;
        *resolvedTag = specific ? o.packageVersion : QStringLiteral("本地缓存");
        return true;
    }

    postLog(QStringLiteral("[PKG] 正在查询 OpenAI Codex GitHub Release：%1").arg(o.packageVersion));
    ReleaseInfo release;
    QString fetchError;
    if (!fetchReleaseSync(o.packageVersion, &release, &fetchError)) {
        const QString fallback = findCodexPackage(o.packageDir, o.packageVersion, arch);
        if (!fallback.isEmpty()) {
            postLog(QStringLiteral("[WARN] 在线查询失败，使用本地缓存：%1").arg(fetchError));
            *packagePath = fallback;
            *resolvedTag = specific ? o.packageVersion : QStringLiteral("本地缓存");
            return true;
        }
        *error = QStringLiteral("无法获取 Codex Release，且没有可用本地缓存：%1").arg(fetchError);
        return false;
    }

    *resolvedTag = release.tag;
    const QString expected = packagePathFor(o.packageDir, release.tag, arch);
    if (QFileInfo::exists(expected)) {
        *packagePath = expected;
        return true;
    }

    postStep(QStringLiteral("在线下载 Codex %1").arg(release.tag), 58);
    QString saved;
    if (!downloadReleaseAsset(release, arch, o.packageDir, &saved, error))
        return false;

    *packagePath = saved;
    QMetaObject::invokeMethod(this, [this] { refreshLocalPackages(); }, Qt::QueuedConnection);
    return true;
}

bool BootstrapController::uploadAndInstallCodex(const Options &o,
                                                const QString &packagePath,
                                                QString *installedPath,
                                                QString *version,
                                                QString *error)
{
    auto r = runSsh(o, QStringLiteral("mkdir -p \"$HOME/.cache/codex-remote-bootstrap\""), false, 20000);
    if (!r.ok()) {
        *error = QStringLiteral("创建远端缓存目录失败：%1").arg(r.errorText());
        return false;
    }

    const QString scp = QStandardPaths::findExecutable(QStringLiteral("scp"));
    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
         << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new");
    appendIdentityArgs(&args, o);

    QString destination;
    if (o.useSshConfigHost) {
        destination = QStringLiteral("%1:.cache/codex-remote-bootstrap/codex-package.tar.gz").arg(o.alias);
    } else {
        args << QStringLiteral("-P") << QString::number(o.port);
        destination = QStringLiteral("%1@%2:.cache/codex-remote-bootstrap/codex-package.tar.gz").arg(o.user, o.host);
    }
    args << packagePath << destination;

    postLog(QStringLiteral("[UPLOAD] 正在上传 Codex 安装包"));
    r = runProcess(scp, args, 600000);
    if (!r.ok()) {
        *error = QStringLiteral("上传 Codex 安装包失败：%1").arg(r.errorText());
        return false;
    }

    const QString prepare = QStringLiteral(
        "set -e; D=\"$HOME/.cache/codex-remote-bootstrap\"; T=\"$D/unpack\"; "
        "rm -rf \"$T\"; mkdir -p \"$T\"; tar -xzf \"$D/codex-package.tar.gz\" -C \"$T\"; "
        "B=$(find \"$T\" -maxdepth 3 -type f -name 'codex*' | head -n 1); "
        "test -n \"$B\"; chmod 755 \"$B\"; cp \"$B\" \"$D/codex.bin\"; \"$D/codex.bin\" --version");
    r = runSsh(o, prepare, false, 120000);
    if (!r.ok()) {
        *error = QStringLiteral("远端解压/校验 Codex 失败：%1；请确认远端有 tar/gzip 且安装包完整").arg(r.errorText());
        return false;
    }
    postLog(QStringLiteral("[PKG] 安装包自检：%1").arg(cleanOutput(r.stdOut)));

    const auto rootCheck = runSsh(o, QStringLiteral("id -u"), false, 15000);
    const bool isRoot = rootCheck.ok() && cleanOutput(rootCheck.stdOut) == QStringLiteral("0");

    if (isRoot) {
        r = runSsh(o,
                   QStringLiteral("install -m 755 \"$HOME/.cache/codex-remote-bootstrap/codex.bin\" /usr/local/bin/codex"),
                   false, 30000);
        if (!r.ok()) {
            *error = QStringLiteral("安装到 /usr/local/bin/codex 失败：%1").arg(r.errorText());
            return false;
        }
        *installedPath = QStringLiteral("/usr/local/bin/codex");
    } else {
        const auto sudoNoPass = runSsh(o, QStringLiteral("sudo -n true"), false, 15000);
        bool systemInstalled = false;

        if (sudoNoPass.ok()) {
            r = runSsh(o,
                       QStringLiteral("sudo -n install -m 755 \"$HOME/.cache/codex-remote-bootstrap/codex.bin\" /usr/local/bin/codex"),
                       false, 30000);
            systemInstalled = r.ok();
        } else if (!o.password.isEmpty()) {
            QByteArray sudoInput = o.password.toUtf8();
            sudoInput += '\n';
            r = runSsh(o,
                       QStringLiteral("sudo -S -p '' install -m 755 \"$HOME/.cache/codex-remote-bootstrap/codex.bin\" /usr/local/bin/codex"),
                       false, 30000, sudoInput);
            systemInstalled = r.ok();
        }

        if (systemInstalled) {
            *installedPath = QStringLiteral("/usr/local/bin/codex");
        } else {
            postLog(QStringLiteral("[WARN] sudo 安装失败，回退到 ~/.local/bin/codex"));
            const QString localInstall = QStringLiteral(
                "set -e; mkdir -p \"$HOME/.local/bin\"; "
                "install -m 755 \"$HOME/.cache/codex-remote-bootstrap/codex.bin\" \"$HOME/.local/bin/codex\"; "
                "PROFILE=\"$HOME/.profile\"; touch \"$PROFILE\"; "
                "grep -qxF 'export PATH=\"$HOME/.local/bin:$PATH\"' \"$PROFILE\" || printf '\\nexport PATH=\"$HOME/.local/bin:$PATH\"\\n' >> \"$PROFILE\"; "
                "BASHRC=\"$HOME/.bashrc\"; touch \"$BASHRC\"; "
                "grep -qxF 'export PATH=\"$HOME/.local/bin:$PATH\"' \"$BASHRC\" || printf '\\nexport PATH=\"$HOME/.local/bin:$PATH\"\\n' >> \"$BASHRC\"");
            r = runSsh(o, localInstall, false, 30000);
            if (!r.ok()) {
                *error = QStringLiteral("用户级 Codex 安装也失败：%1").arg(r.errorText());
                return false;
            }
            *installedPath = QStringLiteral("~/.local/bin/codex");
        }
    }

    return verifyCodex(o, version, error);
}

bool BootstrapController::syncAuth(const Options &o, QString *error)
{
    const QString authPath = QDir(QDir::homePath()).filePath(QStringLiteral(".codex/auth.json"));
    if (!QFileInfo::exists(authPath)) {
        *error = QStringLiteral("本机不存在 %1，无法同步登录态").arg(QDir::toNativeSeparators(authPath));
        return false;
    }

    auto r = runSsh(o, QStringLiteral("mkdir -p \"$HOME/.codex\" && chmod 700 \"$HOME/.codex\""), false, 15000);
    if (!r.ok()) {
        *error = QStringLiteral("创建远端 ~/.codex 失败：%1").arg(r.errorText());
        return false;
    }

    const QString scp = QStandardPaths::findExecutable(QStringLiteral("scp"));
    QStringList args;
    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes")
         << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new");
    appendIdentityArgs(&args, o);
    QString destination;
    if (o.useSshConfigHost) {
        destination = QStringLiteral("%1:.codex/auth.json").arg(o.alias);
    } else {
        args << QStringLiteral("-P") << QString::number(o.port);
        destination = QStringLiteral("%1@%2:.codex/auth.json").arg(o.user, o.host);
    }
    args << authPath << destination;

    r = runProcess(scp, args, 60000);
    if (!r.ok()) {
        *error = QStringLiteral("同步 auth.json 失败：%1").arg(r.errorText());
        return false;
    }

    r = runSsh(o, QStringLiteral("chmod 600 \"$HOME/.codex/auth.json\""), false, 15000);
    if (!r.ok()) {
        *error = QStringLiteral("设置远端 auth.json 权限失败：%1").arg(r.errorText());
        return false;
    }
    return true;
}

bool BootstrapController::verifyCodex(const Options &o, QString *version, QString *error)
{
    const QString command = QStringLiteral(
        "if command -v codex >/dev/null 2>&1; then codex --version; "
        "elif [ -x \"$HOME/.local/bin/codex\" ]; then \"$HOME/.local/bin/codex\" --version; else exit 127; fi");
    const auto r = runSsh(o, command, false, 30000);
    if (!r.ok()) {
        if (error)
            *error = QStringLiteral("Codex CLI 验证失败：%1").arg(r.errorText());
        return false;
    }
    *version = cleanOutput(r.stdOut);
    return true;
}

void BootstrapController::refreshSshHosts()
{
    const QVariantList hosts = discoverSshHosts();
    if (hosts == m_sshHosts)
        return;
    m_sshHosts = hosts;
    emit sshHostsChanged();
}

QVariantMap BootstrapController::sshHostAt(int index) const
{
    if (index < 0 || index >= m_sshHosts.size())
        return {};
    return m_sshHosts.at(index).toMap();
}

QVariantList BootstrapController::discoverSshHosts() const
{
    QVariantList result;
    QVariantMap manual;
    manual.insert(QStringLiteral("label"), QStringLiteral("＋ 手动输入 / 新增设备"));
    manual.insert(QStringLiteral("alias"), QString());
    manual.insert(QStringLiteral("manual"), true);
    result.push_back(manual);

    QFile file(sshConfigPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QString content = QString::fromUtf8(file.readAll());
    const QRegularExpression hostRe(QStringLiteral("(?im)^\\s*Host\\s+([^\\r\\n#]+)$"));
    auto it = hostRe.globalMatch(content);
    QSet<QString> seen;

    while (it.hasNext()) {
        const QStringList tokens = it.next().captured(1).simplified().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            if (isPatternHost(token))
                continue;
            const QString key = token.toLower();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            QVariantMap host = resolveSshHost(token);
            if (host.isEmpty()) {
                host.insert(QStringLiteral("alias"), token);
                host.insert(QStringLiteral("hostName"), token);
                host.insert(QStringLiteral("user"), QString());
                host.insert(QStringLiteral("port"), 22);
                host.insert(QStringLiteral("identityFile"), defaultIdentityPath());
            }
            host.insert(QStringLiteral("manual"), false);
            const QString user = host.value(QStringLiteral("user")).toString();
            const QString hostName = host.value(QStringLiteral("hostName")).toString();
            host.insert(QStringLiteral("label"), QStringLiteral("%1   ·   %2@%3").arg(token, user, hostName));
            result.push_back(host);
        }
    }
    return result;
}

QVariantMap BootstrapController::resolveSshHost(const QString &alias) const
{
    const QString ssh = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    if (ssh.isEmpty())
        return {};

    const ProcessResult r = runProcess(ssh, {QStringLiteral("-G"), alias}, 10000);
    if (!r.ok())
        return {};

    QVariantMap map;
    map.insert(QStringLiteral("alias"), alias);
    QStringList identities;
    const QStringList lines = QString::fromUtf8(r.stdOut).split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            continue;
        const QString key = line.left(space).trimmed().toLower();
        const QString value = line.mid(space + 1).trimmed();
        if (key == QStringLiteral("hostname") && !map.contains(QStringLiteral("hostName")))
            map.insert(QStringLiteral("hostName"), value);
        else if (key == QStringLiteral("user") && !map.contains(QStringLiteral("user")))
            map.insert(QStringLiteral("user"), value);
        else if (key == QStringLiteral("port") && !map.contains(QStringLiteral("port")))
            map.insert(QStringLiteral("port"), value.toInt());
        else if (key == QStringLiteral("identityfile"))
            identities.push_back(expandHomePath(value));
    }

    QString identity;
    for (const QString &candidate : identities) {
        if (QFileInfo::exists(candidate)) {
            identity = candidate;
            break;
        }
    }
    if (identity.isEmpty() && !identities.isEmpty())
        identity = identities.first();
    if (identity.isEmpty())
        identity = defaultIdentityPath();

    map.insert(QStringLiteral("identityFile"), QDir::toNativeSeparators(identity));
    map.insert(QStringLiteral("source"), QStringLiteral("~/.ssh/config"));
    return map;
}

QString BootstrapController::expandHomePath(const QString &path)
{
    QString p = path;
    if (p.startsWith(QStringLiteral("~/")) || p.startsWith(QStringLiteral("~\\")))
        p = QDir(QDir::homePath()).filePath(p.mid(2));
    p.replace(QStringLiteral("%d"), QDir::homePath());
    return QDir::fromNativeSeparators(p);
}

void BootstrapController::refreshReleaseCatalog(bool includePrerelease)
{
    if (m_packageRunning)
        return;
    setPackageRunning(true);
    emit packageStatus(QStringLiteral("正在获取 OpenAI Codex Release 列表…"), 5);

    m_packageFuture = QtConcurrent::run([this, includePrerelease] {
        QList<ReleaseInfo> releases;
        QString error;
        if (!fetchReleaseCatalogSync(includePrerelease, &releases, &error)) {
            finishPackage(false, error);
            return;
        }

        QVariantList list;
        for (const ReleaseInfo &release : releases) {
            QVariantMap item;
            item.insert(QStringLiteral("tag"), release.tag);
            item.insert(QStringLiteral("name"), release.name.isEmpty() ? release.tag : release.name);
            item.insert(QStringLiteral("publishedAt"), release.publishedAt);
            item.insert(QStringLiteral("prerelease"), release.prerelease);

            bool arm = false;
            bool x64 = false;
            qint64 armSize = 0;
            qint64 x64Size = 0;
            for (const ReleaseAsset &asset : release.assets) {
                if (asset.name == packageAssetName(QStringLiteral("arm64"))) {
                    arm = true;
                    armSize = asset.size;
                } else if (asset.name == packageAssetName(QStringLiteral("x86_64"))) {
                    x64 = true;
                    x64Size = asset.size;
                }
            }
            item.insert(QStringLiteral("arm64Available"), arm);
            item.insert(QStringLiteral("x86Available"), x64);
            item.insert(QStringLiteral("arm64Size"), humanBytes(armSize));
            item.insert(QStringLiteral("x86Size"), humanBytes(x64Size));
            item.insert(QStringLiteral("label"), QStringLiteral("%1%2").arg(release.tag, release.prerelease ? QStringLiteral("  · Preview") : QString()));
            list.push_back(item);
        }

        QMetaObject::invokeMethod(this, [this, list] {
            m_releaseCatalog = list;
            emit releaseCatalogChanged();
        }, Qt::QueuedConnection);
        finishPackage(true, QStringLiteral("已获取 %1 个 Release").arg(list.size()));
    });
}

void BootstrapController::refreshLocalPackages()
{
    QDir().mkpath(defaultPackageDir());
    QVariantList list;
    QDirIterator it(defaultPackageDir(), QStringList{QStringLiteral("codex-*-unknown-linux-musl.tar.gz")},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        QVariantMap item;
        const QString fileName = info.fileName();
        QString arch = QStringLiteral("unknown");
        if (fileName.contains(QStringLiteral("aarch64")))
            arch = QStringLiteral("arm64");
        else if (fileName.contains(QStringLiteral("x86_64")))
            arch = QStringLiteral("x86_64");

        QString tag = info.dir().dirName();
        if (QDir(info.dir().absolutePath()).absolutePath() == QDir(defaultPackageDir()).absolutePath())
            tag = QStringLiteral("手动导入");

        item.insert(QStringLiteral("tag"), tag);
        item.insert(QStringLiteral("arch"), arch);
        item.insert(QStringLiteral("fileName"), fileName);
        item.insert(QStringLiteral("filePath"), QDir::toNativeSeparators(info.absoluteFilePath()));
        item.insert(QStringLiteral("size"), humanBytes(info.size()));
        item.insert(QStringLiteral("modified"), info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        list.push_back(item);
    }
    m_localPackages = list;
    emit localPackagesChanged();
}

void BootstrapController::downloadPackage(const QString &releaseSelector, const QString &arch)
{
    if (m_packageRunning)
        return;
    if (arch != QStringLiteral("arm64") && arch != QStringLiteral("x86_64") && arch != QStringLiteral("both")) {
        emit packageCompleted(false, QStringLiteral("不支持的架构：%1").arg(arch));
        return;
    }

    setPackageRunning(true);
    emit packageStatus(QStringLiteral("正在解析 Release…"), 3);
    m_packageFuture = QtConcurrent::run([this, releaseSelector, arch] {
        ReleaseInfo release;
        QString error;
        if (!fetchReleaseSync(releaseSelector, &release, &error)) {
            finishPackage(false, error);
            return;
        }

        const QStringList archs = arch == QStringLiteral("both")
                ? QStringList{QStringLiteral("arm64"), QStringLiteral("x86_64")}
                : QStringList{arch};
        for (const QString &oneArch : archs) {
            QString saved;
            if (!downloadReleaseAsset(release, oneArch, defaultPackageDir(), &saved, &error)) {
                finishPackage(false, error);
                return;
            }
            postLog(QStringLiteral("[PKG] 已下载 %1").arg(QDir::toNativeSeparators(saved)));
        }

        QMetaObject::invokeMethod(this, [this] { refreshLocalPackages(); }, Qt::QueuedConnection);
        finishPackage(true, QStringLiteral("%1 下载完成").arg(release.tag));
    });
}

void BootstrapController::deleteLocalPackage(const QString &filePath)
{
    const QString absolute = QFileInfo(filePath).absoluteFilePath();
    const QString root = QDir(defaultPackageDir()).absolutePath();
    const QString normalized = QDir::cleanPath(absolute);
    if (!normalized.startsWith(root, Qt::CaseInsensitive)) {
        emit packageCompleted(false, QStringLiteral("拒绝删除包库目录之外的文件"));
        return;
    }
    if (!QFile::remove(absolute)) {
        emit packageCompleted(false, QStringLiteral("删除失败：%1").arg(QDir::toNativeSeparators(absolute)));
        return;
    }
    QDir parent(QFileInfo(absolute).absolutePath());
    if (parent.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        parent.removeRecursively();
    refreshLocalPackages();
    emit packageCompleted(true, QStringLiteral("已删除本地安装包"));
}

void BootstrapController::openPackageFolder()
{
    QDir().mkpath(defaultPackageDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(defaultPackageDir()));
}

QString BootstrapController::curlExecutable() const
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("curl"));
    if (!found.isEmpty())
        return found;
    const QString systemCurl = QStringLiteral("C:/Windows/System32/curl.exe");
    if (QFileInfo::exists(systemCurl))
        return systemCurl;
    return {};
}

QStringList BootstrapController::curlProxyCandidates() const
{
    QStringList list;
    QSet<QString> seen;
    auto add = [&](const QString &value) {
        if (seen.contains(value))
            return;
        seen.insert(value);
        list.push_back(value);
    };

    const QString cached = cachedCurlProxy();
    if (cached != QLatin1String("__unset__"))
        add(cached);

    const QNetworkProxy resolved = resolveGithubProxy();
    if (resolved.type() != QNetworkProxy::NoProxy && !resolved.hostName().isEmpty()) {
        const QString hostPort = QStringLiteral("%1:%2").arg(resolved.hostName()).arg(resolved.port());
        add(QStringLiteral("http://") + hostPort);
        add(QStringLiteral("socks5h://") + hostPort);
    }

    add(QStringLiteral("http://127.0.0.1:7890"));
    add(QStringLiteral("socks5h://127.0.0.1:7890"));
    add(QStringLiteral("socks5h://127.0.0.1:7891"));
    add(QStringLiteral("http://127.0.0.1:7897"));
    add(QStringLiteral("http://127.0.0.1:10809"));
    add(QString());
    return list;
}

BootstrapController::HttpResult BootstrapController::curlHttpGet(const QUrl &url,
                                                                 int timeoutMs,
                                                                 const QString &proxyUrl) const
{
    HttpResult result;
    const QString curl = curlExecutable();
    if (curl.isEmpty()) {
        result.error = QStringLiteral("未找到 curl");
        return result;
    }

    QStringList args;
    args << QStringLiteral("-sS") << QStringLiteral("-L") << QStringLiteral("--http1.1")
         << QStringLiteral("--ssl-no-revoke")
         << QStringLiteral("--connect-timeout") << QStringLiteral("15")
         << QStringLiteral("--max-time") << QString::number(qMax(20, timeoutMs / 1000))
         << QStringLiteral("-A") << QStringLiteral("CodexRemoteBootstrap/0.3")
         << QStringLiteral("-H") << QStringLiteral("Accept: application/vnd.github+json")
         << QStringLiteral("-H") << QStringLiteral("X-GitHub-Api-Version: 2022-11-28")
         << QStringLiteral("-w") << QStringLiteral("\nCRB_HTTP=%{http_code}");
    if (!proxyUrl.isEmpty())
        args << QStringLiteral("-x") << proxyUrl;
    args << url.toString();

    const auto r = runProcess(curl, args, timeoutMs + 8000);
    QByteArray body = r.stdOut;
    int status = 0;
    const int marker = body.lastIndexOf("CRB_HTTP=");
    if (marker >= 0) {
        status = body.mid(marker + 9).trimmed().toInt();
        body = body.left(marker);
        if (body.endsWith('\n'))
            body.chop(1);
        if (body.endsWith('\r'))
            body.chop(1);
    }

    result.status = status;
    result.data = body;
    if (status >= 200 && status < 300 && !body.isEmpty()) {
        result.ok = true;
        return result;
    }
    if (status == 403 && body.contains("rate limit")) {
        result.error = QStringLiteral("GitHub API 频率限制（未登录每小时 60 次），请稍后再试");
        return result;
    }
    if (status > 0) {
        result.error = QStringLiteral("HTTP %1：%2").arg(status).arg(QString::fromUtf8(body.left(300)));
        return result;
    }
    result.error = r.errorText();
    if (result.error.isEmpty())
        result.error = QStringLiteral("curl 请求失败");
    return result;
}

bool BootstrapController::curlDownloadToFile(const QUrl &url,
                                             const QString &target,
                                             int timeoutMs,
                                             QString *error) const
{
    const QString curl = curlExecutable();
    if (curl.isEmpty()) {
        *error = QStringLiteral("未找到 curl");
        return false;
    }

    QFile::remove(target);
    QString lastError;
    for (const QString &proxyUrl : curlProxyCandidates()) {
        if (!proxyUrl.isEmpty()) {
            const QUrl proxy(proxyUrl);
            if (!proxyPortOpen(proxy.host(), static_cast<quint16>(proxy.port())))
                continue;
        }

        const_cast<BootstrapController *>(this)->postLog(
            QStringLiteral("[NET] curl 下载走 %1").arg(curlProxyLabel(proxyUrl)));

        QStringList args;
        args << QStringLiteral("-sS") << QStringLiteral("-L") << QStringLiteral("--http1.1")
             << QStringLiteral("--ssl-no-revoke")
             << QStringLiteral("--connect-timeout") << QStringLiteral("20")
             << QStringLiteral("--max-time") << QString::number(qMax(60, timeoutMs / 1000))
             << QStringLiteral("-A") << QStringLiteral("CodexRemoteBootstrap/0.3")
             << QStringLiteral("-o") << target
             << QStringLiteral("-w") << QStringLiteral("%{http_code}");
        if (!proxyUrl.isEmpty())
            args << QStringLiteral("-x") << proxyUrl;
        args << url.toString();

        const auto r = runProcess(curl, args, timeoutMs + 8000);
        const int status = cleanOutput(r.stdOut).toInt();
        if (r.ok() && status >= 200 && status < 300 && QFileInfo::exists(target) && QFileInfo(target).size() > 0) {
            cachedCurlProxy() = proxyUrl;
            return true;
        }
        lastError = status > 0
                ? QStringLiteral("HTTP %1").arg(status)
                : r.errorText();
        QFile::remove(target);
        if (status == 403)
            break;
    }

    *error = lastError.isEmpty() ? QStringLiteral("curl 下载失败") : lastError;
    return false;
}

BootstrapController::HttpResult BootstrapController::httpGet(const QUrl &url, int timeoutMs) const
{
    const QString curl = curlExecutable();
    if (!curl.isEmpty()) {
        for (const QString &proxyUrl : curlProxyCandidates()) {
            if (!proxyUrl.isEmpty()) {
                const QUrl proxy(proxyUrl);
                if (!proxyPortOpen(proxy.host(), static_cast<quint16>(proxy.port())))
                    continue;
            }
            const_cast<BootstrapController *>(this)->postLog(
                QStringLiteral("[NET] curl 访问 GitHub · %1").arg(curlProxyLabel(proxyUrl)));
            const HttpResult result = curlHttpGet(url, timeoutMs, proxyUrl);
            if (result.ok) {
                cachedCurlProxy() = proxyUrl;
                return result;
            }
            const_cast<BootstrapController *>(this)->postLog(
                QStringLiteral("[NET] %1 失败：%2").arg(curlProxyLabel(proxyUrl), result.error));
            if (result.status == 403)
                return result;
        }
    }

    auto runOnce = [&](QNetworkAccessManager *manager) {
        HttpResult result;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CodexRemoteBootstrap/0.3"));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        QNetworkReply *reply = manager->get(request);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();

        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.data = reply->readAll();
        if (!timer.isActive()) {
            result.error = QStringLiteral("网络请求超时：%1").arg(url.toString());
        } else if (reply->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("%1 (HTTP %2)").arg(reply->errorString()).arg(result.status);
        } else if (result.status < 200 || result.status >= 300) {
            result.error = QStringLiteral("HTTP %1：%2").arg(result.status).arg(QString::fromUtf8(result.data.left(300)));
        } else {
            result.ok = true;
        }
        reply->deleteLater();
        return result;
    };

    QNetworkAccessManager manager;
    applyGithubProxy(&manager);
    return runOnce(&manager);
}

bool BootstrapController::fetchReleaseCatalogSync(bool includePrerelease,
                                                  QList<ReleaseInfo> *releases,
                                                  QString *error) const
{
    const HttpResult http = httpGet(QUrl(QString::fromLatin1(kGithubApiBase) + QStringLiteral("/releases?per_page=100")), 60000);
    if (!http.ok) {
        *error = QStringLiteral("获取 Codex Release 列表失败：%1").arg(http.error);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(http.data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        *error = QStringLiteral("解析 GitHub Release 数据失败：%1").arg(parseError.errorString());
        return false;
    }

    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject())
            continue;
        const QVariantMap obj = value.toObject().toVariantMap();
        ReleaseInfo release;
        if (!parseReleaseObject(obj, &release))
            continue;
        if (!includePrerelease && release.prerelease)
            continue;
        bool hasCodexAsset = false;
        for (const ReleaseAsset &asset : release.assets) {
            if (asset.name == packageAssetName(QStringLiteral("arm64"))
                || asset.name == packageAssetName(QStringLiteral("x86_64"))) {
                hasCodexAsset = true;
                break;
            }
        }
        if (!hasCodexAsset)
            continue;
        releases->push_back(release);
    }
    return true;
}

bool BootstrapController::fetchReleaseSync(const QString &selector, ReleaseInfo *release, QString *error) const
{
    auto hasCodexAsset = [](const ReleaseInfo &candidate) {
        for (const ReleaseAsset &asset : candidate.assets) {
            if (asset.name == packageAssetName(QStringLiteral("arm64"))
                || asset.name == packageAssetName(QStringLiteral("x86_64"))) {
                return true;
            }
        }
        return false;
    };

    if (selector == QStringLiteral("latest-stable")
        || selector == QStringLiteral("latest-any")
        || selector.isEmpty()) {
        const HttpResult http = httpGet(
            QUrl(QString::fromLatin1(kGithubApiBase) + QStringLiteral("/releases?per_page=100")), 60000);
        if (!http.ok) {
            *error = QStringLiteral("获取 Codex Release 失败：%1").arg(http.error);
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(http.data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            *error = QStringLiteral("解析 Release 数据失败：%1").arg(parseError.errorString());
            return false;
        }

        const bool stableOnly = selector != QStringLiteral("latest-any");
        for (const QJsonValue &value : doc.array()) {
            if (!value.isObject())
                continue;
            ReleaseInfo candidate;
            if (!parseReleaseObject(value.toObject().toVariantMap(), &candidate))
                continue;
            if (stableOnly && candidate.prerelease)
                continue;
            if (!hasCodexAsset(candidate))
                continue;
            *release = candidate;
            return true;
        }

        *error = stableOnly
            ? QStringLiteral("没有找到包含 Linux Codex CLI 资产的稳定 Release")
            : QStringLiteral("没有找到包含 Linux Codex CLI 资产的 Release");
        return false;
    }

    const QByteArray encoded = QUrl::toPercentEncoding(selector);
    const QUrl url(QString::fromLatin1(kGithubApiBase)
                   + QStringLiteral("/releases/tags/")
                   + QString::fromLatin1(encoded));
    const HttpResult http = httpGet(url, 30000);
    if (!http.ok) {
        *error = QStringLiteral("获取 Codex Release 失败：%1").arg(http.error);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(http.data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("解析 Release 数据失败：%1").arg(parseError.errorString());
        return false;
    }

    if (!parseReleaseObject(doc.object().toVariantMap(), release)) {
        *error = QStringLiteral("Release 数据缺少 tag_name");
        return false;
    }
    if (!hasCodexAsset(*release)) {
        *error = QStringLiteral("Release %1 不包含 Linux Codex CLI 安装包").arg(release->tag);
        return false;
    }
    return true;
}

bool BootstrapController::parseReleaseObject(const QVariantMap &obj, ReleaseInfo *release) const
{
    release->tag = obj.value(QStringLiteral("tag_name")).toString();
    if (release->tag.isEmpty())
        return false;
    release->name = obj.value(QStringLiteral("name")).toString();
    release->publishedAt = obj.value(QStringLiteral("published_at")).toString();
    release->prerelease = obj.value(QStringLiteral("prerelease")).toBool();
    release->assets.clear();

    const QVariantList assets = obj.value(QStringLiteral("assets")).toList();
    for (const QVariant &value : assets) {
        const QVariantMap a = value.toMap();
        ReleaseAsset asset;
        asset.name = a.value(QStringLiteral("name")).toString();
        asset.url = a.value(QStringLiteral("browser_download_url")).toString();
        asset.digest = a.value(QStringLiteral("digest")).toString();
        asset.size = a.value(QStringLiteral("size")).toLongLong();
        if (!asset.name.isEmpty() && !asset.url.isEmpty())
            release->assets.push_back(asset);
    }
    return true;
}

bool BootstrapController::downloadReleaseAsset(const ReleaseInfo &release,
                                               const QString &arch,
                                               const QString &packageDir,
                                               QString *savedPath,
                                               QString *error)
{
    const QString wanted = packageAssetName(arch);
    ReleaseAsset asset;
    bool found = false;
    for (const ReleaseAsset &candidate : release.assets) {
        if (candidate.name == wanted) {
            asset = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        *error = QStringLiteral("Release %1 中没有 %2").arg(release.tag, wanted);
        return false;
    }

    const QString target = packagePathFor(packageDir, release.tag, arch);
    QDir().mkpath(QFileInfo(target).absolutePath());

    if (QFileInfo::exists(target)) {
        if (asset.digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
            const QString expected = asset.digest.mid(QStringLiteral("sha256:").size()).toLower();
            if (sha256File(target).compare(expected, Qt::CaseInsensitive) == 0) {
                *savedPath = target;
                return true;
            }
        } else if (QFileInfo(target).size() == asset.size && asset.size > 0) {
            *savedPath = target;
            return true;
        }
    }

    const QString part = target + QStringLiteral(".part");
    QString curlError;
    if (!curlExecutable().isEmpty()
        && curlDownloadToFile(QUrl(asset.url), part, 10 * 60 * 1000, &curlError)) {
        QFile::remove(target);
        if (!QFile::rename(part, target)) {
            QFile::remove(part);
            *error = QStringLiteral("无法保存安装包到 %1").arg(QDir::toNativeSeparators(target));
            return false;
        }
    } else {
        QFile::remove(part);
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法创建安装包：%1").arg(file.errorString());
        return false;
    }

    QNetworkAccessManager manager;
    applyGithubProxy(&manager);
    QNetworkRequest request(QUrl(asset.url));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CodexRemoteBootstrap/0.3"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::readyRead, [&] { file.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [this, wanted](qint64 received, qint64 total) {
        const int percent = total > 0 ? qBound(0, static_cast<int>((received * 100) / total), 100) : 0;
        QMetaObject::invokeMethod(this, [this, wanted, received, total, percent] {
            emit packageStatus(QStringLiteral("下载 %1 · %2 / %3")
                                   .arg(wanted, humanBytes(received), total > 0 ? humanBytes(total) : QStringLiteral("?")),
                               percent);
        }, Qt::QueuedConnection);
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(10 * 60 * 1000);
    loop.exec();
    if (reply->bytesAvailable() > 0)
        file.write(reply->readAll());

    if (!timer.isActive()) {
        file.cancelWriting();
        reply->deleteLater();
        *error = QStringLiteral("下载 %1 超时").arg(wanted);
        return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString replyError = reply->errorString();
        file.cancelWriting();
        reply->deleteLater();
        *error = QStringLiteral("下载 %1 失败：%2").arg(wanted, replyError);
        return false;
    }
    reply->deleteLater();

    if (!file.commit()) {
        *error = QStringLiteral("保存 %1 失败：%2").arg(wanted, file.errorString());
        return false;
    }
    }

    if (asset.digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        const QString expected = asset.digest.mid(QStringLiteral("sha256:").size()).toLower();
        const QString actual = sha256File(target).toLower();
        if (actual != expected) {
            QFile::remove(target);
            *error = QStringLiteral("%1 SHA256 校验失败").arg(wanted);
            return false;
        }
    }

    *savedPath = target;
    return true;
}

QString BootstrapController::packagePathFor(const QString &packageDir,
                                            const QString &tag,
                                            const QString &arch) const
{
    const QString asset = packageAssetName(arch);
    if (asset.isEmpty() || tag.isEmpty())
        return {};
    return QDir(packageDir).filePath(sanitizeTag(tag) + QLatin1Char('/') + asset);
}

QString BootstrapController::packageAssetName(const QString &arch)
{
    const QString normalized = normalizeArch(arch);
    if (normalized == QStringLiteral("arm64"))
        return QStringLiteral("codex-aarch64-unknown-linux-musl.tar.gz");
    if (normalized == QStringLiteral("x86_64"))
        return QStringLiteral("codex-x86_64-unknown-linux-musl.tar.gz");
    return {};
}

QString BootstrapController::sanitizeTag(const QString &tag)
{
    QString out = tag;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    if (out.isEmpty())
        out = QStringLiteral("unknown");
    return out;
}

QString BootstrapController::humanBytes(qint64 bytes)
{
    const double b = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString BootstrapController::sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString BootstrapController::shellQuote(const QString &value)
{
    QString v = value;
    v.replace(QChar(39), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(v);
}

QString BootstrapController::normalizeArch(const QString &arch)
{
    const QString a = arch.trimmed().toLower();
    if (a == QStringLiteral("aarch64") || a == QStringLiteral("arm64"))
        return QStringLiteral("arm64");
    if (a == QStringLiteral("x86_64") || a == QStringLiteral("amd64") || a == QStringLiteral("x64"))
        return QStringLiteral("x86_64");
    return {};
}

QString BootstrapController::safeAlias(const QString &value)
{
    QString out = value;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("-"));
    out.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return out;
}

QString BootstrapController::managedBlock(const Options &o)
{
    QString identity = QDir::fromNativeSeparators(o.identityPath);
    QString block;
    block += QString::fromLatin1(kBeginMarkerPrefix) + o.alias + QLatin1Char('\n');
    block += QStringLiteral("Host %1\n").arg(o.alias);
    block += QStringLiteral("    HostName %1\n").arg(o.host);
    block += QStringLiteral("    User %1\n").arg(o.user);
    block += QStringLiteral("    Port %1\n").arg(o.port);
    block += QStringLiteral("    IdentityFile \"%1\"\n").arg(identity);
    block += QStringLiteral("    IdentitiesOnly yes\n");
    block += QStringLiteral("    ServerAliveInterval 30\n");
    block += QStringLiteral("    ServerAliveCountMax 3\n");
    block += QStringLiteral("    StrictHostKeyChecking accept-new\n");
    if (o.proxyEnabled) {
        block += QStringLiteral("    RemoteForward 127.0.0.1:%1 %2:%3\n")
                     .arg(o.remoteProxyPort).arg(o.proxyHost).arg(o.proxyPort);
    }
    block += QString::fromLatin1(kEndMarkerPrefix) + o.alias;
    return block;
}

QString BootstrapController::proxyProfileBlock(const Options &o)
{
    return QString::fromLatin1(kProxyBegin) + QLatin1Char('\n')
         + QStringLiteral("export HTTP_PROXY=http://127.0.0.1:%1\n").arg(o.remoteProxyPort)
         + QStringLiteral("export HTTPS_PROXY=http://127.0.0.1:%1\n").arg(o.remoteProxyPort)
         + QStringLiteral("export http_proxy=http://127.0.0.1:%1\n").arg(o.remoteProxyPort)
         + QStringLiteral("export https_proxy=http://127.0.0.1:%1\n").arg(o.remoteProxyPort)
         + QString::fromLatin1(kProxyEnd);
}
