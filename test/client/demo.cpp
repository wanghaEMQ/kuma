#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <thread>
#include <map>
#include <cstdlib>
#include <sstream>

#ifndef KUMA_OS_WIN
#include <signal.h>
#endif

#include "mqtt/client.h"

#include "kmapi.h"
#include "LoopPool.h"
#include "../../third_party/libkev/src/util/defer.h"
#include "./HttpClient.h"

/* Mqtt parameter defined here */

const std::string SERVER_PREFIX { "$rpc/server" };
const std::string RESPONSE_PREFIX { "$rpc/response" };
const std::string PATH { "/demo/rpc" };

const std::string SERVER_ADDRESS { "tcp://localhost:1883" };

const std::string ID { "/demo_clientid" };
const std::string TOPIC { "$rpc/request/clientid:HTTP2:path/query" };
const char*       PAYLOAD_MQTT = "hello";

const int QOS = 1;

std::string POST_ADDRESS = { "http://localhost:18084/demo/rpc" };
std::string GET_ADDRESS = { "http://localhost:18084/demo/rpc?a=1" };

/* MsgQueueThread parameter defined here */

std::queue<mqtt::message> msgQueue;
std::mutex mqCvMtx;
std::condition_variable mqCv;

/* KUMA (for Http2) parameter defined here */

using namespace kuma;

#define THREAD_COUNT    2
static bool g_exit = false;
bool g_test_http2 = true;
std::string g_proxy_url;
std::string g_proxy_user;
std::string g_proxy_passwd;
size_t g_bandwidth = 0;
EventLoop main_loop(PollType::NONE);

static uint32_t _send_interval_ = 0;
uint32_t getSendInterval()
{
    return _send_interval_;
}

extern "C" int km_parse_address(const char *addr,
    char *proto,
    size_t proto_len,
    char *host,
    size_t  host_len,
    unsigned short *port);

#ifdef KUMA_OS_WIN
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
    if(CTRL_C_EVENT == dwCtrlType) {
        g_exit = TRUE;
        main_loop.stop();
        return TRUE;
    }
    return FALSE;
}
#else
void HandlerRoutine(int sig)
{
    if(sig == SIGTERM || sig == SIGINT || sig == SIGKILL) {
        g_exit = true;
        main_loop.stop();
    }
	exit(0);
}
#endif

static const std::string g_usage =
"   client [option] http://google.com/\n"
"   -v              print version\n"
"   --http2         test http2\n"
;

std::vector<std::thread> event_threads;

void printUsage()
{
    printf("%s\n", g_usage.c_str());
}

std::string getHttpVersion()
{
    return g_test_http2 ? "HTTP/2.0": "HTTP/1.1";
}

void split_str(std::string& str, std::string& delim, std::vector<std::string>& vec)
{
	auto start = 0U;
	auto end = str.find(delim);
	while (end != std::string::npos) {
		vec.push_back(str.substr(start, end - start));
		start = end + delim.length();
		end = str.find(delim, start);
	}
	vec.push_back(str.substr(start, end - start));
}

/////////////////////////////////////////////////////////////////////////////

class sample_mem_persistence : virtual public mqtt::iclient_persistence
{
	// Whether the store is open
	bool open_;

	// Use an STL map to store shared persistence pointers
	// against string keys.
	std::map<std::string, std::string> store_;

public:
	sample_mem_persistence() : open_(false) {}

	// "Open" the store
	void open(const std::string& clientId, const std::string& serverURI) override {
//		std::cout << "[Opening persistence store for '" << clientId
//			<< "' at '" << serverURI << "']" << std::endl;
		open_ = true;
	}

	// Close the persistent store that was previously opened.
	void close() override {
//		std::cout << "[Closing persistence store.]" << std::endl;
		open_ = false;
	}

	// Clears persistence, so that it no longer contains any persisted data.
	void clear() override {
//		std::cout << "[Clearing persistence store.]" << std::endl;
		store_.clear();
	}

	// Returns whether or not data is persisted using the specified key.
	bool contains_key(const std::string &key) override {
		return store_.find(key) != store_.end();
	}

