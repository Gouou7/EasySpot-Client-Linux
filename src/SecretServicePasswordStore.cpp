#include "SecretServicePasswordStore.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QMap>
#include <QTimer>

namespace SecretServiceDbus
{
using SecretAttributes = QMap<QString, QString>;

struct SecretStruct
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &argument, const SecretStruct &secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SecretStruct &secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}
}

Q_DECLARE_METATYPE(SecretServiceDbus::SecretStruct)

namespace
{
struct SearchResults
{
    QList<QDBusObjectPath> unlocked;
    QList<QDBusObjectPath> locked;
};

class PromptWaiter final : public QObject
{
    Q_OBJECT

public:
    explicit PromptWaiter(QEventLoop *eventLoop)
        : m_eventLoop(eventLoop)
    {
    }

    bool dismissed() const { return m_dismissed; }
    bool completed() const { return m_completed; }

public slots:
    void onCompleted(bool dismissed, const QDBusVariant &)
    {
        m_completed = true;
        m_dismissed = dismissed;
        m_eventLoop->quit();
    }

private:
    QEventLoop *m_eventLoop = nullptr;
    bool m_completed = false;
    bool m_dismissed = true;
};

QDBusInterface secretServiceInterface()
{
    return QDBusInterface(QStringLiteral("org.freedesktop.secrets"),
                          QStringLiteral("/org/freedesktop/secrets"),
                          QStringLiteral("org.freedesktop.Secret.Service"),
                          QDBusConnection::sessionBus());
}

QDBusInterface secretServicePropertiesInterface()
{
    return QDBusInterface(QStringLiteral("org.freedesktop.secrets"),
                          QStringLiteral("/org/freedesktop/secrets"),
                          QStringLiteral("org.freedesktop.DBus.Properties"),
                          QDBusConnection::sessionBus());
}

QString formatDbusError(const QDBusMessage &reply, const QString &fallback)
{
    if (reply.type() != QDBusMessage::ErrorMessage) {
        return fallback;
    }

    if (reply.errorMessage().isEmpty()) {
        return fallback;
    }

    return QStringLiteral("%1 (%2)").arg(fallback, reply.errorMessage());
}

bool promptIfNeeded(const QDBusObjectPath &promptPath, const QString &failureMessage, QString *errorMessage = nullptr)
{
    if (promptPath.path().isEmpty() || promptPath.path() == QStringLiteral("/")) {
        return true;
    }

    QEventLoop eventLoop;
    PromptWaiter waiter(&eventLoop);
    const bool connected = QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.secrets"),
                                                                 promptPath.path(),
                                                                 QStringLiteral("org.freedesktop.Secret.Prompt"),
                                                                 QStringLiteral("Completed"),
                                                                 &waiter,
                                                                 SLOT(onCompleted(bool,QDBusVariant)));
    if (!connected) {
        if (errorMessage) {
            *errorMessage = failureMessage;
        }
        return false;
    }

    QDBusInterface prompt(QStringLiteral("org.freedesktop.secrets"),
                          promptPath.path(),
                          QStringLiteral("org.freedesktop.Secret.Prompt"),
                          QDBusConnection::sessionBus());
    const QDBusMessage promptReply = prompt.call(QStringLiteral("Prompt"), QString());
    if (promptReply.type() != QDBusMessage::ReplyMessage) {
        if (errorMessage) {
            *errorMessage = formatDbusError(promptReply, failureMessage);
        }
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.secrets"),
                                                 promptPath.path(),
                                                 QStringLiteral("org.freedesktop.Secret.Prompt"),
                                                 QStringLiteral("Completed"),
                                                 &waiter,
                                                 SLOT(onCompleted(bool,QDBusVariant)));
        return false;
    }

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    timeout.start(120000);
    eventLoop.exec();

    QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.secrets"),
                                             promptPath.path(),
                                             QStringLiteral("org.freedesktop.Secret.Prompt"),
                                             QStringLiteral("Completed"),
                                             &waiter,
                                             SLOT(onCompleted(bool,QDBusVariant)));

    if (!waiter.completed() || waiter.dismissed()) {
        if (errorMessage) {
            *errorMessage = failureMessage;
        }
        return false;
    }

    return true;
}

