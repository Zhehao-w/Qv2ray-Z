#include "src/ui/widgets/widgets/StreamSettingsTransport.hpp"

#include "catch.hpp"

TEST_CASE("Stream transport editor mapping is explicit and index independent")
{
    REQUIRE(StreamTransportEditorForNetwork("tcp") == StreamTransportEditor::Tcp);
    REQUIRE(StreamTransportEditorForNetwork("http") == StreamTransportEditor::Http);
    REQUIRE(StreamTransportEditorForNetwork("ws") == StreamTransportEditor::WebSocket);
    REQUIRE(StreamTransportEditorForNetwork("kcp") == StreamTransportEditor::Kcp);
    REQUIRE(StreamTransportEditorForNetwork("domainsocket") == StreamTransportEditor::DomainSocket);
    REQUIRE(StreamTransportEditorForNetwork("quic") == StreamTransportEditor::Quic);
    REQUIRE(StreamTransportEditorForNetwork("grpc") == StreamTransportEditor::Grpc);
    REQUIRE(StreamTransportEditorForNetwork("xhttp") == StreamTransportEditor::Xhttp);
    REQUIRE(StreamTransportEditorForNetwork("unknown") == StreamTransportEditor::Invalid);
}

TEST_CASE("Stream security editor mapping preserves existing none semantics and fails closed for unknown values")
{
    REQUIRE(StreamSecurityEditorIndexForValue("") == 0);
    REQUIRE(StreamSecurityEditorIndexForValue("none") == 0);
    REQUIRE(StreamSecurityEditorIndexForValue("tls") == 1);
    REQUIRE(StreamSecurityEditorIndexForValue("reality") == 2);
    REQUIRE(StreamSecurityEditorIndexForValue("future-security") == -1);
}
