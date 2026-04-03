#include "usertoken.h"

UserToken::UserToken(QObject *parent) : QObject(parent)
{
    m_strTokenType = "bearer";
}

void UserToken::SetData(QString accessToken, QString refreshToken, int expirenIn)
{
    m_strAccessToken = accessToken;
    m_strRefreshToken = refreshToken;
    m_iExpireIn = expirenIn;
}

void UserToken::SetAccessToken(QString accessToken)
{
    m_strAccessToken = accessToken;
}

void UserToken::SetRefreshToken(QString refreshToken)
{
    m_strRefreshToken = refreshToken;
}

void UserToken::SetExpirenIn(int expirenIn)
{
    m_iExpireIn = expirenIn;
}
