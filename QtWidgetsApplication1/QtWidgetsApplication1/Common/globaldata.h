#ifndef GLOBALDATA_H
#define GLOBALDATA_H

#include "usertoken.h"

class GlobalData
{
public:
    GlobalData();

public:
    bool m_bLogin;
    UserToken m_userToken;
    QString m_strUserName;
    QString m_strSN;
    QString m_strStyleName;

    // 账套 API 所需字段
    int m_nServiceId;
    QString m_strAppAsId;
    QString m_strNingMengCookie;
};

#endif // GLOBALDATA_H