	// Returns the keys in this persistent data store.
	mqtt::string_collection keys() const override {
		mqtt::string_collection ks;
		for (const auto& k : store_)
			ks.push_back(k.first);
		return ks;
	}

	// Puts the specified data into the persistent store.
	void put(const std::string& key, const std::vector<mqtt::string_view>& bufs) override {
//		std::cout << "[Persisting data with key '"
//			<< key << "']" << std::endl;
		std::string str;
		for (const auto& b : bufs)
			str.append(b.data(), b.size());	// += b.str();
		store_[key] = std::move(str);
	}

	// Gets the specified data out of the persistent store.
	std::string get(const std::string& key) const override {
//		std::cout << "[Searching persistence for key '"
//			<< key << "']" << std::endl;
		auto p = store_.find(key);
		if (p == store_.end())
			throw mqtt::persistence_exception();
//		std::cout << "[Found persistence data for key '"
//			<< key << "']" << std::endl;

		return p->second;
	}

	// Remove the data for the specified key.
	void remove(const std::string &key) override {
//		std::cout << "[Persistence removing key '" << key << "']" << std::endl;
		auto p = store_.find(key);
		if (p == store_.end())
			throw mqtt::persistence_exception();
		store_.erase(p);
//		std::cout << "[Persistence key removed '" << key << "']" << std::endl;
	}
};

/////////////////////////////////////////////////////////////////////////////
// Class to receive callbacks

class user_callback : public virtual mqtt::callback
{
	void connection_lost(const std::string& cause) override {
		std::cout << "\nConnection lost" << std::endl;
		if (!cause.empty())
			std::cout << "\tcause: " << cause << std::endl;
	}

	void delivery_complete(mqtt::delivery_token_ptr tok) override {
		std::cout << "\n\t[Delivery complete for token: "
			<< (tok ? tok->get_message_id() : -1) << "]" << std::endl;
	}

	void message_arrived(mqtt::const_message_ptr msg) override {
		std::cout << "Message arrived" << std::endl;
		std::string topic = msg->get_topic();
		std::string payload = msg->to_string();
		std::cout << "\ttopic: '" << topic << "'" << std::endl;
		std::cout << "\tpayload: '" << payload << "'\n" << std::endl;
		// TODO mqtt msg parser
		// Topic as $rpc/request/${ID}:${HTTP Method}:${Path}?${Query String}
		std::string delim_a = ":";
		std::vector<std::string> topic_s;
		split_str(topic, delim_a, topic_s);

		std::string delim_b = "/";
		std::vector<std::string> topic_s_s;
		split_str(topic_s[0], delim_b, topic_s_s);

		std::string requestID = topic_s_s[2];
		std::string method = topic_s[1];
		std::string url = topic_s[2];

		// Response topic must have prefix $rpc/response/${ID}:${Path}
		std::string responseTopic = RESPONSE_PREFIX + delim_b + requestID + delim_a + url;
		std::string requestPayload = payload;
		std::string responsePayload = "{\"message\": \"hello\"}";

		std::cout << "Method: " << method << std::endl;
		std::cout << "Topic: " << topic << std::endl;
		std::cout << "Url: " << url << std::endl;
		std::cout << "Payload: " << requestPayload << std::endl;
		std::cout << "--------------------" << std::endl;
		std::cout << "Response topic: " << responseTopic << std::endl;
		std::cout << "Response payload: " << responsePayload << std::endl;

		mqtt::message responseMsg(responseTopic, responsePayload);
		msgQueue.push(responseMsg);
		std::unique_lock<std::mutex> lck(mqCvMtx);
		mqCv.notify_one();

//		std::string bind_addr;
//		loop_pool->startTest(url, bind_addr, 1);
	}

public:
	LoopPool* loop_pool;
	void set_loop_pool(LoopPool* loop_pool) {
		this->loop_pool = loop_pool;
	}
};

void doreconnect(mqtt::client& client) {
	std::cout << "\nReconnencting..." << std::endl;
	if(!client.is_connected()) {
		client.reconnect();
	}
	std::cout << "...OK" << std::endl;
}

