#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "net/netcompat.h"
#include "net/conn.h"
#include "server/acme_internal.h"
#include "server/acme.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace tak::net;
using tak::srv::acme_detail::Challenge;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
uint16_t unusedPort() {
    int fd=int(socket(AF_INET,SOCK_STREAM,0));check(fd>=0,"socket");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr)==0,"bind");
    socklen_t n=sizeof addr;check(getsockname(fd,reinterpret_cast<sockaddr*>(&addr),&n)==0,"getsockname");sockClose(fd);return ntohs(addr.sin_port);
}
int connectTo(uint16_t port) {
    int fd=int(socket(AF_INET,SOCK_STREAM,0));check(fd>=0,"socket");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port);
    if(connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr)) {sockClose(fd);return -1;}return fd;
}
std::string request(uint16_t port,const std::string& text) {
    int fd=connectTo(port);check(fd>=0,"connect responder");
    check(send(fd,text.data(),int(text.size()),MSG_NOSIGNAL)==int(text.size()),"send request");
    sockSetNonBlock(fd);std::string response;char bytes[2048];
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(std::chrono::steady_clock::now()<deadline) {
        pollfd p{};p.fd=fd;p.events=POLLIN;TAK_POLL(&p,1,100);
        int n=int(recv(fd,bytes,sizeof bytes,0));if(n>0)response.append(bytes,size_t(n));else if(n==0)break;
        else if(!sockWouldBlock(sockErr()) && !sockInterrupted(sockErr()))break;
    }
    sockClose(fd);return response;
}
}
int main() {
    try {
        netStartup();using tak::srv::acme_detail::validDomain;
        check(validDomain("tak.pgnet.us"),"valid hostname");
        for(const char* value:{"","localhost","*.example.org","127.0.0.1","bad_name.test","x..test","-a.test","a-.test","tls://a.test","a.test:80","a.test\r\n"})check(!validDomain(value),"invalid hostname accepted");
        const auto port=unusedPort();const std::string token(43,'a'),auth=token+".thumbprint";
        check(connectTo(port)<0,"port should start closed");
        {
            Challenge listener(port,token,auth);
            auto response=request(port,"GET /.well-known/acme-challenge/"+token+" HTTP/1.1\r\nHost: example.test\r\n\r\n");
            check(response.starts_with("HTTP/1.1 200 OK") && response.ends_with(auth),"challenge response");
            for(const char* path:{"/","/../current.pem","/.well-known/acme-challenge/wrong"})
                check(request(port,std::string("GET ")+path+" HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"),"unknown path");
            check(request(port,"POST /.well-known/acme-challenge/"+token+" HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"),"unexpected method");
            std::vector<int> slow;for(int i=0;i<16;++i)slow.push_back(connectTo(port));
            check(request(port,"GET / HTTP/1.1\r\n\r\n").starts_with("HTTP/1.1 404"),"slow peers block validation");
            for(int fd:slow)if(fd>=0)sockClose(fd);
            bool rejected=false;try {Challenge duplicate(port,token,auth);}catch(const std::exception&) {rejected=true;}
            check(rejected,"occupied challenge port accepted");
        }
        check(connectTo(port)<0,"port remained open after validation");
        const auto failedPort=unusedPort();
        try {Challenge listener(failedPort,token,auth);throw std::runtime_error("simulated CA failure");}catch(const std::exception&){}
        check(connectTo(failedPort)<0,"port remained open after failure");
        bool rejected=false;try {Challenge listener(port,"../invalid",auth);}catch(const std::exception&){rejected=true;}
        check(rejected && connectTo(port)<0,"invalid token opened listener");
        tak::srv::AcmeOptions options;options.domain="tak.pgnet.us";options.state="unused-acme-test-state";
        rejected=false;try {tak::srv::AcmeCertificates certificates(options);}catch(const std::exception&) {rejected=true;}
        check(rejected,"terms agreement not enforced");
        std::puts("ACME responder and configuration checks passed");return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
