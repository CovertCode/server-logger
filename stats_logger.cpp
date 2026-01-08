#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/time.h> // <--- ADDED THIS TO FIX THE ERROR
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <regex>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

// Struct to hold parsed URL
struct Endpoint
{
    string host, path;
    int port = 80;
    bool valid = false;
};

// Queue for background sending
queue<string> payloadQueue;
mutex queueMutex;
condition_variable queueCv;

// Helper: Parse http://IP:PORT/path
Endpoint parseUrl(const string &url)
{
    Endpoint ep;
    // Regex for http://HOST[:PORT]/PATH
    regex re(R"(http:\/\/([^\/:]+)(:(\d+))?(\/.*))");
    smatch m;
    if (!regex_match(url, m, re))
    {
        cerr << "Invalid URL. Must be http://... (https not supported)\n";
        return ep;
    }
    ep.host = m[1];
    ep.port = m[3].matched ? stoi(m[3]) : 80;
    ep.path = m[4];
    ep.valid = true;
    return ep;
}

double get_cpu_usage()
{
    static long prev_idle = 0, prev_total = 0;
    ifstream f("/proc/stat");
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    string cpu;
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

// Network Sender (Standard Linux Sockets)
void perform_request(const Endpoint &ep, const string &json, const string &apiKey)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return;

    // Timeout (5 seconds) to prevent hanging
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    // Set separate timeouts for Receive and Send
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    hostent *server = gethostbyname(ep.host.c_str());
    if (server)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ep.port);
        memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
        {
            string req = "POST " + ep.path + " HTTP/1.1\r\n"
                                             "Host: " +
                         ep.host + "\r\n"
                                   "User-Agent: StatsLogger/1.0\r\n"
                                   "X-API-Key: " +
                         apiKey + "\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " +
                         to_string(json.size()) + "\r\n"
                                                  "Connection: close\r\n\r\n" +
                         json;
            send(sock, req.c_str(), req.size(), 0);
        }
    }
    close(sock);
}

// Background Worker
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

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        cerr << "Usage: " << argv[0] << " <http_url> <server_name> <api_key>\n";
        return 1;
    }

    Endpoint ep = parseUrl(argv[1]);
    if (!ep.valid)
        return 1;

    string serverName = argv[2];
    string apiKey = argv[3];

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

        {
            lock_guard<mutex> lock(queueMutex);
            if (payloadQueue.size() < 50)
                payloadQueue.push(json);
        }
        queueCv.notify_one();
        this_thread::sleep_for(chrono::seconds(5));
    }
}