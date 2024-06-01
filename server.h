//SERVER.H
#ifndef SERVER_H
#define SERVER_H
#include "MessageType.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>
#include <QTime>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

class Server : public QTcpServer
{
    Q_OBJECT

public:
    Server();
    ~Server();
    QTcpSocket* socket;

private:
    QMap<int, QTcpSocket *> clients; //Сопоставление ID пользователя и сокета
    QByteArray Data;
    QSqlDatabase db;
    void SendToClient(QString str, QString user);
    void slotProcessAuthData(QString login, QString password);
    void slotRegisterUser(QString login, QString password);
    void GetContacts();
    void SendPrivateChat(const QStringList &usernames);
    void ShowChat(int ChatID);
    quint16 nextBlockSize;
public slots:
    void incomingConnection(qintptr socketDescriptor);
    void slotReadyRead();



};

#endif // SERVER_H
