#include "QtWidgetsApplication1.h"
#include "startupwindow.h"
#include "Common/globaldata.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    GlobalData globalData;

    StartUpWindow loginWindow(&globalData);
    loginWindow.exec();

    if (!globalData.m_bLogin)
    {
        return 0;
    }

    QtWidgetsApplication1 window(&globalData);
    window.show();
    return app.exec();
}
