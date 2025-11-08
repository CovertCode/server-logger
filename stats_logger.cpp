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

// ---- mbedTLS headers ----
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>

using namespace std;

// =======================================================
// Parse endpoint
// =======================================================
struct Endpoint {
    string scheme, host, path;
    int port = 0;
    bool isHttps = false;
    bool valid = false;
};

Endpoint parseUrl(const string &url) {
    Endpoint ep;
    regex re(R"((https?):\/\/([^\/:]+)(:(\d+))?(\/.*))");
    smatch m;
    if (!regex_match(url, m, re)) {
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

// =======================================================
// Collect system stats
// =======================================================
double get_cpu_usage() {
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

double get_ram_usage() {
    ifstream f("/proc/meminfo");
    string key; long total = 0, avail = 0;
    while (f >> key) {
        if (key == "MemTotal:") f >> total;
        else if (key == "MemAvailable:") { f >> avail; break; }
        else f.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    return total ? (100.0 * (total - avail) / total) : 0.0;
}

void get_disk(const char *path, double &disk, double &inode) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) { disk = inode = -1; return; }
    disk  = 100.0 * (1.0 - (double)st.f_bavail / st.f_blocks);
    inode = 100.0 * (1.0 - (double)st.f_favail / st.f_files);
}

// =======================================================
// Send POST request (HTTP or HTTPS) — fire & forget
// =======================================================
void send_post_async(const Endpoint &ep, const string &json) {
    thread([=]() {
        string req =
            "POST " + ep.path + " HTTP/1.1\r\n"
            "Host: " + ep.host + "\r\n"
            "User-Agent: StatsLogger/1.0\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + to_string(json.size()) + "\r\n"
            "Connection: close\r\n\r\n" +
            json;

        // ---------- Plain HTTP ----------
        if (!ep.isHttps) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { perror("socket"); return; }
            hostent *server = gethostbyname(ep.host.c_str());
            if (!server) { cerr << "DNS failed\n"; close(sock); return; }
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(ep.port);
            memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0)
                send(sock, req.c_str(), req.size(), 0);
            else perror("connect");
            close(sock);
            return;
        }

        // ---------- HTTPS via mbedTLS ----------
        mbedtls_net_context net;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_context entropy;
        char errbuf[128];
        int ret;

        mbedtls_net_init(&net);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);

        const char *pers = "stats_logger";
        if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                         (const unsigned char *)pers, strlen(pers))) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] ctr_drbg_seed failed: " << errbuf << endl;
            goto cleanup;
        }

        if ((ret = mbedtls_net_connect(&net, ep.host.c_str(), to_string(ep.port).c_str(),
                                       MBEDTLS_NET_PROTO_TCP)) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] connect failed: " << errbuf << endl;
            goto cleanup;
        }

        if ((ret = mbedtls_ssl_config_defaults(&conf,
                MBEDTLS_SSL_IS_CLIENT,
                MBEDTLS_SSL_TRANSPORT_STREAM,
                MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] ssl_config_defaults failed: " << errbuf << endl;
            goto cleanup;
        }

        // Cloudflare-compatible TLS 1.3
        mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

        if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] ssl_setup failed: " << errbuf << endl;
            goto cleanup;
        }
        if ((ret = mbedtls_ssl_set_hostname(&ssl, ep.host.c_str())) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] set_hostname failed: " << errbuf << endl;
            goto cleanup;
        }
        mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);

        if ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] handshake failed: " << errbuf << endl;
            goto cleanup;
        }

        ret = mbedtls_ssl_write(&ssl, (const unsigned char*)req.c_str(), req.size());
        if (ret <= 0) {
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            cerr << "[TLS] write failed: " << errbuf << endl;
        } else {
            cout << "[sent] " << json << endl;
        }

    cleanup:
        mbedtls_ssl_close_notify(&ssl);
        mbedtls_net_free(&net);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }).detach();
}

// =======================================================
// Main
// =======================================================
int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " http[s]://host[:port]/path\n";
        return 1;
    }

    Endpoint ep = parseUrl(argv[1]);
    if (!ep.valid) return 1;

    cout << "Sending stats to " << ep.scheme << "://" << ep.host
         << ":" << ep.port << ep.path << endl;

    while (true) {
        double cpu = get_cpu_usage();
        double ram = get_ram_usage();
        double disk, inode;
        get_disk("/", disk, inode);

        string json = "{\"cpu\":" + to_string(cpu) +
                      ",\"ram\":" + to_string(ram) +
                      ",\"disk\":" + to_string(disk) +
                      ",\"inode\":" + to_string(inode) + "}";

        send_post_async(ep, json);
        this_thread::sleep_for(chrono::seconds(5));
    }
}
