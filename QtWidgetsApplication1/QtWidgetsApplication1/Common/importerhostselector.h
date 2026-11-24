#pragma once

#include "../lemoncofighelper.h"
#include <QComboBox>
#include <QUrl>

// Keeps the primary desktop and CLI importer aligned with the active login.
inline QString primaryProfessionalImporterHost()
{
    QString host = lemoncofighelper::AccountConfigValue(lemoncofighelper::kProImporterApiHostKey);
    if (lemoncofighelper::CurrentHttpEnvironment() == "Dev2") {
        if (host.isEmpty() || QUrl(host).host() == "proimportertest2.ningmengyun.com")
            return QStringLiteral("https://proimportertest2.ningmengyun.com");
    }
    return host;
}

// Keep the two professional import entry points on the same environment choices.
inline void populateProfessionalImporterHosts(QComboBox *combo)
{
    const QString environment = lemoncofighelper::CurrentHttpEnvironment();
    const QString configured = primaryProfessionalImporterHost().trimmed();
    if (environment.compare("Pro", Qt::CaseInsensitive) == 0) {
        combo->addItem(QStringLiteral("Pro - %1").arg(configured), configured);
        combo->addItem("PT - https://prodaozhangapipt.ningmengyun.com", "https://prodaozhangapipt.ningmengyun.com");
        combo->addItem("KF - https://kfprodaozhangapi.ningmengyun.com", "https://kfprodaozhangapi.ningmengyun.com");
    } else {
        combo->addItem("Dev - https://proimportertest.ningmengyun.com", "https://proimportertest.ningmengyun.com");
        const QString test2 = environment == "Dev2" ? configured : QStringLiteral("https://proimportertest2.ningmengyun.com");
        combo->addItem(QStringLiteral("Dev2 - %1").arg(test2), test2);
        combo->addItem("Dev3 - https://proimportertest3.ningmengyun.com", "https://proimportertest3.ningmengyun.com");
        // Local is an explicit destination, independent of the login environment.
        combo->addItem("Local - http://localhost:55169", "http://localhost:55169");
        combo->setCurrentIndex(environment == "Dev" ? 0 : environment == "Dev2" ? 1 : 3);
    }
}
