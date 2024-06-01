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
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("messenger.db");

    // Открытие базы данных
    if (!db.open()) {
        qDebug() << "Не удалось открыть базу данных.";
        return;
    }

    //Создание таблиц
    QSqlQuery query;

    //Таблица пользователей
    query.exec("CREATE TABLE IF NOT EXISTS Users ("
               "UserID INTEGER PRIMARY KEY AUTOINCREMENT, "
               "Username TEXT NOT NULL UNIQUE, "
               "Password TEXT NOT NULL, "
               "LastLogin DATETIME)");

    //Таблица контактов
    query.exec("CREATE TABLE IF NOT EXISTS Contacts ("
               "ContactID INTEGER PRIMARY KEY AUTOINCREMENT, "
               "UserID INTEGER NOT NULL, "
               "ContactUserID INTEGER NOT NULL)");

    //Таблица сообщений
    query.exec("CREATE TABLE IF NOT EXISTS Messages ("
               "MessageID INTEGER PRIMARY KEY AUTOINCREMENT, "
               "SenderID INTEGER NOT NULL, "
               "ChatID INTEGER NOT NULL, "
               "Content TEXT NOT NULL, "
               "Timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)");

    //Таблица чатов
    query.exec("CREATE TABLE IF NOT EXISTS Chats ("
               "ChatID INTEGER PRIMARY KEY AUTOINCREMENT, "
               "ParticipantIDs TEXT NOT NULL, "
               "ChatName TEXT)");

    query.exec("SELECT * FROM Users WHERE UserID = 0");
    if (!query.next())
    {
        query.exec("INSERT INTO Users (UserID, Username, Password) VALUES (0, 'server', 'server_password')");
    }
    qDebug() << "База данных успешно подключена.";
}

//Деструктор
Server::~Server()
{
    //Закрытие всех сокетов
    foreach (int userID, clients.keys()) {
        QTcpSocket* socket = clients.value(userID);
        socket->close();
        socket->deleteLater();
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

    //Sockets.push_back(socket);
    clients.insert(0, socket);
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
                    QString str, user;
                    QTime time;
                    in >> time >> str >> user;
                    SendToClient(str, user);
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
                case MessageType::GetContacts:
                {
                    GetContacts();
                    break;
                }
                case MessageType::JoinChat:
                {
                    QStringList usernames;
                    in >> usernames;
                    SendPrivateChat(usernames);
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
    query.prepare("SELECT * FROM users WHERE Username = :login AND Password = :password");
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
    checkQuery.prepare("SELECT * FROM users WHERE Username = :login");
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
    registerQuery.prepare("INSERT INTO users (Username, Password) VALUES (:login, :password)");
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
        slotProcessAuthData(login, password);
    }
}

//Отправка списка пользователей
void Server::GetContacts()
{
    QStringList usernames;
    QSqlQuery query;
    query.exec("SELECT Username FROM Users WHERE UserID != 0");

    while (query.next()) {
        QString username = query.value(0).toString();
        usernames << username;
    }

    Data.clear();
    QDataStream out(&Data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_10);
    out << quint16(0) << MessageType::GetContacts << usernames;

    // Отправляем данные клиенту
    socket->write(Data);
}

//Создание чата
void Server::SendPrivateChat(const QStringList &usernames)
{
    QSqlQuery query;
    // Получаем ID пользователей в порядке возрастания
    QList<int> userIds;
    foreach (const QString &username, usernames) {
        query.prepare("SELECT UserID FROM Users WHERE Username = :username");
        query.bindValue(":username", username);
        if (query.exec() && query.next()) {
            userIds.append(query.value(0).toInt());
        } else {
            qDebug() << "Пользователь не найден:" << username;
            return;
        }
    }

    // Сортируем ID пользователей
    std::sort(userIds.begin(), userIds.end());

    // Создаем строку ParticipantIDs
    QStringList participantIdsStrList;
    foreach (int userId, userIds) {
        participantIdsStrList << QString::number(userId);
    }
    QString participantIdsStr = participantIdsStrList.join(',');

    //Проверяем, существует ли уже чат с такими участниками
    query.prepare("SELECT * FROM Chats WHERE ParticipantIDs = :participantIds");
    query.bindValue(":participantIds", participantIdsStr);
    if (query.exec() && query.next()) {
        int chatID = query.value(0).toInt(); // Получаем ID чата
        ShowChat(chatID);
        return;
    }

    //Добавляем новый чат в таблицу Chats
    query.prepare("INSERT INTO Chats (ParticipantIDs, ChatName) VALUES (:participantIds, 'Новый чат')");
    query.bindValue(":participantIds", participantIdsStr);
    if (!query.exec()) {
        qDebug() << "Не удалось создать чат:" << query.lastError().text();
        return;
    }

    //Получаем ChatID новосозданного чата
    int newChatId = query.lastInsertId().toInt();

    //Добавляем сообщение от сервера об успешном создании чата
    query.prepare("INSERT INTO Messages (SenderID, ChatID, Content) VALUES (:senderId, :chatId, :content)");
    query.bindValue(":senderId", 0);
    query.bindValue(":chatId", newChatId);
    query.bindValue(":content", "Чат успешно создан.");
    if (!query.exec()) {
        qDebug() << "Не удалось добавить сообщение от сервера:" << query.lastError().text();
        return;
    }
    ShowChat(newChatId);
}

//Отправка истории сообщений
void Server::ShowChat(int ChatID)
{
    QSqlQuery query;
    query.prepare("SELECT SenderID, Content, Timestamp FROM Messages WHERE ChatID = :chatID");
    query.bindValue(":chatID", ChatID);
    if (!query.exec()) {
        qDebug() << "Ошибка при выборке сообщений: " << query.lastError().text();
        return;
    }
    QStringList messages;
    Data.clear();
    QDataStream out(&Data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_2);
    while (query.next()) {
        int senderID = query.value(0).toInt();
        QString content = query.value(1).toString();
        QString timestamp = query.value(2).toDateTime().toString(Qt::ISODate).replace('T', ' ');

        //Получаем имя пользователя по SenderID
        QSqlQuery userQuery;
        userQuery.prepare("SELECT Username FROM Users WHERE UserID = :userID");
        userQuery.bindValue(":userID", senderID);
        QString username;
        if (userQuery.exec() && userQuery.next()) {
            username = userQuery.value(0).toString();
        } else {
            qDebug() << "Ошибка при получении имени пользователя: " << userQuery.lastError().text();
            continue; //Пропускаем сообщение, если не удалось получить имя пользователя
        }

        //Формируем строку сообщения с именем пользователя и временем отправки
        QString messageString = QString("%1 [%2] %3").arg(timestamp).arg(username).arg(content);
        qDebug() << messageString;
        //Добавляем сообщение в QByteArray
        messages.append(messageString + "\n");
    }

    //Отправляем собранный QByteArray через сокет
    out << quint16(0) << MessageType::JoinChat << messages;
    socket->write(Data);
}

//Отправка сообщения в общий чат
void Server::SendToClient(QString str, QString user)
{
    Data.clear();
    QDataStream out(&Data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_2);
    out << quint16(0) << MessageType::Message << QTime::currentTime() << user << str;
    out.device()->seek(0);
    out << quint16(Data.size() - sizeof(quint16));
    // Отправка данных всем подключенным клиентам
    foreach (QTcpSocket* clientSocket, clients.values()) {
        if (clientSocket->state() == QAbstractSocket::ConnectedState) {
            clientSocket->write(Data);
        }
    }
}
