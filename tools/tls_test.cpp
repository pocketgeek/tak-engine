#include "net/conn.h"
#include "net/netcompat.h"
#include "net/crypto.h"
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <stdexcept>

using namespace tak::net;
static void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
static void certificate(const std::filesystem::path& certPath,const std::filesystem::path& keyPath,bool expired) {
    EVP_PKEY* key=EVP_RSA_gen(2048);check(key,"key generation");
    X509* cert=X509_new();check(cert,"certificate allocation");
    X509_set_version(cert,2);ASN1_INTEGER_set(X509_get_serialNumber(cert),expired?2:1);
    X509_gmtime_adj(X509_getm_notBefore(cert),expired?-259200:-60);
    X509_gmtime_adj(X509_getm_notAfter(cert),expired?-86400:86400);
    X509_set_pubkey(cert,key);
    X509_NAME* name=X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("localhost"),-1,-1,0);
    X509_set_issuer_name(cert,name);
    X509_EXTENSION* san=X509V3_EXT_conf_nid(nullptr,nullptr,NID_subject_alt_name,const_cast<char*>("DNS:localhost"));
    check(san,"SAN generation");X509_add_ext(cert,san,-1);X509_EXTENSION_free(san);
    check(X509_sign(cert,key,EVP_sha256())>0,"certificate signing");
    FILE* out=std::fopen(certPath.string().c_str(),"wb");check(out,"certificate file");
    check(PEM_write_X509(out,cert)==1,"certificate write");std::fclose(out);
    out=std::fopen(keyPath.string().c_str(),"wb");check(out,"key file");
    check(PEM_write_PrivateKey(out,key,nullptr,nullptr,0,nullptr,nullptr)==1,"key write");std::fclose(out);
    X509_free(cert);EVP_PKEY_free(key);
}
static void trust(const std::filesystem::path& cert) {
#ifdef _WIN32
    _putenv_s("SSL_CERT_FILE",cert.string().c_str());
#else
    setenv("SSL_CERT_FILE",cert.string().c_str(),1);
#endif
}
struct Echo {
    int listener=-1;uint16_t port=0;std::atomic<bool> stop=false;
    std::thread worker;
    std::atomic<std::shared_ptr<TlsContext>> active;
    explicit Echo(std::shared_ptr<TlsContext> context):active(std::move(context)) {
        std::string error;listener=listenOn(0,error,true);check(listener>=0,"listen");
        sockaddr_in address{};socklen_t length=sizeof address;
        check(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0,"port");port=ntohs(address.sin_port);
        worker=std::thread([this] {
            std::vector<Conn> peers;
            while(!stop.load()) {
                const int fd=int(accept(listener,nullptr,nullptr));
                if(fd>=0) {setupSocket(fd);peers.emplace_back(fd,active.load());}
                for(auto& peer:peers) {
                    if(!peer.ok())continue;
                    peer.recv();Frame f;
                    while(peer.ok() && peer.poll(f))peer.send(f.kind,f.payload);
                    peer.flushWrite();
                    if(peer.peerClosed() || !peer.ok())peer.closeNow();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    ~Echo() {stop=true;if(worker.joinable())worker.join();if(listener>=0)sockClose(listener);}
};
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--fixtures") {
        const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
        certificate(root/"cert.pem",root/"key.pem",false);return 0;
    }
    const auto root=std::filesystem::temp_directory_path()/("tak-tls-"+tak::crypto::toHex(tak::crypto::randomVec(8)));
    std::filesystem::create_directories(root);
    int result=0;
    try {
        const auto cert=root/"cert.pem",key=root/"key.pem";
        certificate(cert,key,false);trust(cert);
        {
            Echo server(TlsContext::server(cert.string(),key.string()));
            Conn good;check(good.connect("tls://localhost",server.port),"trusted hostname rejected");
            auto read=[](const auto& path) {std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
            const auto bundle=read(cert)+read(key);
            server.active.store(TlsContext::serverPem(bundle,bundle));
            // An established connection must survive a renewal context swap;
            // later connections below also exercise the new in-memory context.
            const std::vector<uint8_t> payload(65536,0x5a);
            for(int i=0;i<64;++i)good.send(Msg::Chat,payload);
            int received=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(received<64 && std::chrono::steady_clock::now()<deadline) {
                check(good.flushWrite() && good.recv(),"TLS transfer failed");Frame frame;
                while(good.poll(frame)) {check(frame.kind==Msg::Chat && frame.payload==payload,"TLS payload corrupted");++received;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(received==64,"TLS multi-frame transfer stalled");
            good.closeNow(); // no close-notify: must not kill the process on a subsequent write
            Conn wrong;check(!wrong.connect("tls://127.0.0.1",server.port),"wrong hostname accepted");
            trust(root/"missing.pem");Conn untrusted;
            check(!untrusted.connect("tls://localhost",server.port),"untrusted certificate accepted");
            trust(cert);
            Conn plain;check(plain.connect("127.0.0.1",server.port),"plain TCP connect");plain.send(Msg::Hello);plain.flushWrite();
            for(int i=0;i<1000 && plain.ok() && !plain.peerClosed();++i) {plain.recv();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            check(!plain.ok() || plain.peerClosed(),"plaintext accepted by TLS listener");
            Conn healthy;check(healthy.connect("tls://localhost",server.port),"bad peers damaged TLS listener");
        }
        certificate(cert,key,true);trust(cert);
        {Echo server(TlsContext::server(cert.string(),key.string()));Conn expired;
         check(!expired.connect("tls://localhost",server.port),"expired certificate accepted");}
        std::puts("PASS: verified TLS, framing/backpressure, wrong hostname, untrusted/expired certificate, plaintext rejection and abrupt close");
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    std::filesystem::remove_all(root);return result;
}
