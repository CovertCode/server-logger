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

using namespace std;

// =======================================================
//           PARSE ENDPOINT FROM ARGUMENT
// =======================================================

struct Endpoint {
    string host;
    string path;
    int port;
    bool valid = false;
};

Endpoint parseEndpoint(const string &url) {
    Endpoint ep;

    // supports only http://host[:port]/path
    regex re(R"(http:\/\/([^\/:]+)(:(\d+))?(\/.*))");
    smatch m;
    if (!regex_match(url, m, re)) {
        cerr << "Invalid endpoint format. Use http://host[:port]/path\n";
        return ep;
    }

    ep.host = m[1];
    ep.port = m[3].matched ? stoi(m[3]) : 80;
    ep.path = m[4];
    ep.valid = true;
    return ep;
}

// =======================================================
//                   SYSTEM METRICS
// =======================================================

double get_cpu_usage() {
    static long prev_idle = 0, prev_total = 0;
    ifstream file("/proc/stat");
    string cpu;
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    file >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;

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
//                    FIRE & FORGET POST
// =======================================================

void post_async(const Endpoint &ep, const string &json) {
    thread([=]() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return;

        hostent *server = gethostbyname(ep.host.c_str());
        if (!server) { close(sock); return; }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ep.port);
        memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return;
        }

        string req =
            "POST " + ep.path + " HTTP/1.1\r\n"
            "Host: " + ep.host + "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + to_string(json.size()) + "\r\n"
            "Connection: close\r\n\r\n" +
            json;

        send(sock, req.c_str(), req.size(), 0);
        close(sock);
    }).detach();
}

// =======================================================
//                        MAIN
// =======================================================

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " http://host[:port]/path\n";
        return 1;
    }

    Endpoint ep = parseEndpoint(argv[1]);
    if (!ep.valid) return 1;

    cout << "Sending stats to " << ep.host << ":" << ep.port << ep.path << endl;

    while (true) {
        double cpu = get_cpu_usage();
        double ram = get_ram_usage();
        double disk, inode;
        get_disk("/", disk, inode);

        string payload =
            "{\"cpu\":" + to_string(cpu) +
            ",\"ram\":" + to_string(ram) +
            ",\"disk\":" + to_string(disk) +
            ",\"inode\":" + to_string(inode) + "}";

        post_async(ep, payload);

        this_thread::sleep_for(chrono::seconds(5));
    }

    return 0;
}
