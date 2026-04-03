#include "lemoncofighelper.h"

lemoncofighelper::lemoncofighelper()
{
    m_strConfigRoot = ":/Resource/http/Pro/";
    m_strAccountJsonName = "Account.json";

    InitAccountConfig();
}

QString lemoncofighelper::GetAccountUrlByKey(QString key)
{
    if (m_mapAccountConfig.keys().contains(key))
    {
        return m_mapAccountConfig[key];
    }
    return "";
}

void lemoncofighelper::InitAccountConfig()
{
    QByteArray strJsonData = "";
    QString filep = m_strConfigRoot + m_strAccountJsonName;
    qDebug() << filep;
    QFile file(filep);
    if (file.open(QFile::ReadOnly)) {
        strJsonData = file.readAll();
        file.close();
    }
    QString jsonData(strJsonData);
    jsonData = jsonData.remove("\r").remove("\n").trimmed();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(jsonData.toUtf8(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        qDebug() << QString("resolve Json failed");
    }

    if (document.isObject()) {
        QJsonObject rootObj = document.object();
        QStringList keys = rootObj.keys();
        int keySize = keys.size();
        if (keySize > 0)
        {
            for (int i = 0; i < keySize; i++)
            {
                QString key = keys.at(i);
                QString value = rootObj.value(key).toString();
                m_mapAccountConfig[key] = value;
            }
        }
    }
}
