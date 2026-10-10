// xpanel-input-probe - root helper for the XPanel button mapper.
//
// Stops InputPlumber (it grabs the Ally devices), reads every ROG Ally / Ally X / Xbox Ally input node
// (evdev + hidraw, USB 0b05:1b4c or 0b05:1abe) and writes each event as one JSON line on stdout.
// Starts InputPlumber again on exit: normal end, SIGTERM/SIGINT/SIGHUP, or when the
// reader goes away (stdout broken pipe, checked by a heartbeat every second).
//
// Lines:
//   {"kind":"key","src":"event5","dev":"ASUS ROG Ally X Gamepad","code":304,"name":"BTN_A","value":1}
//   {"kind":"abs","src":"event5","dev":"...","code":0,"name":"ABS_X","value":-1200,"min":-32768,"max":32767}
//   {"kind":"hid","src":"hidraw2","dev":"...","id":"5a","data":"5a a6 ..."}
//   {"kind":"status","text":"..."}   {"kind":"alive"}

#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;
bool g_restartPlumber = false;

std::string readFile(const std::string &fn)
{
    std::ifstream f(fn);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

std::string jsonEscape(const std::string &s)
{
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

bool emit(const std::string &line)
{
    const std::string out = line + "\n";
    size_t off = 0;
    while (off < out.size()) {
        const ssize_t n = ::write(STDOUT_FILENO, out.data() + off, out.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false; // reader gone
        }
        off += size_t(n);
    }
    return true;
}

// Code names from the kernel header (KEY_/BTN_/ABS_). Falls back to numbers.
std::map<int, std::string> g_keyNames, g_absNames;
void loadNames()
{
    std::ifstream f("/usr/include/linux/input-event-codes.h");
    std::string line;
    const std::regex re(R"(#define\s+((KEY|BTN|ABS)_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|\d+))");
    std::smatch m;
    while (std::getline(f, line)) {
        if (!std::regex_search(line, m, re))
            continue;
        const int v = int(std::stol(m[3].str(), nullptr, 0));
        const std::string name = m[1].str();
        if (name == "KEY_MAX" || name == "KEY_CNT" || name == "ABS_MAX" || name == "ABS_CNT")
            continue;
        auto &tbl = m[2].str() == "ABS" ? g_absNames : g_keyNames;
        // keep the first name, but prefer BTN_ names for button codes
        if (!tbl.count(v) || (name.rfind("BTN_", 0) == 0 && tbl[v].rfind("BTN_", 0) != 0))
            tbl[v] = name;
    }
}

std::string keyName(int c) { return g_keyNames.count(c) ? g_keyNames[c] : "KEY_" + std::to_string(c); }
std::string absName(int c) { return g_absNames.count(c) ? g_absNames[c] : "ABS_" + std::to_string(c); }

bool isAlly(const std::string &vendor, const std::string &product)
{
    // ROG Ally X and ROG Xbox Ally / Xbox Ally X 0b05:1b4c, ROG Ally (RC71L) 0b05:1abe
    auto lower = [](std::string s) { for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string p = lower(product);
    return lower(vendor) == "0b05" && (p == "1b4c" || p == "1abe");
}

struct Node {
    int fd = -1;
    std::string src, dev;
    bool hid = false;
    std::map<int, input_absinfo> abs;
    std::map<int, int> lastAbs;
    std::map<int, std::chrono::steady_clock::time_point> lastAbsSent;
    std::map<int, std::chrono::steady_clock::time_point> lastHidSent;
    std::map<std::string, std::string> lastHid;
};

std::vector<Node> g_nodes;

bool isOpen(const std::string &src)
{
    for (const Node &n : g_nodes)
        if (n.src == src) return true;
    return false;
}

void scan()
{
    if (DIR *d = opendir("/sys/class/input")) {
        while (dirent *e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.rfind("event", 0) != 0 || isOpen(name))
                continue;
            const std::string base = "/sys/class/input/" + name + "/device/";
            if (!isAlly(readFile(base + "id/vendor"), readFile(base + "id/product")))
                continue;
            Node n;
            n.src = name;
            n.dev = readFile(base + "name");
            n.fd = ::open(("/dev/input/" + name).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (n.fd < 0)
                continue;
            for (int c = 0; c < ABS_CNT; ++c) {
                input_absinfo ai{};
                if (ioctl(n.fd, EVIOCGABS(c), &ai) == 0 && ai.maximum != ai.minimum) {
                    unsigned long bits[(ABS_CNT + 63) / 64] = {};
                    ioctl(n.fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
                    if (bits[c / 64] & (1UL << (c % 64)))
                        n.abs[c] = ai;
                }
            }
            emit("{\"kind\":\"status\",\"text\":\"open " + name + " (" + jsonEscape(n.dev) + ")\"}");
            g_nodes.push_back(n);
        }
        closedir(d);
    }
    if (DIR *d = opendir("/sys/class/hidraw")) {
        while (dirent *e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.rfind("hidraw", 0) != 0 || isOpen(name))
                continue;
            const std::string uevent = readFile("/sys/class/hidraw/" + name + "/device/uevent");
            if (uevent.find("00000B05:00001B4C") == std::string::npos && uevent.find("00000B05:00001ABE") == std::string::npos)
                continue;
            Node n;
            n.src = name;
            n.hid = true;
            const auto p = uevent.find("HID_NAME=");
            n.dev = p == std::string::npos ? name : uevent.substr(p + 9, uevent.find('\n', p) - p - 9);
            const auto ph = uevent.find("HID_PHYS=");
            if (ph != std::string::npos) {
                const std::string phys = uevent.substr(ph + 9, uevent.find('\n', ph) - ph - 9);
                n.dev += " [" + phys.substr(phys.rfind('/') + 1) + "]";
            }
            n.fd = ::open(("/dev/" + name).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (n.fd < 0)
                continue;
            emit("{\"kind\":\"status\",\"text\":\"open " + name + " (" + jsonEscape(n.dev) + ")\"}");
            g_nodes.push_back(n);
        }
        closedir(d);
    }
}

void readEvdev(Node &n)
{
    input_event ev[64];
    for (;;) {
        const ssize_t r = ::read(n.fd, ev, sizeof(ev));
        if (r <= 0) {
            if (r < 0 && errno == ENODEV) { ::close(n.fd); n.fd = -1; }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < size_t(r) / sizeof(input_event); ++i) {
            const input_event &e = ev[i];
            const std::string head = "{\"src\":\"" + n.src + "\",\"dev\":\"" + jsonEscape(n.dev) + "\",";
            if (e.type == EV_KEY) {
                if (e.value == 2) continue; // autorepeat
                emit(head + "\"kind\":\"key\",\"code\":" + std::to_string(e.code) + ",\"name\":\"" +
                     keyName(e.code) + "\",\"value\":" + std::to_string(e.value) + "}");
            } else if (e.type == EV_ABS) {
                // Send a stick / trigger value at most 25 times a second, and only real changes.
                const auto &ai = n.abs.count(e.code) ? n.abs[e.code] : input_absinfo{};
                const int range = std::max(1, ai.maximum - ai.minimum);
                const int last = n.lastAbs.count(e.code) ? n.lastAbs[e.code] : e.value;
                const bool big = std::abs(e.value - last) * 50 >= range;
                if (!big && n.lastAbs.count(e.code))
                    continue;
                if (n.lastAbsSent.count(e.code) && now - n.lastAbsSent[e.code] < std::chrono::milliseconds(40))
                    continue;
                n.lastAbs[e.code] = e.value;
                n.lastAbsSent[e.code] = now;
                emit(head + "\"kind\":\"abs\",\"code\":" + std::to_string(e.code) + ",\"name\":\"" +
                     absName(e.code) + "\",\"value\":" + std::to_string(e.value) + ",\"min\":" +
                     std::to_string(ai.minimum) + ",\"max\":" + std::to_string(ai.maximum) + "}");
            }
        }
    }
}

void readHid(Node &n)
{
    unsigned char buf[256];
    for (;;) {
        const ssize_t r = ::read(n.fd, buf, sizeof(buf));
        if (r <= 0) {
            if (r < 0 && errno == ENODEV) { ::close(n.fd); n.fd = -1; }
            return;
        }
        char hex[4];
        std::string data;
        for (ssize_t i = 0; i < std::min<ssize_t>(r, 32); ++i) {
            snprintf(hex, sizeof(hex), "%02x", buf[i]);
            data += (i ? " " : "") + std::string(hex);
        }
        const int id = buf[0];
        snprintf(hex, sizeof(hex), "%02x", id);
        // Same report again: skip. Streaming reports: at most 10 a second.
        if (n.lastHid[hex] == data)
            continue;
        const auto now = std::chrono::steady_clock::now();
        if (n.lastHidSent.count(id) && now - n.lastHidSent[id] < std::chrono::milliseconds(100))
            continue;
        n.lastHid[hex] = data;
        n.lastHidSent[id] = now;
        emit("{\"src\":\"" + n.src + "\",\"dev\":\"" + jsonEscape(n.dev) + "\",\"kind\":\"hid\",\"id\":\"" +
             hex + "\",\"data\":\"" + data + "\"}");
    }
}

void onSignal(int) { g_stop = 1; }

void cleanup()
{
    for (Node &n : g_nodes)
        if (n.fd >= 0) ::close(n.fd);
    g_nodes.clear();
    if (g_restartPlumber) {
        g_restartPlumber = false;
        std::system("systemctl start inputplumber.service");
    }
}

} // namespace

int main()
{
    if (geteuid() != 0) {
        fprintf(stderr, "xpanel-input-probe: run as root\n");
        return 1;
    }
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);
    loadNames();

    if (std::system("systemctl is-active --quiet inputplumber.service") == 0) {
        g_restartPlumber = true;
        emit("{\"kind\":\"status\",\"text\":\"stopping InputPlumber\"}");
        std::system("systemctl stop inputplumber.service");
        usleep(1500 * 1000); // the devices come back after InputPlumber lets them go
    }
    scan();
    emit("{\"kind\":\"status\",\"text\":\"recording\"}");

    auto lastBeat = std::chrono::steady_clock::now();
    auto lastScan = lastBeat;
    while (!g_stop) {
        std::vector<pollfd> pfds;
        for (const Node &n : g_nodes)
            pfds.push_back({n.fd, POLLIN, 0});
        poll(pfds.data(), pfds.size(), 250);
        for (Node &n : g_nodes) {
            if (n.fd < 0) continue;
            if (n.hid) readHid(n);
            else readEvdev(n);
        }
        // drop gone nodes, look for new ones (devices can re-appear with new numbers)
        for (auto it = g_nodes.begin(); it != g_nodes.end();)
            it = it->fd < 0 ? g_nodes.erase(it) : it + 1;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastScan > std::chrono::seconds(3)) {
            scan();
            lastScan = now;
        }
        if (now - lastBeat > std::chrono::seconds(1)) {
            if (!emit("{\"kind\":\"alive\"}"))
                break;
            lastBeat = now;
        }
    }
    cleanup();
    return 0;
}
