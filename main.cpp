//MAIN.CPP
#include <QCoreApplication>
#include "server.h"

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "RUS");
    QCoreApplication a(argc, argv);
    Server s;
    return a.exec();
}
