#pragma once

#include "../lemoncofighelper.h"
#include <QComboBox>

// Keep the two professional import entry points on the same environment choices.
inline void populateProfessionalImporterHosts(QComboBox *combo)
{
    lemoncofighelper config;
    const QString environment = lemoncofighelper::CurrentHttpEnvironment();
    const QString configured = config.GetAccountUrlByKey(lemoncofighelper::kProImporterApiHostKey).trimmed();
    if (environment.compare("Pro", Qt::CaseInsensitive) == 0) {
        combo->addItem(QStringLiteral("Pro - %1").arg(configured), configured);
        combo->addItem("PT - https://prodaozhangapipt.ningmengyun.com", "https://prodaozhangapipt.ningmengyun.com");
        combo->addItem("KF - https://kfprodaozhangapi.ningmengyun.com", "https://kfprodaozhangapi.ningmengyun.com");
    } else {
        combo->addItem("Dev - https://proimportertest.ningmengyun.com", "https://proimportertest.ningmengyun.com");
        combo->addItem("Dev2 - https://proimportertest2.ningmengyun.com", "https://proimportertest2.ningmengyun.com");
        combo->addItem("Dev3 - https://proimportertest3.ningmengyun.com", "https://proimportertest3.ningmengyun.com");
        // Local is an explicit destination, independent of the login environment.
        combo->addItem("Local - http://localhost:55169", "http://localhost:55169");
        combo->setCurrentIndex(environment == "Dev" ? 0 : environment == "Dev2" ? 1 : 3);
    }
}
