#include "LogViewer.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    CLogViewer w("TestLog");
    w.show();
    return a.exec();
}
