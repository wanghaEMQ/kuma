#include "TestLoop.h"
#include "LoopPool.h"
#include "TcpClient.h"
#include "HttpClient.h"
#include "WsClient.h"
#include "UdpClient.h"

#include <string.h>
#include <string>

extern "C" int km_parse_address(const char *addr,
    char *proto,
    size_t proto_len,
    char *host,
    size_t  host_len,
    unsigned short *port);


TestLoop::TestLoop(LoopPool* server, PollType poll_type)
: loop_(new EventLoop(poll_type))
, server_(server)
, thread_()
{
    
}

void TestLoop::cleanup()
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    for (auto &kv : obj_map_) {
        kv.second->close();
        delete kv.second;
    }
    obj_map_.clear();
}

bool TestLoop::init()
{
    try {
        thread_ = std::thread([this] {
            run();
        });
    }
    catch(...)
    {
        return false;
    }
    return true;
}

void TestLoop::stop()
{
    //cleanup();
    if(loop_) {
        loop_->async([this] { cleanup(); });
        loop_->stop();
    }
    if(thread_.joinable()) {
        try {
            thread_.join();
        } catch (...) {
            printf("failed to join loop thread\n");
        }
    }
}

void TestLoop::run()
{
    if(!loop_->init()) {
        printf("TestLoop::run, failed to init EventLoop\n");
        return;
    }
    loop_->loop();
}

void TestLoop::startTest(const std::string& addr_url, const std::string& bind_addr)
{
    loop_->async([=] {
        startTest_i(addr_url, bind_addr);
    });
}

void TestLoop::startTest_i(const std::string& addr_url, const std::string& bind_addr)
{
    char proto[16] = {0};
    char host[64] = {0};
    uint16_t port = 0;
    if(km_parse_address(addr_url.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != 0) {
        return ;
    }
    
    if(strcmp(proto, "tcp") == 0 || strcmp(proto, "tcps") == 0) {
        long conn_id = server_->getConnId();
        TcpClient* client = new TcpClient(this, conn_id);
        addObject(conn_id, client);
        if(!bind_addr.empty()) {
            char bind_host[64] = {0};
            uint16_t bind_port = 0;
            if(km_parse_address(bind_addr.c_str(), NULL, 0, bind_host, sizeof(bind_host), &bind_port) == 0) {
                client->bind(bind_host, bind_port);
            }
        }
        if (strcmp(proto, "tcps") == 0) {
            if (port == 0) port = 443;
            client->setSslFlags(SSL_ENABLE);
        }
        client->connect(host, port);
    } else if(strcmp(proto, "udp") == 0) {
        long conn_id = server_->getConnId();
        UdpClient* udp_client = new UdpClient(this, conn_id);
        addObject(conn_id, udp_client);
        if(!bind_addr.empty()) {
            char bind_host[64] = {0};
            uint16_t bind_port = 0;
            if(km_parse_address(bind_addr.c_str(), NULL, 0, bind_host, sizeof(bind_host), &bind_port) == 0) {
                udp_client->bind(bind_host, bind_port);
            }
        }
        udp_client->startSend(host, port);
    } else if(strcmp(proto, "http") == 0 || strcmp(proto, "https") == 0) {
		// Doing http request TODO
		printf("Do http request...\n");
        long conn_id = server_->getConnId();
        HttpClient* http_client = new HttpClient(this, conn_id);
        addObject(conn_id, http_client);
        http_client->startRequest(addr_url);
		printf("...OK\n");
    } else if(strcmp(proto, "ws") == 0 || strcmp(proto, "wss") == 0) {
        long conn_id = server_->getConnId();
        WsClient* ws_client = new WsClient(this, conn_id);
        addObject(conn_id, ws_client);
        ws_client->startRequest(addr_url);
    }
}

void TestLoop::addObject(long conn_id, TestObject* obj)
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    obj_map_.insert(std::make_pair(conn_id, obj));
}

void TestLoop::removeObject(long conn_id)
{
    printf("TestLoop::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(obj_mutex_);
    auto it = obj_map_.find(conn_id);
    if(it != obj_map_.end()) {
        delete it->second;
        obj_map_.erase(it);
    }
}
