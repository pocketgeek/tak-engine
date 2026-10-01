#pragma once
#include <memory>
#include <string>

struct ssl_ctx_st;
struct ssl_st;
namespace tak::net {
bool attachTlsSocket(ssl_st* ssl,int fd);
// OpenSSL is statically linked. Certificates/keys stay outside the executable.
struct TlsContext {
    ssl_ctx_st* handle=nullptr;
    ~TlsContext();
    static std::shared_ptr<TlsContext> server(const std::string& certificate,const std::string& key);
    static std::shared_ptr<TlsContext> client();
};
}
