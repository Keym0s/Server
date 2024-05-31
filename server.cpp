//SERVER.CPP
#include "server.h"

//Конструктор
Server::Server()
{
    if(this->listen(QHostAddress::Any, 2323))
    {
        qDebug() << "start";
    }
    else
    {
        qDebug() << "error";
    }
    nextBlockSize = 0;

    //БД
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("users.db");

    if (!db.open())
    {
        qDebug() << "Ошибка открытия базы данных: " << db.lastError().text();
    }
    else
    {
        QSqlQuery query;
        query.exec("CREATE TABLE IF NOT EXISTS users ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "login TEXT NOT NULL, "
                   "password TEXT NOT NULL)");
    }
}

//Деструктор
Server::~Server()
{
    //Закрытие всех сокетов
    for(QTcpSocket* socket : Sockets)
    {
        socket->close();
    }

    //Закрытие соединения с базой данных
    QSqlDatabase::database().close();
    QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
}

//Подключение клиента
void Server::incomingConnection(qintptr socketDescriptor)
{
    socket = new QTcpSocket;
    socket->setSocketDescriptor(socketDescriptor);
    connect(socket,&QTcpSocket::readyRead, this, &Server::slotReadyRead);
    connect(socket,&QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    Sockets.push_back(socket);
    qDebug() << "client connected" << socketDescriptor;
}


//Чтение входящего запроса
void Server::slotReadyRead()
{
    socket = (QTcpSocket*)sender();
    QDataStream in(socket);
    in.setVersion(QDataStream::Qt_6_2);
    if(in.status() == QDataStream::Ok)
    {
        for(;;)
        {
            if(nextBlockSize == 0)
            {
                if(socket->bytesAvailable() < 2)
                {
                    break;
                }
                in >> nextBlockSize;
            }
            if (socket->bytesAvailable() < nextBlockSize)
            {
                break;
            }

            MessageType type;
            in >> type;

            switch (type)
            {
                case MessageType::Message:
                {
                    qDebug() << "Сообщение принято от" << socketDescriptor();
                    QString str;
                    QTime time;
                    in >> time >> str;
                    SendToClient(str);
                    break;
                }
                case MessageType::AuthData:
                {
                    QString login,password;
                    in >> login >> password;
                    slotProcessAuthData(login, password);
                    break;
                }
                case MessageType::Register:
                {
                    QString login,password;
                    in >> login >> password;
                    slotRegisterUser(login, password);
                    break;
                }
            }
            nextBlockSize = 0;
            break;
        }
    }
    else
    {
        qDebug() << "DataStream error";
    }
}

//Обработка информации при входе
void Server::slotProcessAuthData(QString login, QString password)
{
    QSqlQuery query;
    query.prepare("SELECT * FROM users WHERE login = :login AND password = :password");
    query.bindValue(":login", login);
    query.bindValue(":password", password);
    query.exec();

    if (query.next())
    {
        //Пользователь найден
        qDebug() << "Пользователь авторизован: " << login;

        //Отправить подтверждение клиенту
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthDone << login;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
        return;
    }
    else
    {
        //Пользователь не найден
        QString str;
        str = "Неверный логин или пароль";
        qDebug() << str << " для:" << login;

        //Отправить сообщение об ошибке клиенту
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthError << str;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
        return;
    }
}

//Обработка информации при регистрации
void Server::slotRegisterUser(QString login, QString password)
{
    //Проверка на существование пользователя с таким логином
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT * FROM users WHERE login = :login");
    checkQuery.bindValue(":login", login);
    if (!checkQuery.exec())
    {
        QString str;
        str = "Ошибка при проверке пользователя: ";
        qDebug() << str << checkQuery.lastError().text();
        //Отправить сообщение об ошибке клиенту
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthError << str;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
        return;
    }

    if (checkQuery.next())
    {
        QString str;
        str = "Пользователь с таким именем уже существует: ";
        qDebug() << str << login;
        // Отправьте сообщение клиенту о том, что логин уже занят
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthError << str;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
        return;
    }

    // Если пользователь не найден, продолжаем регистрацию
    QSqlQuery registerQuery;
    registerQuery.prepare("INSERT INTO users (login, password) VALUES (:login, :password)");
    registerQuery.bindValue(":login", login);
    registerQuery.bindValue(":password", password);

    if (!registerQuery.exec())
    {
        QString str;
        str = "Ошибка при регистрации пользователя: ";
        qDebug() << str << registerQuery.lastError().text();
        // Отправьте сообщение об ошибке клиенту
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthError << str;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
    }
    else
    {
        qDebug() << "Пользователь успешно зарегистрирован: " << login;
        // Отправьте подтверждение клиенту
        Data.clear();
        QDataStream out(&Data, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << quint16(0) << MessageType::AuthDone << login;
        out.device()->seek(0);
        out << quint16(Data.size() - sizeof(quint16));
        socket->write(Data);
    }
}

//Отправка сообщения в общий чат
void Server::SendToClient(QString str)
{
    Data.clear();
    QDataStream out(&Data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_2);
    out << quint16(0) << MessageType::Message << QTime::currentTime() << str;
    out.device()->seek(0);
    out << quint16(Data.size() - sizeof(quint16));
    //socket->write(Data);
    for(int i = 0; i < Sockets.size(); i++)
    {
        Sockets[i]->write(Data);
    }
}