void ensureSecretServiceDbusTypesRegistered()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<SecretServiceDbus::SecretAttributes>();
        qDBusRegisterMetaType<SecretServiceDbus::SecretStruct>();
        return true;
    }();
    Q_UNUSED(registered);
}

QString openPlainSession(QString *errorMessage = nullptr)
{
    QDBusInterface secrets = secretServiceInterface();
    if (!secrets.isValid()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("KWallet or another Freedesktop Secret Service provider is not available.");
        }
        return {};
    }

    QDBusMessage reply = secrets.call(QStringLiteral("OpenSession"),
                                      QStringLiteral("plain"),
                                      QVariant::fromValue(QDBusVariant(QString())));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (errorMessage) {
            *errorMessage = formatDbusError(reply,
                                            QObject::tr("Secret Service did not open a session for EasySpot."));
        }
        return {};
    }

    return qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
}

SecretServiceDbus::SecretAttributes easySpotAttributes()
{
    return {
        {QStringLiteral("application"), QStringLiteral("easyspot-linux-client")},
        {QStringLiteral("schema"), QStringLiteral("xyz.ggorg.easyspot.hotspot-password")},
    };
}

QString defaultCollectionPath(QString *errorMessage = nullptr)
{
    QDBusInterface secrets = secretServiceInterface();
    if (!secrets.isValid()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("KWallet or another Freedesktop Secret Service provider is not available.");
        }
        return {};
    }

    const QDBusMessage aliasReply = secrets.call(QStringLiteral("ReadAlias"), QStringLiteral("default"));
    if (aliasReply.type() == QDBusMessage::ReplyMessage && !aliasReply.arguments().isEmpty()) {
        const QString aliasPath = qdbus_cast<QDBusObjectPath>(aliasReply.arguments().first()).path();
        if (!aliasPath.isEmpty() && aliasPath != QStringLiteral("/")) {
            return aliasPath;
        }
    }

    QDBusInterface properties = secretServicePropertiesInterface();
    const QDBusReply<QVariant> collectionsReply = properties.call(QStringLiteral("Get"),
                                                                  QStringLiteral("org.freedesktop.Secret.Service"),
                                                                  QStringLiteral("Collections"));
    if (collectionsReply.isValid()) {
        const auto collections = qdbus_cast<QList<QDBusObjectPath>>(collectionsReply.value().value<QDBusArgument>());
        if (!collections.isEmpty()) {
            return collections.first().path();
        }
    }

    if (errorMessage) {
        *errorMessage = QObject::tr("Secret Service did not expose a writable default collection.");
    }
    return {};
}

SearchResults searchItems()
{
    QDBusInterface secrets = secretServiceInterface();
    if (!secrets.isValid()) {
        return {};
    }

    QDBusMessage reply = secrets.call(QStringLiteral("SearchItems"), QVariant::fromValue(easySpotAttributes()));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    SearchResults results;
    results.unlocked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().first());
    if (reply.arguments().size() > 1) {
        results.locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
    }
    return results;
}

bool unlockItems(const QList<QDBusObjectPath> &items, QString *errorMessage = nullptr)
{
    if (items.isEmpty()) {
        return true;
    }

    QDBusInterface secrets = secretServiceInterface();
    if (!secrets.isValid()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("KWallet or another Freedesktop Secret Service provider is not available.");
        }
        return false;
    }

    const QDBusMessage reply = secrets.call(QStringLiteral("Unlock"), QVariant::fromValue(items));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (errorMessage) {
            *errorMessage = formatDbusError(reply, QObject::tr("KWallet did not unlock the EasySpot password."));
        }
        return false;
    }

    const auto promptPath = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
    return promptIfNeeded(promptPath, QObject::tr("KWallet password unlock was cancelled."), errorMessage);
}
}

