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
    RegisterDone,
    RegisterError
};

#endif // MESSAGETYPE_H
