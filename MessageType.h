//MESSAGETYPE.H
#ifndef MESSAGETYPE_H
#define MESSAGETYPE_H

enum class MessageType
{
    Message,
    AuthData,
    AuthDone,
    AuthError,
    Register,
    GetContacts,
    GetChats,
    JoinChat,
    JoinGroupChat,
    ShowGroupChat
};

#endif // MESSAGETYPE_H
