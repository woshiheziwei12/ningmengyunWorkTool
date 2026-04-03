#ifndef LEMONCOFIGHELPER_H
#define LEMONCOFIGHELPER_H

#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QCoreApplication>
#include <QDebug>

class lemoncofighelper
{
private:
    QMap<QString, QString> m_mapAccountConfig;

public:
    lemoncofighelper();
    QString GetAccountUrlByKey(QString key);

    QString m_strConfigRoot;
    QString m_strAccountJsonName;

private:
    void InitAccountConfig();
};

#endif // LEMONCOFIGHELPER_H
