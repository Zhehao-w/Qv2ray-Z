#pragma once

#include <QString>

enum class StreamTransportEditor
{
    Invalid,
    Tcp,
    WebSocket,
    Kcp,
    DomainSocket,
    Grpc,
    Xhttp
};

inline StreamTransportEditor StreamTransportEditorForNetwork(const QString &network)
{
    if (network == "tcp")
        return StreamTransportEditor::Tcp;
    if (network == "ws")
        return StreamTransportEditor::WebSocket;
    if (network == "kcp")
        return StreamTransportEditor::Kcp;
    if (network == "domainsocket")
        return StreamTransportEditor::DomainSocket;
    if (network == "grpc")
        return StreamTransportEditor::Grpc;
    if (network == "xhttp")
        return StreamTransportEditor::Xhttp;
    return StreamTransportEditor::Invalid;
}

inline int StreamSecurityEditorIndexForValue(const QString &security)
{
    if (security.isEmpty() || security == "none")
        return 0;
    if (security == "tls")
        return 1;
    if (security == "reality")
        return 2;
    return -1;
}