void test(mqtt::client& client) {
	// Now try with itemized publish.
	std::cout << "\nSending message..." << std::endl;
	client.publish(TOPIC, PAYLOAD_MQTT, strlen(PAYLOAD_MQTT)+1);
	std::cout << "...OK" << std::endl;
}

void mqThreadMain(mqtt::client& client)
{
	std::unique_lock<std::mutex> lck(mqCvMtx);
	while (1) {
		mqCv.wait(lck, []{return (!msgQueue.empty() || g_exit);});
		if (g_exit) {
			std::cout << "mqThread exit." << std::endl;
			return;
		}
		client.publish(msgQueue.front());
		msgQueue.pop();
	}
}

int
main()
{
	std::cout << "MQTT initialzing..." << std::endl;
	sample_mem_persistence persist;
	std::string clientID = SERVER_PREFIX + PATH + ID;
	std::cout << "Clientid is [" << clientID << "]" << std::endl;
	mqtt::client client(SERVER_ADDRESS, clientID, &persist);

	user_callback cb;
	client.set_callback(cb);

	mqtt::connect_options connOpts;
	connOpts.set_keep_alive_interval(20);
	connOpts.set_clean_session(true);
	std::cout << "...OK" << std::endl;

	std::cout << "MqThreadMain initialzing..." << std::endl;
	std::thread mqThread(mqThreadMain, std::ref(client));
	mqThread.detach();
	std::cout << "...OK" << std::endl;

	std::cout << "KUMA initialzing..." << std::endl;
	signal(SIGINT, HandlerRoutine);
	signal(SIGTERM, HandlerRoutine);

    kuma::init();
    DEFER(kuma::fini());
	int  concurrent = 1;
	std::cout << "...OK" << std::endl;
    
    if (!main_loop.init()) {
        printf("failed to init EventLoop\n");
        return -1;
    }
    if(concurrent <= 0) {
        concurrent = 1;
    }
	std::string bind_addr;

    LoopPool loop_pool;
    loop_pool.init(THREAD_COUNT, main_loop.getPollType());

	cb.set_loop_pool(&loop_pool);
	std::string input;

	try {
		std::cout << "\nConnecting..." << std::endl;
		client.connect(connOpts);
		std::cout << "...OK" << std::endl;

		std::cout << "\nSubscribing..." << std::endl;
		client.subscribe(TOPIC, QOS);
		std::cout << "...OK" << std::endl;
	}
	catch (const mqtt::persistence_exception& exc) {
		std::cerr << "Persistence Error: " << exc.what() << " ["
			<< exc.get_reason_code() << "]" << std::endl;
		return 1;
	}
	catch (const mqtt::exception& exc) {
		std::cerr << exc.what() << std::endl;
		return 1;
	}

	while (1) {
		std::cin >> input;
		if (strcmp(input.data(), "c") == 0) {
			g_exit = true;
			mqCv.notify_all();
        	main_loop.stop();
			break;
		} else if (strcmp(input.data(), "g") == 0) {
    		loop_pool.startTest(GET_ADDRESS, bind_addr, concurrent);
		} else if (strcmp(input.data(), "p") == 0) {
    		loop_pool.startTest(POST_ADDRESS, bind_addr, concurrent);
		} else if (strcmp(input.data(), "r") == 0) {
			doreconnect(client);
		} else if (strcmp(input.data(), "test") == 0) {
			test(client);
		} else {
			std::cout << "undefined command." << std::endl;
		}
	}

	try {
		// Disconnect
		std::cout << "\nDisconnecting..." << std::endl;
		client.disconnect();
		std::cout << "...OK" << std::endl;
	}
	catch (const mqtt::persistence_exception& exc) {
		std::cerr << "Persistence Error: " << exc.what() << " ["
			<< exc.get_reason_code() << "]" << std::endl;
		return 1;
	}
	catch (const mqtt::exception& exc) {
		std::cerr << exc.what() << std::endl;
		return 1;
	}

	std::cout << "\nExiting" << std::endl;
 	return 0;
}