SecretServicePasswordStore::SecretServicePasswordStore(QObject *parent)
    : PasswordStore(parent)
{
    ensureSecretServiceDbusTypesRegistered();
}

bool SecretServicePasswordStore::hasPassword()
{
    const auto items = searchItems();
    return !items.unlocked.isEmpty() || !items.locked.isEmpty();
}

QString SecretServicePasswordStore::loadPassword()
{
    auto items = searchItems();
    if (items.unlocked.isEmpty() && !items.locked.isEmpty()) {
        QString errorMessage;
        if (!unlockItems(items.locked, &errorMessage)) {
            return {};
        }
        items = searchItems();
    }

    if (items.unlocked.isEmpty()) {
        return {};
    }

    const auto sessionPath = openPlainSession();
    if (sessionPath.isEmpty()) {
        return {};
    }

    const auto itemPath = items.unlocked.first().path();
    QDBusInterface item(QStringLiteral("org.freedesktop.secrets"),
                        itemPath,
                        QStringLiteral("org.freedesktop.Secret.Item"),
                        QDBusConnection::sessionBus());
    QDBusMessage secretReply = item.call(QStringLiteral("GetSecret"), QVariant::fromValue(QDBusObjectPath(sessionPath)));
    if (secretReply.type() != QDBusMessage::ReplyMessage || secretReply.arguments().isEmpty()) {
        return {};
    }

    SecretServiceDbus::SecretStruct secret;
    secretReply.arguments().first().value<QDBusArgument>() >> secret;
    return QString::fromUtf8(secret.value);
}

bool SecretServicePasswordStore::savePassword(const QString &password, QString *errorMessage)
{
    const auto sessionPath = openPlainSession(errorMessage);
    if (sessionPath.isEmpty()) {
        return false;
    }

    const auto collectionPath = defaultCollectionPath(errorMessage);
    if (collectionPath.isEmpty()) {
        return false;
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                      QStringLiteral("EasySpot hotspot password"));
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"), QVariant::fromValue(easySpotAttributes()));

    const SecretServiceDbus::SecretStruct secret{
        .session = QDBusObjectPath(sessionPath),
        .parameters = QByteArray(),
        .value = password.toUtf8(),
        .contentType = QStringLiteral("text/plain"),
    };

    QDBusInterface collection(QStringLiteral("org.freedesktop.secrets"),
                              collectionPath,
                              QStringLiteral("org.freedesktop.Secret.Collection"),
                              QDBusConnection::sessionBus());
    QDBusMessage reply = collection.call(QStringLiteral("CreateItem"), properties, QVariant::fromValue(secret), true);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (errorMessage) {
            *errorMessage = formatDbusError(reply,
                                            tr("The hotspot password could not be saved in KWallet or Secret Service."));
        }
        return false;
    }

    const auto promptPath = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
    return promptIfNeeded(promptPath, tr("KWallet password save was cancelled."), errorMessage);
}

bool SecretServicePasswordStore::clearPassword(QString *)
{
    const auto items = searchItems();
    const auto allItems = items.unlocked + items.locked;
    for (const auto &item : allItems) {
        const auto itemPath = item.path();
        QDBusInterface secretItem(QStringLiteral("org.freedesktop.secrets"),
                                  itemPath,
                                  QStringLiteral("org.freedesktop.Secret.Item"),
                                  QDBusConnection::sessionBus());
        const QDBusMessage reply = secretItem.call(QStringLiteral("Delete"));
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            promptIfNeeded(qdbus_cast<QDBusObjectPath>(reply.arguments().first()),
                           tr("KWallet password deletion was cancelled."));
        }
    }
    return true;
}

QString SecretServicePasswordStore::serviceName()
{
    return QStringLiteral("easyspot-linux-client");
}

QString SecretServicePasswordStore::schemaName()
{
    return QStringLiteral("xyz.ggorg.easyspot.hotspot-password");
}

#include "SecretServicePasswordStore.moc"
