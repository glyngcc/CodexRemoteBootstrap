#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <cstdio>

#include "bootstrapcontroller.h"

int main(int argc, char *argv[])
{
    // OpenSSH launches SSH_ASKPASS with the prompt text as argv[1].
    // The environment flag lets the same executable act as a tiny askpass
    // helper without ever writing the password to a temporary file.
    if (qEnvironmentVariableIsSet("CRB_ASKPASS_MODE")) {
        const QByteArray password = qgetenv("CRB_SSH_PASSWORD");
        std::fwrite(password.constData(), 1, static_cast<size_t>(password.size()), stdout);
        std::fputc('\n', stdout);
        return 0;
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("CodexRemoteBootstrap"));
    QCoreApplication::setOrganizationName(QStringLiteral("CodexRemoteBootstrap"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    BootstrapController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("bootstrapController"), &controller);
    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &app,
                     [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("CodexRemoteBootstrap", "Main");

    return app.exec();
}
