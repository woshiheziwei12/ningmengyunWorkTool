#ifndef USERTOKEN_H
#define USERTOKEN_H

#include <QObject>

class UserToken : public QObject
{
    Q_OBJECT
public:
    explicit UserToken(QObject *parent = nullptr);

    void SetData(QString accessToken, QString refreshToken, int expirenIn);
    void SetAccessToken(QString accessToken);
    void SetRefreshToken(QString refreshToken);
    void SetExpirenIn(int expirenIn);

public:
    QString m_strAccessToken;
    QString m_strRefreshToken;
    int m_iExpireIn;
    QString m_strTokenType;
};

#endif // USERTOKEN_H
