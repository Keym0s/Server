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
    // Закрытие всех сокетов
    foreach (QTcpSocket* socket, clients.keys()) {
        // Закрытие сокета
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
    connect(socket, &QTcpSocket::readyRead, this, &Server::slotReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &Server::clientDisconnected);

    ClientInfo clientInfo;
    clientInfo.userID = -1;
    clientInfo.chatID = -1;

    clients.insert(socket, clientInfo); // Вставка пары сокет-информация о клиенте в QMap
    qDebug() << "Клиент подключен" << socketDescriptor;
}

//Отключения клиента
void Server::clientDisconnected()
{
    if (socket) {
        clients.remove(socket); //Удаление информации о клиенте из QMap
        socket->deleteLater(); //Запланировать удаление объекта сокета
        qDebug() << "Клиент отключен";
    }
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
                    in >> str;
                    ReceiveMessage(str);
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
                case MessageType::GetChats:
                {
                    GetChats();
                    break;
                }
                case MessageType::JoinChat:
                {
                    QStringList usernames;
                    in >> usernames;
                    SendPrivateChat(usernames);
                    break;
                }
                case MessageType::JoinGroupChat:
                {
                    QStringList usernames;
                    QString chat;
                    foreach (const QString &username, usernames) {
                        qDebug() << username;
                    }
                    in >> usernames >> chat;
                    SendGroupChat(usernames, chat);
                    break;
                }
                case MessageType::ShowGroupChat:
                {
                    QString chat;
                    in >> chat;
                    QSqlQuery query;
                    query.prepare("SELECT ChatID FROM Chats WHERE ChatName = :chatName");
                    query.bindValue(":chatName", chat); // Замените название_чата на фактическое название интересующего вас чата
                    if (!query.exec()) {
                        qDebug() << "Ошибка при поиске ID чата: " << query.lastError().text();
                    } else {
                        if (query.next())
                        {
                            int chatID = query.value(0).toInt();
                            qDebug() << "ID чата: " << chatID;
                            ShowChat(chatID);
                        }
                    }
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
        //Получаем userID из результатов запроса
        int userID = query.value("UserID").toInt();

        //Пользователь найден
        qDebug() << "Пользователь авторизован: " << login << " ID:" << userID;

        //Находим сокет, связанный с этим логином

        //Проверяем, есть ли такой сокет в QMap
        if (clients.contains(socket)) {

            //Извлекаем текущую информацию о клиенте
            ClientInfo& clientInfo = clients[socket];

            //Обновляем userID
            clientInfo.userID = userID;
            clientInfo.chatID = 0;
        }

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
    int currentUserId = clients.value(socket).userID;
    query.prepare("SELECT Username FROM Users WHERE UserID != :currentUserId AND UserID != 0");
    query.bindValue(":currentUserId", currentUserId);
    if (!query.exec()) {
        qDebug() << "Ошибка при выборке контактов: " << query.lastError().text();
        return;
    }

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

//Отправка списка чатов
void Server::GetChats()
{
    int userID = clients.value(socket).userID;
    QStringList chatsName;
    //Запрос на выборку названий чатов, где участвует userID и количество участников больше 2
    QString queryString = QString(
                              "SELECT ChatName FROM Chats "
                              "WHERE ParticipantIDs LIKE '%%1%' AND "
                              "(LENGTH(ParticipantIDs) - LENGTH(REPLACE(ParticipantIDs, ',', '')) + 1) > 2"
                              ).arg(userID);

    QSqlQuery query;
    if(query.exec(queryString)) {
        while(query.next()) {
            QString chatName = query.value(0).toString(); //Получаем название чата
            chatsName.append(chatName); //Добавляем название в список названий чатов
        }
    }
    Data.clear();
    QDataStream out(&Data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_10);
    out << quint16(0) << MessageType::GetChats << chatsName;

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
    //Проверяем, есть ли такой сокет в QMap
    if (clients.contains(socket)) {

        //Извлекаем текущую информацию о клиенте
        ClientInfo& clientInfo = clients[socket];

        //Обновляем chatID
        clientInfo.chatID = ChatID;
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
        QString messageString = QString("%1 [%2]: %3").arg(timestamp).arg(username).arg(content);
        //Добавляем сообщение в QByteArray
        messages.append(messageString);
    }

    QSqlQuery chatQuery;
    chatQuery.prepare("SELECT ParticipantIDs FROM Chats WHERE ChatID = :chatID");
    chatQuery.bindValue(":chatID", ChatID);
    QStringList participantUsernames;
    if (chatQuery.exec() && chatQuery.next()) {
        QString participantIDs = chatQuery.value(0).toString();
        QStringList userIDList = participantIDs.split(',');

        // Получаем имена пользователей по их UserID
        foreach (const QString &userID, userIDList) {
            QSqlQuery userQuery;
            userQuery.prepare("SELECT Username FROM Users WHERE UserID = :userID");
            userQuery.bindValue(":userID", userID.trimmed());
            if (userQuery.exec() && userQuery.next()) {
                participantUsernames.append(userQuery.value(0).toString());
            }
        }
    }
    //Отправляем собранный QByteArray через сокет
    out << quint16(0) << MessageType::JoinChat << messages << participantUsernames;
    socket->write(Data);
}

//Создание группового чата
void Server::SendGroupChat(const QStringList &usernames, const QString &chatname)
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
    query.prepare("INSERT INTO Chats (ParticipantIDs, ChatName) VALUES (:participantIds, :chatname)");
    query.bindValue(":participantIds", participantIdsStr);
    query.bindValue(":chatname", chatname);
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

//Занесение сообщения в базу данных
void Server::ReceiveMessage(QString message)
{

    QSqlQuery query;
    ClientInfo clientInfo = clients.value(socket);
    int senderID = clientInfo.userID;
    int chatID = clientInfo.chatID;
    query.prepare("INSERT INTO Messages (SenderID, ChatID, Content) VALUES (:senderID, :chatID, :content)");
    query.bindValue(":senderID", senderID);
    query.bindValue(":chatID", chatID);
    query.bindValue(":content", message);
    if (!query.exec()) {
        qDebug() << "Ошибка при добавлении сообщения в базу данных:" << query.lastError();
        return;
    }

    //Получение MessageID последнего вставленного сообщения
    int messageID = query.lastInsertId().toInt();

    //Получение имени пользователя и временной метки из базы данных
    query.prepare("SELECT Username, Timestamp FROM Users, Messages WHERE Messages.MessageID = :messageID AND Users.UserID = Messages.SenderID");
    query.bindValue(":messageID", messageID);
    if (!query.exec()) {
        qDebug() << "Ошибка при получении данных из базы данных:" << query.lastError();
        return;
    }

    QString username, timestamp;
    if (query.next()) {
        username = query.value("Username").toString();
        timestamp = query.value("Timestamp").toString();
    }

    //Формируем строку сообщения с именем пользователя и временем отправки
    QString messageString = QString("%1 [%2]: %3").arg(timestamp).arg(username).arg(message);

    //Отправка сообщения всем клиентам в чате
    foreach (QTcpSocket *socket, clients.keys()) {
        ClientInfo clientInfo = clients.value(socket);
        if (clientInfo.chatID == chatID) {
            Data.clear();
            QDataStream out(&Data, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_2);
            out << quint16(0) << MessageType::Message << messageString;
            out.device()->seek(0);
            out << quint16(Data.size() - sizeof(quint16));
            socket->write(Data);
        }
    }
}
