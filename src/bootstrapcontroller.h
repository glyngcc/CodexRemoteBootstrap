#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class BootstrapController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool packageRunning READ packageRunning NOTIFY packageRunningChanged)
    Q_PROPERTY(QString defaultIdentityPath READ defaultIdentityPath CONSTANT)
    Q_PROPERTY(QString defaultPackageDir READ defaultPackageDir CONSTANT)
    Q_PROPERTY(QString sshConfigPath READ sshConfigPath CONSTANT)
    Q_PROPERTY(QVariantList sshHosts READ sshHosts NOTIFY sshHostsChanged)
    Q_PROPERTY(QVariantList releaseCatalog READ releaseCatalog NOTIFY releaseCatalogChanged)
    Q_PROPERTY(QVariantList localPackages READ localPackages NOTIFY localPackagesChanged)

public:
    explicit BootstrapController(QObject *parent = nullptr);
    ~BootstrapController() override;

    bool running() const { return m_running; }
    bool packageRunning() const { return m_packageRunning; }
    QString defaultIdentityPath() const;
    QString defaultPackageDir() const;
    QString sshConfigPath() const;

    QVariantList sshHosts() const { return m_sshHosts; }
    QVariantList releaseCatalog() const { return m_releaseCatalog; }
    QVariantList localPackages() const { return m_localPackages; }

    Q_INVOKABLE void testConnection(const QVariantMap &options);
    Q_INVOKABLE void configure(const QVariantMap &options);
    Q_INVOKABLE QString suggestedAlias(const QString &host) const;
    Q_INVOKABLE QString localPathFromUrl(const QUrl &url) const;

    // SSH config discovery. Explicit Host aliases are listed and their effective
    // settings are resolved with `ssh -G`, so Include/wildcard defaults are honored.
    Q_INVOKABLE void refreshSshHosts();
    Q_INVOKABLE QVariantMap sshHostAt(int index) const;

    // Codex package library.
    Q_INVOKABLE void refreshReleaseCatalog(bool includePrerelease = true);
    Q_INVOKABLE void refreshLocalPackages();
    Q_INVOKABLE void downloadPackage(const QString &releaseSelector, const QString &arch);
    Q_INVOKABLE void deleteLocalPackage(const QString &filePath);
    Q_INVOKABLE void openPackageFolder();

signals:
    void runningChanged();
    void packageRunningChanged();
    void sshHostsChanged();
    void releaseCatalogChanged();
    void localPackagesChanged();

    void logLine(const QString &line);
    void stepChanged(const QString &step, int percent);
    void completed(bool ok, const QString &message);

    void packageStatus(const QString &message, int percent);
    void packageCompleted(bool ok, const QString &message);

private:
    struct Options;
    struct ProcessResult;
    struct ReleaseAsset;
    struct ReleaseInfo;
    struct HttpResult;

    void setRunning(bool running);
    void setPackageRunning(bool running);
    void postLog(const QString &line);
    void postStep(const QString &step, int percent);
    void finish(bool ok, const QString &message);
    void finishPackage(bool ok, const QString &message);

    bool parseOptions(const QVariantMap &map, Options *out, QString *error) const;
    void runTest(Options options);
    void runConfigure(Options options);

    ProcessResult runProcess(const QString &program,
                             const QStringList &arguments,
                             int timeoutMs,
                             const QByteArray &stdinData = {},
                             bool useAskPass = false,
                             const QString &password = {}) const;
    ProcessResult runSsh(const Options &o,
                         const QString &remoteCommand,
                         bool passwordAuth,
                         int timeoutMs = 30000,
                         const QByteArray &stdinData = {}) const;
    ProcessResult runRemoteScript(const Options &o,
                                 const QString &script,
                                 bool passwordAuth,
                                 int timeoutMs = 30000) const;

    bool ensureLocalTools(QString *error) const;
    bool ensureKeyPair(Options *o, QString *publicKey, QString *error);
    bool identityIsUsable(const QString &identityPath, QString *reason) const;
    bool installPublicKey(const Options &o, const QString &publicKey, QString *error);
    bool ensureSshdPubkeyAuth(const Options &o, QString *error);
    bool verifyKeyLogin(const Options &o, QString *error);
    void logRemoteKeyDiagnostics(const Options &o, const QString &publicKey);
    void appendIdentityArgs(QStringList *args, const Options &o) const;
    bool detectRemote(const Options &o, QString *osName, QString *arch, QString *error);
    bool updateSshConfig(const Options &o, QString *error);
    bool configureRemoteProxyEnvironment(const Options &o, QString *error);

    QString findCodexPackage(const QString &packageDir,
                             const QString &releaseSelector,
                             const QString &arch) const;
    bool ensureCodexPackage(const Options &o,
                            const QString &arch,
                            QString *packagePath,
                            QString *resolvedTag,
                            QString *error);
    bool uploadAndInstallCodex(const Options &o,
                               const QString &packagePath,
                               QString *installedPath,
                               QString *version,
                               QString *error);
    bool syncAuth(const Options &o, QString *error);
    bool verifyCodex(const Options &o, QString *version, QString *error);

    // SSH config helpers.
    QVariantList discoverSshHosts() const;
    QVariantMap resolveSshHost(const QString &alias) const;
    static QString expandHomePath(const QString &path);

    // GitHub release/package helpers.
    HttpResult httpGet(const QUrl &url, int timeoutMs = 30000) const;
    QString curlExecutable() const;
    QStringList curlProxyCandidates() const;
    HttpResult curlHttpGet(const QUrl &url, int timeoutMs, const QString &proxyUrl) const;
    bool curlDownloadToFile(const QUrl &url, const QString &target, int timeoutMs, QString *error) const;
    bool fetchReleaseCatalogSync(bool includePrerelease,
                                 QList<ReleaseInfo> *releases,
                                 QString *error) const;
    bool fetchReleaseSync(const QString &selector, ReleaseInfo *release, QString *error) const;
    bool parseReleaseObject(const QVariantMap &obj, ReleaseInfo *release) const;
    bool downloadReleaseAsset(const ReleaseInfo &release,
                              const QString &arch,
                              const QString &packageDir,
                              QString *savedPath,
                              QString *error);
    QString packagePathFor(const QString &packageDir,
                           const QString &tag,
                           const QString &arch) const;
    static QString packageAssetName(const QString &arch);
    static QString sanitizeTag(const QString &tag);
    static QString humanBytes(qint64 bytes);
    static QString sha256File(const QString &path);

    static QString shellQuote(const QString &value);
    static QString normalizeArch(const QString &arch);
    static QString safeAlias(const QString &value);
    static QString managedBlock(const Options &o);
    static QString proxyProfileBlock(const Options &o);

    bool m_running = false;
    bool m_packageRunning = false;
    QFuture<void> m_future;
    QFuture<void> m_packageFuture;

    QVariantList m_sshHosts;
    QVariantList m_releaseCatalog;
    QVariantList m_localPackages;
};
