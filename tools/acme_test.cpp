#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "net/netcompat.h"
#include "net/conn.h"
#include "server/acme_internal.h"
#include "server/acme.h"
#include <ctime>
#include <thread>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
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
int connectTo(uint16_t port,const char* source=nullptr) {
    int fd=int(socket(AF_INET,SOCK_STREAM,0));check(fd>=0,"socket");
    if(source) {
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7f000002);
        check(bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof local)==0,"bind source");
    }
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port);
    if(connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr)) {sockClose(fd);return -1;}return fd;
}
std::string request(uint16_t port,const std::string& text) {
    int fd=connectTo(port,"127.0.0.2");check(fd>=0,"connect responder");
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
            std::vector<int> slow;for(int i=0;i<40;++i)slow.push_back(connectTo(port));
            check(request(port,"GET /.well-known/acme-challenge/"+token+" HTTP/1.1\r\n\r\n").ends_with(auth),"one source blocked another source's challenge");
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
        // Saved-state startup must work offline, including Unicode state paths
        // and owner-only Windows ACLs. No certificate authority is contacted.
        const auto root=std::filesystem::temp_directory_path()/
            (std::filesystem::path(u8"tak-acme-é-")+=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        try {
            EVP_PKEY* key=EVP_RSA_gen(2048);check(key,"fixture key");
            X509* cert=X509_new();check(cert,"fixture certificate");
            X509_set_version(cert,2);ASN1_INTEGER_set(X509_get_serialNumber(cert),1);
            X509_gmtime_adj(X509_getm_notBefore(cert),-60);X509_gmtime_adj(X509_getm_notAfter(cert),7*86400);
            X509_set_pubkey(cert,key);auto* subject=X509_get_subject_name(cert);
            X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("acme.example.test"),-1,-1,0);
            X509_set_issuer_name(cert,subject);
            auto* san=X509V3_EXT_conf_nid(nullptr,nullptr,NID_subject_alt_name,"DNS:acme.example.test");
            check(san && X509_add_ext(cert,san,-1)==1,"fixture SAN");X509_EXTENSION_free(san);
            check(X509_sign(cert,key,EVP_sha256())>0,"fixture signature");
            BIO* bio=BIO_new(BIO_s_mem());check(bio,"fixture BIO");
            check(PEM_write_bio_X509(bio,cert)==1 && PEM_write_bio_PrivateKey(bio,key,nullptr,nullptr,0,nullptr,nullptr)==1,"fixture PEM");
            char* bytes=nullptr;const long length=BIO_get_mem_data(bio,&bytes);
            {std::ofstream out(root/"current.pem",std::ios::binary);out.write(bytes,length);}
            BIO_free(bio);X509_free(cert);EVP_PKEY_free(key);
            options.domain="acme.example.test";options.state=root;options.agreeTerms=true;
            {
                tak::srv::AcmeCertificates saved(options);check(bool(saved.context()),"offline saved certificate");
                rejected=false;try {tak::srv::AcmeCertificates duplicate(options);}catch(const std::exception&) {rejected=true;}
                check(rejected,"shared state lease not enforced");
                check(!std::filesystem::exists(root/"account.pem"),"offline startup contacted CA");
            }
            std::filesystem::remove(root/"current.pem");
            // Local closed port: a transient CA failure records backoff but does
            // not terminate the service. No public CA or DNS is contacted.
            options.directory="https://localhost:"+std::to_string(unusedPort())+"/dir";
            std::filesystem::remove(root/"identity.json");
            {
                tak::srv::AcmeCertificates failed(options);
                auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                while(!std::filesystem::exists(root/"retry.json") && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                check(std::filesystem::exists(root/"retry.json") && !failed.context(),"transient failure did not persist retry");
            }
            {std::ofstream retry(root/"retry.json");retry<<"{\"next\":"<<(std::time(nullptr)+7200)<<",\"failures\":2}";}
            auto began=std::chrono::steady_clock::now();
            {tak::srv::AcmeCertificates waiting(options);check(!waiting.context(),"backoff served plaintext context");}
            check(std::chrono::steady_clock::now()-began<std::chrono::seconds(1),"persisted backoff blocked startup/shutdown");
            std::filesystem::remove_all(root);
        }catch(...) {std::filesystem::remove_all(root);throw;}
        std::puts("ACME responder, saved state and configuration checks passed");return 0;
    }catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
