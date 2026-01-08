#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <regex>
#include <queue>
#include <mutex>
#include <condition_variable>

// ---- mbedTLS ----
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>

using namespace std;

// =======================================================
// Data Structures
// =======================================================
struct Endpoint
{
    string scheme, host, path;
    int port = 0;
    bool isHttps = false;
    bool valid = false;
};

// Queue for the background thread
queue<string> payloadQueue;
mutex queueMutex;
condition_variable queueCv;

// =======================================================
// Helpers
// =======================================================
Endpoint parseUrl(const string &url)
{
    Endpoint ep;
    regex re(R"((https?):\/\/([^\/:]+)(:(\d+))?(\/.*))");
    smatch m;
    if (!regex_match(url, m, re))
    {
        cerr << "Invalid URL. Use http[s]://host[:port]/path\n";
        return ep;
    }
    ep.scheme = m[1];
    ep.host = m[2];
    ep.port = m[4].matched ? stoi(m[4]) : (ep.scheme == "https" ? 443 : 80);
    ep.path = m[5];
    ep.isHttps = (ep.scheme == "https");
    ep.valid = true;
    return ep;
}

double get_cpu_usage()
{
    static long prev_idle = 0, prev_total = 0;
    ifstream f("/proc/stat");
    string cpu;
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    f >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;
    long idle_all = idle + iowait;
    long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    long diff_idle = idle_all - prev_idle;
    long diff_total = total - prev_total;
    prev_idle = idle_all;
    prev_total = total;
    return diff_total ? (100.0 * (diff_total - diff_idle) / diff_total) : 0.0;
}

double get_ram_usage()
{
    ifstream f("/proc/meminfo");
    string key;
    long total = 0, avail = 0;
    while (f >> key)
    {
        if (key == "MemTotal:")
            f >> total;
        else if (key == "MemAvailable:")
        {
            f >> avail;
            break;
        }
        else
            f.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    return total ? (100.0 * (total - avail) / total) : 0.0;
}

void get_disk(const char *path, double &disk, double &inode)
{
    struct statvfs st;
    if (statvfs(path, &st) != 0)
    {
        disk = inode = -1;
        return;
    }
    disk = 100.0 * (1.0 - (double)st.f_bavail / st.f_blocks);
    inode = 100.0 * (1.0 - (double)st.f_favail / st.f_files);
}

// =======================================================
// Network Logic (Blocking, Safe)
// =======================================================
void perform_request(const Endpoint &ep, const string &json, const string &apiKey)
{
    string req =
        "POST " + ep.path + " HTTP/1.1\r\n"
                            "Host: " +
        ep.host + "\r\n"
                  "User-Agent: StatsLogger/1.1\r\n"
                  "X-API-Key: " +
        apiKey + "\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: " +
        to_string(json.size()) + "\r\n"
                                 "Connection: close\r\n\r\n" +
        json;

    // --- HTTP ---
    if (!ep.isHttps)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            return;

        // Add timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        hostent *server = gethostbyname(ep.host.c_str());
        if (server)
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(ep.port);
            memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
            {
                send(sock, req.c_str(), req.size(), 0);
            }
        }
        close(sock);
        return;
    }

    // --- HTTPS (mbedTLS) ---
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    int ret;
    const char *pers = "stats";

    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers)) != 0)
        goto cleanup;

    if (mbedtls_net_connect(&net, ep.host.c_str(), to_string(ep.port).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
        goto cleanup;

    // Setup SSL Config
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);

    // Note: In mbedTLS 3.x this changes, but we are using 2.28
    mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);

    // WARNING: VERIFY_NONE is insecure. For production, load CAs and use VERIFY_REQUIRED.
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_setup(&ssl, &conf);
    mbedtls_ssl_set_hostname(&ssl, ep.host.c_str());
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);

    if (mbedtls_ssl_handshake(&ssl) == 0)
    {
        mbedtls_ssl_write(&ssl, (const unsigned char *)req.c_str(), req.size());
    }

cleanup:
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}

// =======================================================
// Background Worker
// =======================================================
void worker_thread(Endpoint ep, string apiKey)
{
    while (true)
    {
        unique_lock<mutex> lock(queueMutex);
        queueCv.wait(lock, []
                     { return !payloadQueue.empty(); });

        string json = payloadQueue.front();
        payloadQueue.pop();
        lock.unlock();

        perform_request(ep, json, apiKey);
    }
}

// =======================================================
// MAIN
// =======================================================
int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        cerr << "Usage: " << argv[0] << " <endpoint> <server_name> <api_key>\n";
        return 1;
    }

    Endpoint ep = parseUrl(argv[1]);
    if (!ep.valid)
        return 1;

    string serverName = argv[2];
    string apiKey = argv[3];

    // Start background sender
    thread sender(worker_thread, ep, apiKey);
    sender.detach();

    while (true)
    {
        double cpu = get_cpu_usage();
        double ram = get_ram_usage();
        double disk, inode;
        get_disk("/", disk, inode);

        string json = "{\"server\":\"" + serverName + "\","
                                                      "\"cpu\":" +
                      to_string(cpu) +
                      ",\"ram\":" + to_string(ram) +
                      ",\"disk\":" + to_string(disk) +
                      ",\"inode\":" + to_string(inode) + "}";

        // Add to queue (limit size to prevent RAM explosion if net is down)
        {
            lock_guard<mutex> lock(queueMutex);
            if (payloadQueue.size() < 50)
            {
                payloadQueue.push(json);
            }
        }
        queueCv.notify_one();

        this_thread::sleep_for(chrono::seconds(5));
    }
}