// CivicFix Pakistan — C++ REST API server (Phase 2).
//
// This is deliberately a zero-dependency server: it uses only the C++
// standard library and the operating system's socket API, plus the
// project's existing Database class and custom DSA classes. That means
// it builds on Windows (MSYS2/MinGW) and Linux without installing
// cpp-httplib or nlohmann/json first. The HTTP/JSON contract below is
// the same contract a httplib/json version would expose, so a React
// frontend written against this API does not need to change if the
// server is later swapped for httplib.
//
// Run (from the project root):
//   backend-cpp/civicfix_api.exe 8080 database/civicfix.db
//   backend-cpp/civicfix_api 8080 database/civicfix.db   (Linux)
// With no arguments it uses port 8080 and database/civicfix.db.
// The server handles one request at a time (single-threaded), which
// keeps the single SQLite connection safe. That is fine for a
// university demo and for local React development.
//
// Authentication is NOT implemented yet: the users table in this
// project has no password column, so "login" here is intentionally
// left out rather than faked. Registration, roles, and changedByUserId
// checks are real, but do not treat changedByUserId as proof of
// identity until password hashing + tokens are added (Phase 6).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <random>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define SOCKET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define SOCKET_INVALID -1
#endif

#include "dsa/ComplaintQueue.hpp"
#include "dsa/Sorting.hpp"
#include "models/models.hpp"
#include "services/Database.hpp"

using namespace civicfix;

// ------------------------------------------------------------------
// Small JSON helpers (flat objects only — enough for this API).
// ------------------------------------------------------------------

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static std::string jsonStr(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

// Find "key": <value> in a flat JSON object body.
static bool jsonGetString(const std::string& body, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos >= body.size() || body[pos] != '"') return false;
    pos++;
    std::string value;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            pos++;
            if (body[pos] == 'n') value += '\n';
            else if (body[pos] == 't') value += '\t';
            else if (body[pos] == 'r') value += '\r';
            else value += body[pos];
        } else {
            value += body[pos];
        }
        pos++;
    }
    out = value;
    return true;
}

static bool jsonGetInt(const std::string& body, const std::string& key, int& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
    size_t start = pos;
    if (pos < body.size() && (body[pos] == '-' || body[pos] == '+')) pos++;
    while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos == start) return false;
    try {
        out = std::stoi(body.substr(start, pos - start));
    } catch (...) {
        return false;
    }
    return true;
}

static bool jsonGetDouble(const std::string& body, const std::string& key, double& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
    size_t start = pos;
    if (pos < body.size() && (body[pos] == '-' || body[pos] == '+')) pos++;
    while (pos < body.size() && (std::isdigit(static_cast<unsigned char>(body[pos])) || body[pos] == '.')) pos++;
    if (pos == start) return false;
    try { out = std::stod(body.substr(start, pos - start)); } catch (...) { return false; }
    return true;
}

static std::string nowTimestamp() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
    return std::string(buf);
}

static std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// ------------------------------------------------------------------
// Serializers
// ------------------------------------------------------------------

static std::string complaintJson(const Complaint& c) {
    return "{"
        "\"id\":" + std::to_string(c.id) +
        ",\"trackingCode\":" + jsonStr(c.trackingCode) +
        ",\"citizenId\":" + std::to_string(c.citizenId) +
        ",\"title\":" + jsonStr(c.title) +
        ",\"description\":" + jsonStr(c.description) +
        ",\"category\":" + jsonStr(toString(c.category)) +
        ",\"priorityScore\":" + std::to_string(c.priorityScore) +
        ",\"areaId\":" + std::to_string(c.areaId) +
        ",\"photoPath\":" + jsonStr(c.photoPath) +
        ",\"latitude\":" + std::to_string(c.latitude) +
        ",\"longitude\":" + std::to_string(c.longitude) +
        ",\"aiSuggestedCategory\":" + jsonStr(c.aiSuggestedCategory) +
        ",\"aiConfidence\":" + std::to_string(c.aiConfidence) +
        ",\"aiImageLabel\":" + jsonStr(c.aiImageLabel) +
        ",\"status\":" + jsonStr(toString(c.status)) +
        ",\"createdAt\":" + jsonStr(c.createdAt) +
        ",\"updatedAt\":" + jsonStr(c.updatedAt) + "}";
}

// Public tracking response (9 Nov 2026, handoff-2 phase 1): the
// anonymous track-by-code endpoint used to return the full complaint
// serializer — citizen id, description, coordinates and photo path
// included. A tracking code is only meant to answer "where is my
// report?": code, status and last update. Logged-in views keep the
// full complaintJson above; photo delivery (/uploads/...) is unchanged
// for now, because <img> tags cannot send a Bearer token — locking
// it down needs a signed-URL or cookie change, not a one-line fix.
static std::string publicTrackingJson(const Complaint& c) {
    return "{"
        "\"trackingCode\":" + jsonStr(c.trackingCode) +
        ",\"status\":" + jsonStr(toString(c.status)) +
        ",\"updatedAt\":" + jsonStr(c.updatedAt) + "}";
}

static std::string userJson(const User& u) {
    return "{"
        "\"id\":" + std::to_string(u.id) +
        ",\"fullName\":" + jsonStr(u.fullName) +
        ",\"email\":" + jsonStr(u.email) +
        ",\"phone\":" + jsonStr(u.phone) +
        ",\"role\":" + jsonStr(toString(u.role)) +
        ",\"areaId\":" + std::to_string(u.areaId) +
        ",\"isActive\":" + (u.isActive ? "true" : "false") + "}";
}

static std::string areaJson(const Area& a) {
    return "{"
        "\"id\":" + std::to_string(a.id) +
        ",\"name\":" + jsonStr(a.name) +
        ",\"city\":" + jsonStr(a.city) +
        ",\"province\":" + jsonStr(a.province) + "}";
}

static std::string historyJson(const StatusUpdate& h) {
    return "{"
        "\"id\":" + std::to_string(h.id) +
        ",\"complaintId\":" + std::to_string(h.complaintId) +
        ",\"changedByUserId\":" + std::to_string(h.changedByUserId) +
        ",\"oldStatus\":" + jsonStr(toString(h.oldStatus)) +
        ",\"newStatus\":" + jsonStr(toString(h.newStatus)) +
        ",\"notes\":" + jsonStr(h.notes) +
        ",\"createdAt\":" + jsonStr(h.createdAt) + "}";
}

static std::string actionJson(const AdminAction& a) {
    return "{"
        "\"id\":" + std::to_string(a.id) +
        ",\"description\":" + jsonStr(a.description) +
        ",\"complaintCode\":" + jsonStr(a.complaintCode) +
        ",\"previousStatus\":" + jsonStr(a.previousStatus) +
        ",\"newStatus\":" + jsonStr(a.newStatus) +
        ",\"isUndone\":" + (a.isUndone ? "true" : "false") + "}";
}

template <typename T>
static std::string listJson(const std::vector<T>& items, std::string (*toJson)(const T&)) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); i++) {
        if (i) out += ",";
        out += toJson(items[i]);
    }
    out += "]";
    return out;
}

static std::string errorJson(const std::string& message) {
    return "{\"error\":" + jsonStr(message) + "}";
}

// ------------------------------------------------------------------
// Parsing / validation
// ------------------------------------------------------------------

static bool parseStatus(const std::string& s, ComplaintStatus& out) {
    if (s == "Submitted") out = ComplaintStatus::Submitted;
    else if (s == "Under Review") out = ComplaintStatus::UnderReview;
    else if (s == "Assigned") out = ComplaintStatus::Assigned;
    else if (s == "In Progress") out = ComplaintStatus::InProgress;
    else if (s == "Resolved") out = ComplaintStatus::Resolved;
    else if (s == "Rejected") out = ComplaintStatus::Rejected;
    else return false;
    return true;
}

static bool parseCategory(const std::string& s, ComplaintCategory& out) {
    if (s == "Garbage Collection" || s == "GarbageCollection") out = ComplaintCategory::GarbageCollection;
    else if (s == "Road Damage" || s == "RoadDamage") out = ComplaintCategory::RoadDamage;
    else if (s == "Water Supply" || s == "WaterSupply") out = ComplaintCategory::WaterSupply;
    else if (s == "Drainage") out = ComplaintCategory::Drainage;
    else if (s == "Streetlight") out = ComplaintCategory::Streetlight;
    else if (s == "Other") out = ComplaintCategory::Other;
    else return false;
    return true;
}

static bool parseRole(const std::string& s, UserRole& out) {
    if (s == "Citizen") out = UserRole::Citizen;
    else if (s == "Admin") out = UserRole::Admin;
    else if (s == "Staff") out = UserRole::Staff;
    else return false;
    return true;
}

// Allowed status transitions for the API (undo bypasses this, by design).
static bool isValidTransition(ComplaintStatus oldStatus, ComplaintStatus newStatus) {
    if (oldStatus == ComplaintStatus::Submitted)
        return newStatus == ComplaintStatus::UnderReview || newStatus == ComplaintStatus::Rejected;
    if (oldStatus == ComplaintStatus::UnderReview)
        return newStatus == ComplaintStatus::Assigned || newStatus == ComplaintStatus::Rejected;
    if (oldStatus == ComplaintStatus::Assigned)
        return newStatus == ComplaintStatus::InProgress || newStatus == ComplaintStatus::Rejected;
    if (oldStatus == ComplaintStatus::InProgress)
        return newStatus == ComplaintStatus::Resolved || newStatus == ComplaintStatus::Rejected;
    // Reopen (9 Nov 2026, handoff-2 phase 2): Resolved / Rejected are
    // no longer terminal. An Admin — and only an Admin, because the
    // status endpoint already requires the Admin role — can send a
    // closed complaint back to triage (Under Review, not Submitted:
    // it has already been triaged once). The change is recorded in
    // status_history and admin_actions like any other status change,
    // so undo restores the closed status.
    if (oldStatus == ComplaintStatus::Resolved || oldStatus == ComplaintStatus::Rejected)
        return newStatus == ComplaintStatus::UnderReview;
    return false;
}

static bool userExists(Database& db, int userId, User* found = nullptr) {
    for (const User& u : db.getAllUsers()) {
        if (u.id == userId) {
            if (found) *found = u;
            return true;
        }
    }
    return false;
}


// ------------------------------------------------------------------
// Photo upload + ML wiring (C++ -> ML service, never React -> ML).
//
// Photos arrive as base64 in JSON (the zero-dependency server has
// no multipart parser). They are saved under uploads/ next to the
// project root and served back by GET /uploads/<file>. The ML
// service runs on 127.0.0.1:8000; if it is down, the complaint is
// still saved — the AI fields just stay empty, and the frontend
// says so instead of inventing a prediction.
// ------------------------------------------------------------------
static std::vector<unsigned char> base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    int acc = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

static bool savePhoto(const std::string& b64Raw, const std::string& extRaw,
                      const std::string& trackingCode, std::string& relPath) {
    std::string ext = extRaw;
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "jpeg") ext = "jpg";
    if (ext != "jpg" && ext != "png") return false;
    std::string b64 = b64Raw;
    size_t comma = b64.find(',');
    if (b64.rfind("data:", 0) == 0 && comma != std::string::npos) b64 = b64.substr(comma + 1);
    std::vector<unsigned char> bytes = base64Decode(b64);
    if (bytes.empty() || bytes.size() > 5 * 1024 * 1024) return false;
    // Magic-byte check (9 Nov 2026, handoff-2 phase 2): the extension
    // alone does not prove a file is a photo — without this, any bytes
    // (including a script) could be stored as ".jpg" and served back.
    // JPEG must start FF D8 FF, PNG must start 89 50 4E 47.
    if (ext == "jpg") {
        if (bytes.size() < 3 || bytes[0] != 0xFF || bytes[1] != 0xD8 || bytes[2] != 0xFF) return false;
    } else {  // png
        if (bytes.size() < 4 || bytes[0] != 0x89 || bytes[1] != 0x50 || bytes[2] != 0x4E || bytes[3] != 0x47) return false;
    }
    std::error_code ec;
    std::filesystem::create_directories("uploads", ec);
    relPath = "uploads/" + trackingCode + "." + ext;
    std::ofstream out(relPath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

static bool mlPost(const std::string& path, const std::string& body,
                   const std::string& contentType, std::string& outBody) {
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCKET_INVALID) return false;
#ifdef _WIN32
    DWORD timeoutMs = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval tv{};
    tv.tv_sec = 3;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { CLOSE_SOCKET(s); return false; }
    std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1:8000\r\nContent-Type: " + contentType +
        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < req.size()) {
        int n = send(s, req.c_str() + sent, static_cast<int>(req.size() - sent), 0);
        if (n <= 0) { CLOSE_SOCKET(s); return false; }
        sent += static_cast<size_t>(n);
    }
    std::string raw;
    char buf[4096];
    int n = 0;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) raw.append(buf, n);
    CLOSE_SOCKET(s);
    if (raw.find(" 200 ") == std::string::npos && raw.rfind("HTTP/1.1 200", 0) != 0) return false;
    size_t sep = raw.find("\r\n\r\n");
    if (sep == std::string::npos) return false;
    outBody = raw.substr(sep + 4);
    return true;
}

static bool mlPredictText(const std::string& text, std::string& category, double& confidence) {
    std::string resp;
    if (!mlPost("/predict", "{\"text\":" + jsonStr(text) + "}", "application/json", resp)) return false;
    return jsonGetString(resp, "predicted_category", category) && jsonGetDouble(resp, "confidence", confidence);
}

static bool mlDetectImage(const std::string& filePath, std::string& category,
                          double& confidence, std::string& topLabel) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string boundary = "----CivicFixBoundary7d2a";
    std::string filename = filePath.substr(filePath.find_last_of("/\\") + 1);
    std::string ext = filename.size() >= 4 ? filename.substr(filename.size() - 3) : "jpg";
    std::string mime = (ext == "png") ? "image/png" : "image/jpeg";
    std::string body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename +
        "\"\r\nContent-Type: " + mime + "\r\n\r\n" + bytes + "\r\n--" + boundary + "--\r\n";
    std::string resp;
    if (!mlPost("/detect-image", body, "multipart/form-data; boundary=" + boundary, resp)) return false;
    if (!jsonGetString(resp, "predicted_category", category)) return false;
    jsonGetDouble(resp, "confidence", confidence);
    jsonGetString(resp, "label", topLabel);
    return true;
}

static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : path) {
        if (ch == '/') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

static bool isNumber(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// ------------------------------------------------------------------
// Routing
// ------------------------------------------------------------------

struct Response {
    int code;
    std::string body;
    std::string contentType = "application/json";
};


// ------------------------------------------------------------------
// SHA-256 (standard implementation, zero-dependency). Used only for
// password hashing: stored value is "<salt hex>$<sha256(salt+password)>",
// so the same password for two users still hashes differently and
// a leaked database does not hand out the passwords directly.
// Test vector: sha256("abc") =
// ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad.
// ------------------------------------------------------------------
static std::string sha256Hex(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string msg = input;
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; i--) msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xFF));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 3]));
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; i++)
        for (int s = 28; s >= 0; s -= 4) out += hex[(h[i] >> s) & 0xF];
    return out;
}

static std::string randomHex(size_t bytes) {
    std::random_device rd;
    std::string out;
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < bytes; i++) {
        unsigned int v = rd() & 0xFF;
        out += hex[v >> 4];
        out += hex[v & 0xF];
    }
    return out;
}

// Password hashing: iterated SHA-256 (8 Nov 2026). The old format was a
// single SHA-256 round — fast enough to brute-force at scale. The new
// format applies SHA-256 10,000 times, chained: each round hashes the
// previous hex digest + salt + password. Format: iter10000$<salt>$<hash>.
// verifyPassword() still accepts the old legacy format
// <salt>$<sha256(salt+password)> so existing users keep working; a
// successful legacy login upgrades the stored hash (see the login route).
static const int kHashIterations = 10000;

static std::string hashPasswordIterated(const std::string& salt, const std::string& password) {
    std::string digest = sha256Hex(salt + password);
    for (int i = 1; i < kHashIterations; i++)
        digest = sha256Hex(digest + salt + password);
    return digest;
}

static std::string hashPassword(const std::string& password) {
    std::string salt = randomHex(16);
    return "iter10000$" + salt + "$" + hashPasswordIterated(salt, password);
}

static bool verifyPassword(const std::string& password, const std::string& stored) {
    // New format: iter10000$<salt>$<hash>
    const std::string kPrefix = "iter10000$";
    if (stored.rfind(kPrefix, 0) == 0) {
        std::string rest = stored.substr(kPrefix.size());
        size_t sep = rest.find('$');
        if (sep == std::string::npos) return false;
        return hashPasswordIterated(rest.substr(0, sep), password) == rest.substr(sep + 1);
    }
    // Legacy format: <salt>$<sha256(salt+password)>
    size_t sep = stored.find('$');
    if (sep == std::string::npos) return false;
    return sha256Hex(stored.substr(0, sep) + password) == stored.substr(sep + 1);
}

// True when the stored hash is the old single-round format, so the
// login route can upgrade it after a successful login.
static bool isLegacyHash(const std::string& stored) {
    return stored.rfind("iter10000$", 0) != 0;
}

// Login rate limiting (8 Nov 2026): failed POST /api/auth/login
// attempts are counted per email in memory. After 5 failures within
// 15 minutes the email is locked out until the window passes. A
// successful login clears the counter. Only login is limited.
struct FailedLogin {
    int failures = 0;
    std::time_t firstFailure = 0;
};
static std::map<std::string, FailedLogin> g_failedLogins;

static void pruneFailedLogins() {
    std::time_t now = std::time(nullptr);
    for (auto it = g_failedLogins.begin(); it != g_failedLogins.end();) {
        if (now - it->second.firstFailure >= 15 * 60) it = g_failedLogins.erase(it);
        else ++it;
    }
}

static bool loginRateLimited(const std::string& email) {
    auto it = g_failedLogins.find(email);
    if (it == g_failedLogins.end()) return false;
    return it->second.failures >= 5;
}

static void recordFailedLogin(const std::string& email) {
    std::time_t now = std::time(nullptr);
    auto it = g_failedLogins.find(email);
    if (it == g_failedLogins.end()) {
        g_failedLogins[email] = FailedLogin{1, now};
        return;
    }
    it->second.failures++;
}

static void clearFailedLogins(const std::string& email) {
    g_failedLogins.erase(email);
}

static std::string expiryIn24h() {
    std::time_t t = std::time(nullptr) + 24 * 3600;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
    return std::string(buf);
}

// Demo passwords for the seeded users, set only when a user has no
// password yet (first run after the upgrade). These are DEMO
// passwords for a student project — change them after first login
// in any real use. Admin: admin123, Citizen: ali123, Staff: staff123.
//
// Isolation from deployment (9 Nov 2026, handoff-2 phase 2):
// seeding is skipped entirely when CIVICFIX_DEMO_SEED=0. A
// deployment MUST set that and rotate/disable the demo accounts —
// never run a pilot with predictable passwords. With seeding off,
// an empty-hash user simply cannot log in until an Admin resets
// their password (POST /api/users/{id}/reset-password).
static void seedDefaultPasswords(Database& db) {
    for (const User& u : db.getAllUsers()) {
        if (!u.passwordHash.empty()) continue;
        std::string plain = (u.role == UserRole::Admin) ? "admin123"
                            : (u.role == UserRole::Staff) ? "staff123" : "ali123";
        if (db.setUserPasswordHash(u.id, hashPassword(plain)))
            std::cout << "[auth] Seeded demo password for " << u.email << " (" << toString(u.role) << ")\n";
    }
}

// ------------------------------------------------------------------
// RBAC, enforced server-side with login tokens (added 5 Nov 2026).
//
// The frontend logs in at POST /api/auth/login and sends the token
// as "Authorization: Bearer <token>". The server looks the token up
// in the sessions table, gets the user, and checks the stored role
// — it never trusts a role string or a user id from the client.
// Every mutating endpoint below calls requireRole() first, so
// hiding a button in React is not the only protection: a Citizen
// calling POST /api/users or a Staff user calling the status
// endpoint gets 403 from the API. Missing/expired token -> 401.
//
// Honest limit: tokens are random strings stored in SQLite, not
// signed JWTs, and passwords are salted SHA-256 (not bcrypt). That
// is real login for this project, but a production system would use
// bcrypt/argon2 + signed tokens + HTTPS.
//
// Reads are locked down too (fixed 8 Nov 2026 — the old code left
// most of them open to anyone): the complaints list, a single
// complaint, its history, the work queue, stats and the audit trail
// all need a login, and a Citizen only ever sees their own
// complaints. Only two things stay public by design: tracking a
// complaint by its code, and the area list. Tracking codes are
// random now, not sequential, so they cannot be counted through.
// ------------------------------------------------------------------
static bool requireRole(Database& db, int actorId, UserRole wanted,
                        const std::string& action, Response& out) {
    if (actorId <= 0) {
        out = {401, errorJson("Login required: send Authorization: Bearer <token>")};
        return false;
    }
    User actor;
    if (!userExists(db, actorId, &actor)) {
        out = {401, errorJson("Invalid or expired login token")};
        return false;
    }
    if (actor.role != wanted) {
        out = {403, errorJson("Only " + toString(wanted) + " users can " + action)};
        return false;
    }
    return true;
}

// Loads the logged-in user from the session-derived actorId.
// Returns false with a 401 in `out` when there is no valid login.
static bool loadActor(Database& db, int actorId, User& actor, Response& out) {
    if (actorId <= 0) {
        out = {401, errorJson("Login required: send Authorization: Bearer <token>")};
        return false;
    }
    if (!userExists(db, actorId, &actor)) {
        out = {401, errorJson("Invalid or expired login token")};
        return false;
    }
    return true;
}

// Staff or Admin only (the staff work queue is not for citizens).
static bool requireStaffOrAdmin(Database& db, int actorId, const std::string& action,
                                Response& out) {
    User actor;
    if (!loadActor(db, actorId, actor, out)) return false;
    if (actor.role != UserRole::Staff && actor.role != UserRole::Admin) {
        out = {403, errorJson("Only Staff or Admin users can " + action)};
        return false;
    }
    return true;
}

// Can this user see this complaint? Admin and Staff see everything;
// a Citizen sees only their own complaints.
static bool canSeeComplaint(const User& actor, const Complaint& c) {
    if (actor.role == UserRole::Admin || actor.role == UserRole::Staff) return true;
    return c.citizenId == actor.id;
}

// Public-safe user record: id, name and role only — no email or phone.
// Citizens and Staff get this from GET /api/users; Admins get the full
// record. The frontend only needs names to display "Submitted by ...",
// so nothing on screen changes.
static std::string userPublicJson(const User& u) {
    return "{"
        "\"id\":" + std::to_string(u.id) +
        ",\"fullName\":" + jsonStr(u.fullName) +
        ",\"role\":" + jsonStr(toString(u.role)) + "}";
}

// Random, unguessable tracking code, e.g. CFX-7K3M9P.
// The old codes were sequential (CFX-101, CFX-102...), so anyone could
// walk the numbers and read every complaint through the public tracking
// endpoint. 6 characters from a 32-character alphabet (no 0/O/1/I, so
// codes stay readable) gives ~1 billion combinations; we still check
// the database and retry on a collision. Returns "" if no free code
// could be found (effectively impossible).
static std::string randomTrackingCode(Database& db) {
    static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> pick(0, 31);
    for (int attempt = 0; attempt < 10; attempt++) {
        std::string code = "CFX-";
        for (int i = 0; i < 6; i++) code += kAlphabet[pick(rng)];
        Complaint existing;
        if (!db.getComplaintByTrackingCode(code, existing)) return code;
    }
    return "";
}

static Response handleRequest(Database& db, const std::string& method,
                              const std::string& path,
                              const std::map<std::string, std::string>& query,
                              const std::string& body, int actorId = 0,
                              const std::string& authToken = "") {
    std::vector<std::string> p = splitPath(path);
    auto bad = [](const std::string& msg) { return Response{400, errorJson(msg)}; };

    // GET /api/health — process health only: the server is up and
    // answering. It says nothing about the database (a fresh path
    // used to return 200 here while registration failed with 500).
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "health")
        return {200, "{\"status\":\"ok\",\"service\":\"CivicFix Pakistan\"}"};

    // GET /api/ready — database readiness (9 Nov 2026, handoff-2
    // phase 1): 200 only when all core tables exist (Database::
    // isReady), otherwise 503. Deployments and the frontend's
    // "is the service usable?" check should use this, not /health.
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "ready") {
        if (db.isReady())
            return {200, "{\"status\":\"ready\",\"service\":\"CivicFix Pakistan\"}"};
        return {503, "{\"status\":\"not_ready\",\"service\":\"CivicFix Pakistan\"}"};
    }

    // GET /api/areas
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "areas")
        return {200, listJson<Area>(db.getAllAreas(), areaJson)};

    // POST /api/auth/register, POST /api/auth/login,
    // GET /api/auth/me, POST /api/auth/logout
    if (p.size() == 3 && p[0] == "api" && p[1] == "auth" && p[2] == "register") {
        if (method != "POST") return {404, errorJson("Not found")};
        User u;
        std::string password;
        if (!jsonGetString(body, "fullName", u.fullName) || u.fullName.empty())
            return bad("fullName is required");
        if (!jsonGetString(body, "email", u.email) || u.email.empty())
            return bad("email is required");
        if (!jsonGetString(body, "password", password) || password.size() < 6)
            return bad("password is required (at least 6 characters)");
        jsonGetString(body, "phone", u.phone);
        if (!jsonGetInt(body, "areaId", u.areaId)) u.areaId = 1;
        u.role = UserRole::Citizen;  // self-registration is Citizen only
        u.isActive = true;
        User existing;
        if (db.getUserByEmail(u.email, existing))
            return {409, errorJson("An account with this email already exists")};
        u.passwordHash = hashPassword(password);
        if (!db.insertUser(u)) return {500, errorJson("Could not create account")};
        User created;
        if (!db.getUserByEmail(u.email, created)) return {500, errorJson("Account created but could not be read back")};
        std::string token = randomHex(32);
        if (!db.createSession(created.id, token, expiryIn24h())) return {500, errorJson("Could not start session")};
        return {201, "{\"user\":" + userJson(created) + ",\"token\":" + jsonStr(token) + "}"};
    }
    if (p.size() == 3 && p[0] == "api" && p[1] == "auth" && p[2] == "login") {
        if (method != "POST") return {404, errorJson("Not found")};
        std::string email, password;
        if (!jsonGetString(body, "email", email) || !jsonGetString(body, "password", password))
            return bad("email and password are required");
        // Rate limiting (8 Nov 2026): 5 failed attempts for one email
        // within 15 minutes locks that email out until the window
        // passes. Checked before the password, so a locked-out
        // attacker cannot keep guessing.
        pruneFailedLogins();
        if (loginRateLimited(email))
            return {429, errorJson("Too many failed login attempts. Try again in a few minutes.")};
        User u;
        // Same message whether the email or the password is wrong,
        // so a login failure does not reveal which accounts exist.
        if (!db.getUserByEmail(email, u) || u.passwordHash.empty() || !verifyPassword(password, u.passwordHash)) {
            recordFailedLogin(email);
            return {401, errorJson("Invalid email or password")};
        }
        if (!u.isActive) {
            recordFailedLogin(email);
            return {401, errorJson("This account is disabled")};
        }
        // Successful login: clear the failure counter, and upgrade a
        // legacy single-round hash to the iterated format.
        clearFailedLogins(email);
        if (isLegacyHash(u.passwordHash))
            db.setUserPasswordHash(u.id, hashPassword(password));
        std::string token = randomHex(32);
        if (!db.createSession(u.id, token, expiryIn24h())) return {500, errorJson("Could not start session")};
        return {200, "{\"user\":" + userJson(u) + ",\"token\":" + jsonStr(token) + "}"};
    }
    if (p.size() == 3 && p[0] == "api" && p[1] == "auth" && p[2] == "me") {
        if (method != "GET") return {404, errorJson("Not found")};
        if (actorId <= 0) return {401, errorJson("Login required: send Authorization: Bearer <token>")};
        User u;
        if (!userExists(db, actorId, &u)) return {401, errorJson("Invalid or expired login token")};
        return {200, userJson(u)};
    }
    if (p.size() == 3 && p[0] == "api" && p[1] == "auth" && p[2] == "logout") {
        if (method != "POST") return {404, errorJson("Not found")};
        if (actorId <= 0) return {401, errorJson("Login required: send Authorization: Bearer <token>")};
        db.deleteSession(authToken);
        return {200, "{\"loggedOut\":true}"};
    }

    // GET /api/users, POST /api/users
    if (p.size() == 2 && p[0] == "api" && p[1] == "users") {
        if (method == "GET") {
            User actor;
            Response denied;
            if (!loadActor(db, actorId, actor, denied)) return denied;
            // Admins see the full record (the Team card needs it).
            // Everyone else gets names + roles only, so a logged-in
            // citizen cannot harvest other citizens' emails and phones.
            if (actor.role == UserRole::Admin)
                return {200, listJson<User>(db.getAllUsers(), userJson)};
            return {200, listJson<User>(db.getAllUsers(), userPublicJson)};
        }
        if (method == "POST") {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Admin, "create users (add staff)", denied))
                return denied;
            User u;
            std::string password;
            if (!jsonGetString(body, "fullName", u.fullName) || u.fullName.empty())
                return bad("fullName is required");
            if (!jsonGetString(body, "email", u.email) || u.email.empty())
                return bad("email is required");
            if (!jsonGetString(body, "password", password) || password.size() < 6)
                return bad("password is required (at least 6 characters)");
            u.passwordHash = hashPassword(password);
            jsonGetString(body, "phone", u.phone);
            std::string roleStr;
            if (jsonGetString(body, "role", roleStr)) {
                if (!parseRole(roleStr, u.role)) return bad("role must be Citizen, Admin or Staff");
            }
            if (!jsonGetInt(body, "areaId", u.areaId)) u.areaId = 1;
            u.isActive = true;
            for (const User& existing : db.getAllUsers()) {
                if (existing.email == u.email)
                    return {409, errorJson("User already exists")};
            }
            if (!db.insertUser(u)) return {500, errorJson("Could not register user")};
            for (const User& created : db.getAllUsers()) {
                if (created.email == u.email) return {201, userJson(created)};
            }
            return {201, userJson(u)};
        }
    }

    // POST /api/users/{id}/disable, /enable, /reset-password
    // (9 Nov 2026, handoff-2 phase 2 — operator revocation and
    // recovery). Admin only. Disable sets is_active = 0 AND deletes
    // all of the user's sessions, and the session check also requires
    // is_active = 1, so a live token dies immediately, not at the
    // next login. Reset-password also deletes all sessions. An Admin
    // cannot disable their own account (400): that would leave no
    // Admin to re-enable anyone. There is no self-service reset —
    // recovery is operator-assisted through this endpoint.
    if (method == "POST" && p.size() == 4 && p[0] == "api" && p[1] == "users" && isNumber(p[2]) &&
        (p[3] == "disable" || p[3] == "enable" || p[3] == "reset-password")) {
        Response denied;
        if (!requireRole(db, actorId, UserRole::Admin, "manage user access", denied))
            return denied;
        int userId = std::stoi(p[2]);
        User target;
        if (!userExists(db, userId, &target)) return {404, errorJson("User not found")};
        if (p[3] == "disable") {
            if (userId == actorId) return bad("An Admin cannot disable their own account");
            if (!db.setUserActive(userId, false)) return {500, errorJson("Could not disable user")};
            db.deleteSessionsForUser(userId);
            User updated;
            userExists(db, userId, &updated);
            return {200, userJson(updated)};
        }
        if (p[3] == "enable") {
            if (!db.setUserActive(userId, true)) return {500, errorJson("Could not enable user")};
            User updated;
            userExists(db, userId, &updated);
            return {200, userJson(updated)};
        }
        // reset-password
        std::string password;
        if (!jsonGetString(body, "password", password) || password.size() < 6)
            return bad("password is required (at least 6 characters)");
        if (!db.setUserPasswordHash(userId, hashPassword(password)))
            return {500, errorJson("Could not reset password")};
        db.deleteSessionsForUser(userId);
        User updated;
        userExists(db, userId, &updated);
        return {200, userJson(updated)};
    }

    // POST /api/complaints, GET /api/complaints
    if (p.size() == 2 && p[0] == "api" && p[1] == "complaints") {
        if (method == "GET") {
            User actor;
            Response denied;
            if (!loadActor(db, actorId, actor, denied)) return denied;
            std::vector<Complaint> all = db.getAllComplaints();
            // Citizens see only their own complaints; Staff and Admin
            // see the whole table. The citizen page's browser-side
            // filter still works — it just filters an already-scoped list.
            if (actor.role == UserRole::Citizen) {
                std::vector<Complaint> mine;
                for (const Complaint& c : all) if (c.citizenId == actorId) mine.push_back(c);
                all = mine;
            }
            auto statusIt = query.find("status");
            if (statusIt != query.end()) {
                ComplaintStatus wanted;
                if (!parseStatus(statusIt->second, wanted)) return bad("Invalid status filter");
                std::vector<Complaint> filtered;
                for (const Complaint& c : all) if (c.status == wanted) filtered.push_back(c);
                all = filtered;
            }
            auto catIt = query.find("category");
            if (catIt != query.end()) {
                ComplaintCategory wanted;
                if (!parseCategory(catIt->second, wanted)) return bad("Invalid category filter");
                std::vector<Complaint> filtered;
                for (const Complaint& c : all) if (c.category == wanted) filtered.push_back(c);
                all = filtered;
            }
            auto sortIt = query.find("sort");
            if (sortIt != query.end() && sortIt->second == "priority") {
                // Real DSA use: stable Counting Sort by priority (5 first).
                Sorting::countingSortByPriority(all);
            }
            return {200, listJson<Complaint>(all, complaintJson)};
        }
        if (method == "POST") {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Citizen, "submit complaints", denied))
                return denied;
            Complaint c;
            if (!jsonGetInt(body, "citizenId", c.citizenId)) return bad("citizenId is required");
            if (c.citizenId != actorId)
                return {403, errorJson("A citizen can only submit complaints as themselves")};
            if (!jsonGetString(body, "title", c.title) || c.title.empty()) return bad("title is required");
            if (!jsonGetString(body, "description", c.description) || c.description.empty()) return bad("description is required");
            std::string catStr;
            if (!jsonGetString(body, "category", catStr) || !parseCategory(catStr, c.category))
                return bad("category must be e.g. Garbage Collection, Road Damage, Water Supply, Drainage, Streetlight or Other");
            std::string severity;
            if (jsonGetString(body, "severity", severity)) {
                // Citizen-facing wording wins over the numeric score:
                // "low"/"normal"/"high" instead of a 1-5 number the
                // citizen had to guess at.
                if (severity == "low") c.priorityScore = 2;
                else if (severity == "normal") c.priorityScore = 3;
                else if (severity == "high") c.priorityScore = 5;
                else return bad("severity must be low, normal or high");
            } else {
                if (!jsonGetInt(body, "priorityScore", c.priorityScore)) c.priorityScore = 3;
                if (c.priorityScore < 1 || c.priorityScore > 5) return bad("priorityScore must be 1-5");
            }
            if (!jsonGetInt(body, "areaId", c.areaId)) return bad("areaId is required");
            if (!userExists(db, c.citizenId)) return bad("citizenId does not exist");
            // Optional map location + photo, added 5 Nov 2026.
            jsonGetDouble(body, "latitude", c.latitude);
            jsonGetDouble(body, "longitude", c.longitude);
            if ((c.latitude != 0 && (c.latitude < -90 || c.latitude > 90)) ||
                (c.longitude != 0 && (c.longitude < -180 || c.longitude > 180)))
                return bad("latitude must be -90..90 and longitude -180..180");
            std::string photoB64, photoExt;
            bool hasPhoto = jsonGetString(body, "photoBase64", photoB64) && !photoB64.empty();
            if (hasPhoto) jsonGetString(body, "photoExt", photoExt);
            // Random, unguessable tracking code (see randomTrackingCode).
            // The old codes were sequential, so anyone could count
            // through them on the public tracking endpoint.
            c.trackingCode = randomTrackingCode(db);
            if (c.trackingCode.empty()) return {500, errorJson("Could not generate a tracking code")};
            c.photoPath = "";
            if (hasPhoto) {
                std::string saved;
                if (!savePhoto(photoB64, photoExt, c.trackingCode, saved))
                    return bad("photo must be a JPG or PNG under 5 MB");
                c.photoPath = saved;
            }
            c.status = ComplaintStatus::Submitted;
            c.createdAt = nowTimestamp();
            c.updatedAt = c.createdAt;
            if (!db.insertComplaint(c)) return {500, errorJson("Could not submit complaint")};
            Complaint created;
            if (!db.getComplaintByTrackingCode(c.trackingCode, created)) return {500, errorJson("Complaint saved but could not be read back")};
            // C++ -> ML: text category first, then image detection if
            // a photo was uploaded. ML being down never blocks the
            // complaint — the AI fields simply stay empty.
            {
                std::string aiCat;
                double aiConf = 0.0;
                if (mlPredictText(created.title + ". " + created.description, aiCat, aiConf)) {
                    std::string imgLabel;
                    double imgConf = 0.0;
                    std::string imgCat;
                    if (!created.photoPath.empty() &&
                        mlDetectImage(created.photoPath, imgCat, imgConf, imgLabel)) {
                        db.updateComplaintAi(created.id, aiCat, aiConf, imgLabel);
                    } else {
                        db.updateComplaintAi(created.id, aiCat, aiConf, "");
                    }
                    db.getComplaintByTrackingCode(c.trackingCode, created);
                } else if (!created.photoPath.empty()) {
                    std::string imgLabel, imgCat;
                    double imgConf = 0.0;
                    if (mlDetectImage(created.photoPath, imgCat, imgConf, imgLabel))
                        db.updateComplaintAi(created.id, imgCat, imgConf, imgLabel);
                    db.getComplaintByTrackingCode(c.trackingCode, created);
                }
            }
            return {201, complaintJson(created)};
        }
    }

    // GET /uploads/<file> — serve a saved complaint photo.
    if (method == "GET" && p.size() == 2 && p[0] == "uploads") {
        std::string file = p[1];
        if (file.find("..") != std::string::npos || file.find('/') != std::string::npos ||
            file.find('\\') != std::string::npos)
            return bad("Invalid file name");
        std::ifstream in("uploads/" + file, std::ios::binary);
        if (!in) return {404, errorJson("Photo not found")};
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string ct = (file.size() >= 4 && file.substr(file.size() - 4) == ".png") ? "image/png" : "image/jpeg";
        return {200, bytes, ct};
    }
    // GET /api/complaints/track/{code} — public, but minimal (see
    // publicTrackingJson): tracking code, status, last update only.
    if (method == "GET" && p.size() == 4 && p[0] == "api" && p[1] == "complaints" && p[2] == "track") {
        Complaint c;
        if (!db.getComplaintByTrackingCode(p[3], c)) return {404, errorJson("Complaint not found")};
        return {200, publicTrackingJson(c)};
    }

    // GET /api/complaints/{id}, GET .../history, PATCH/PUT .../status
    if (p.size() >= 3 && p[0] == "api" && p[1] == "complaints" && isNumber(p[2])) {
        int id = std::stoi(p[2]);
        if (method == "GET" && p.size() == 3) {
            User actor;
            Response denied;
            if (!loadActor(db, actorId, actor, denied)) return denied;
            Complaint c;
            if (!db.getComplaintById(id, c)) return {404, errorJson("Complaint not found")};
            if (!canSeeComplaint(actor, c))
                return {403, errorJson("A citizen can only view their own complaints")};
            return {200, complaintJson(c)};
        }
        if (method == "POST" && p.size() == 4 && p[3] == "photo") {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Citizen, "add photos to complaints", denied))
                return denied;
            Complaint current;
            if (!db.getComplaintById(id, current)) return {404, errorJson("Complaint not found")};
            if (current.citizenId != actorId)
                return {403, errorJson("A citizen can only add photos to their own complaints")};
            std::string photoB64, photoExt;
            if (!jsonGetString(body, "photoBase64", photoB64) || photoB64.empty())
                return bad("photoBase64 is required");
            jsonGetString(body, "photoExt", photoExt);
            std::string saved;
            if (!savePhoto(photoB64, photoExt, current.trackingCode, saved))
                return bad("photo must be a JPG or PNG under 5 MB");
            if (!db.updateComplaintPhoto(id, saved)) return {500, errorJson("Could not save photo")};
            std::string imgCat, imgLabel;
            double imgConf = 0.0;
            if (mlDetectImage(saved, imgCat, imgConf, imgLabel)) {
                std::string keepCat = current.aiSuggestedCategory.empty() ? imgCat : current.aiSuggestedCategory;
                double keepConf = current.aiSuggestedCategory.empty() ? imgConf : current.aiConfidence;
                db.updateComplaintAi(id, keepCat, keepConf, imgLabel);
            }
            Complaint updated;
            db.getComplaintById(id, updated);
            return {200, complaintJson(updated)};
        }
        if (method == "GET" && p.size() == 4 && p[3] == "history") {
            User actor;
            Response denied;
            if (!loadActor(db, actorId, actor, denied)) return denied;
            Complaint c;
            if (!db.getComplaintById(id, c)) return {404, errorJson("Complaint not found")};
            if (!canSeeComplaint(actor, c))
                return {403, errorJson("A citizen can only view the history of their own complaints")};
            return {200, listJson<StatusUpdate>(db.getComplaintHistory(id), historyJson)};
        }
        if ((method == "PATCH" || method == "PUT") && p.size() == 4 && p[3] == "status") {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Admin, "change complaint status", denied))
                return denied;
            std::string statusStr;
            int changedBy = 0;
            std::string notes;
            if (!jsonGetString(body, "newStatus", statusStr)) return bad("newStatus is required");
            if (!jsonGetInt(body, "changedByUserId", changedBy)) return bad("changedByUserId is required");
            if (changedBy != actorId)
                return {403, errorJson("changedByUserId must match the acting user")};
            jsonGetString(body, "notes", notes);
            ComplaintStatus newStatus;
            if (!parseStatus(statusStr, newStatus)) return bad("Invalid newStatus");
            Complaint current;
            if (!db.getComplaintById(id, current)) return {404, errorJson("Complaint not found")};
            if (!userExists(db, changedBy)) return bad("changedByUserId does not exist");
            if (current.status == newStatus) return bad("Complaint already has this status");
            if (!isValidTransition(current.status, newStatus))
                return bad("Invalid status transition: " + toString(current.status) + " -> " + toString(newStatus));
            if (!db.updateComplaintStatus(id, newStatus, changedBy, notes))
                return {500, errorJson("Status update failed")};
            Complaint updated;
            db.getComplaintById(id, updated);
            return {200, complaintJson(updated)};
        }
    }

    // POST /api/assignments
    if (method == "POST" && p.size() == 2 && p[0] == "api" && p[1] == "assignments") {
        Response denied;
        if (!requireRole(db, actorId, UserRole::Admin, "assign complaints to staff", denied))
            return denied;
        int complaintId = 0, staffId = 0, assignedBy = 0;
        if (!jsonGetInt(body, "complaintId", complaintId)) return bad("complaintId is required");
        if (!jsonGetInt(body, "staffId", staffId)) return bad("staffId is required");
        if (!jsonGetInt(body, "assignedByAdminId", assignedBy)) return bad("assignedByAdminId is required");
        if (assignedBy != actorId)
            return {403, errorJson("assignedByAdminId must match the acting user")};
        Complaint c;
        if (!db.getComplaintById(complaintId, c)) return {404, errorJson("Complaint not found")};
        User staff;
        if (!userExists(db, staffId, &staff)) return bad("staffId does not exist");
        if (staff.role != UserRole::Staff) return bad("staffId is not a Staff user");
        if (!userExists(db, assignedBy)) return bad("assignedByAdminId does not exist");
        if (!db.assignComplaint(complaintId, staffId, assignedBy))
            return {500, errorJson("Could not assign complaint")};
        return {201, "{\"complaintId\":" + std::to_string(complaintId) +
                      ",\"staffId\":" + std::to_string(staffId) +
                      ",\"assignedByAdminId\":" + std::to_string(assignedBy) + "}"};
    }

    // GET /api/admin/actions, GET /api/admin/actions/latest, POST /api/admin/undo
    if (p.size() >= 3 && p[0] == "api" && p[1] == "admin" && p[2] == "actions") {
        if (method == "GET" && p.size() == 3) {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Admin, "view the audit trail", denied))
                return denied;
            return {200, listJson<AdminAction>(db.getAllAdminActions(), actionJson)};
        }
        if (method == "GET" && p.size() == 4 && p[3] == "latest") {
            Response denied;
            if (!requireRole(db, actorId, UserRole::Admin, "view the audit trail", denied))
                return denied;
            AdminAction a;
            if (!db.getLatestAdminAction(a)) return {404, errorJson("No pending admin action")};
            return {200, actionJson(a)};
        }
    }
    if (method == "POST" && p.size() == 3 && p[0] == "api" && p[1] == "admin" && p[2] == "undo") {
        Response denied;
        if (!requireRole(db, actorId, UserRole::Admin, "undo admin actions", denied))
            return denied;
        int changedBy = 0;
        std::string notes;
        if (!jsonGetInt(body, "changedByUserId", changedBy)) return bad("changedByUserId is required");
        if (changedBy != actorId)
            return {403, errorJson("changedByUserId must match the acting user")};
        jsonGetString(body, "notes", notes);
        AdminAction latest;
        if (!db.getLatestAdminAction(latest))
            return {400, errorJson("No admin action available to undo")};
        if (!db.undoLatestAdminAction(changedBy, notes))
            return {400, errorJson("Undo rejected: complaint status has changed since the action, or no action is pending")};
        return {200, "{\"undoneActionId\":" + std::to_string(latest.id) +
                      ",\"complaintCode\":" + jsonStr(latest.complaintCode) +
                      ",\"restoredStatus\":" + jsonStr(latest.previousStatus) + "}"};
    }

    // GET /api/stats
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "stats") {
        Response denied;
        if (!requireRole(db, actorId, UserRole::Admin, "view statistics", denied))
            return denied;
        std::vector<Complaint> all = db.getAllComplaints();
        std::map<std::string, int> byStatus, byCategory;
        int resolved = 0, pending = 0;
        for (const Complaint& c : all) {
            byStatus[toString(c.status)]++;
            byCategory[toString(c.category)]++;
            if (c.status == ComplaintStatus::Resolved) resolved++;
            // Baseline fix, 9 Nov 2026 (handoff-2 phase 1): pending
            // used to be total - resolved, so Rejected complaints
            // counted as still pending. Pending now means active:
            // neither Resolved nor Rejected — the same rule the
            // work queue already used. Acceptance: 1 Submitted +
            // 1 Rejected + 1 Resolved => pending 1, queue 1.
            if (c.status != ComplaintStatus::Resolved &&
                c.status != ComplaintStatus::Rejected) pending++;
        }
        auto mapJson = [](const std::map<std::string, int>& m) {
            std::string out = "{";
            bool first = true;
            for (const auto& kv : m) {
                if (!first) out += ",";
                first = false;
                out += jsonStr(kv.first) + ":" + std::to_string(kv.second);
            }
            out += "}";
            return out;
        };
        return {200, "{\"totalComplaints\":" + std::to_string(all.size()) +
                      ",\"resolvedComplaints\":" + std::to_string(resolved) +
                      ",\"pendingComplaints\":" + std::to_string(pending) +
                      ",\"totalUsers\":" + std::to_string(db.getAllUsers().size()) +
                      ",\"totalAreas\":" + std::to_string(db.getAllAreas().size()) +
                      ",\"byStatus\":" + mapJson(byStatus) +
                      ",\"byCategory\":" + mapJson(byCategory) + "}"};
    }

    // GET /api/me/assignments — the logged-in staff member's own work:
    // complaints currently assigned to them, in assignment order.
    // Staff only: a Citizen or Admin asking for "my assignments" gets 403.
    if (method == "GET" && p.size() == 3 && p[0] == "api" && p[1] == "me" && p[2] == "assignments") {
        Response denied;
        if (!requireRole(db, actorId, UserRole::Staff, "view their own assignments", denied))
            return denied;
        return {200, listJson<Complaint>(db.getComplaintsAssignedTo(actorId), complaintJson)};
    }

    // GET /api/queue — active complaints in FIFO order, via the custom queue.
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "queue") {
        Response denied;
        if (!requireStaffOrAdmin(db, actorId, "view the work queue", denied)) return denied;
        ComplaintQueue queue;
        for (const Complaint& c : db.getAllComplaints()) {
            if (c.status != ComplaintStatus::Resolved && c.status != ComplaintStatus::Rejected)
                queue.enqueue(c);
        }
        std::vector<Complaint> ordered;
        Complaint next;
        while (queue.dequeue(next)) ordered.push_back(next);
        return {200, listJson<Complaint>(ordered, complaintJson)};
    }

    return {404, errorJson("Not found")};
}

// ------------------------------------------------------------------
// HTTP plumbing
// ------------------------------------------------------------------

static std::string statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

// CORS (8 Nov 2026, allowlist added Nov 2026): the Vite dev proxy used
// to hide the fact that this server sent no cross-origin headers at
// all, so a frontend served from any other origin could not call it
// directly. Every response now carries CORS headers, and preflight
// OPTIONS requests are answered in the main loop below.
//
// Which origins are allowed comes from the CIVICFIX_ALLOWED_ORIGINS
// environment variable — a comma-separated list, e.g.
//   CIVICFIX_ALLOWED_ORIGINS=https://civicfix.example.com,https://app.example.com
// Unset or empty means "*" (any origin), the local-development
// default. That is safe here because the frontend uses Bearer
// tokens, not cookies. Before a public deployment, set it to the
// real frontend origin(s) only: a browser from any other origin
// then gets no Access-Control-Allow-Origin header and its
// cross-origin requests are refused by the browser. Non-browser
// clients (curl, the Vite proxy) are unaffected — CORS is a browser
// rule, not a server firewall.
static std::string corsAllowedOrigin(const std::string& requestOrigin) {
    const char* env = std::getenv("CIVICFIX_ALLOWED_ORIGINS");
    if (env == nullptr || std::string(env).empty()) return "*";
    std::string list = env;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string entry = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t first = entry.find_first_not_of(" \t");
        size_t last = entry.find_last_not_of(" \t");
        if (first != std::string::npos) entry = entry.substr(first, last - first + 1);
        else entry.clear();
        if (entry == "*") return "*";
        if (!requestOrigin.empty() && entry == requestOrigin) return requestOrigin;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return "";  // origin not on the allowlist: send no CORS headers
}

static void sendResponse(socket_t client, const Response& res, const std::string& requestOrigin) {
    std::string cors;
    std::string allowed = corsAllowedOrigin(requestOrigin);
    if (!allowed.empty()) {
        cors = "\r\nAccess-Control-Allow-Origin: " + allowed +
            "\r\nAccess-Control-Allow-Headers: Content-Type, Authorization" +
            "\r\nAccess-Control-Allow-Methods: GET, POST, PATCH, PUT, DELETE, OPTIONS";
        if (allowed != "*") cors += "\r\nVary: Origin";
    }
    std::string header = "HTTP/1.1 " + std::to_string(res.code) + " " + statusText(res.code) +
        "\r\nContent-Type: " + res.contentType + "\r\nContent-Length: " + std::to_string(res.body.size()) +
        cors +
        "\r\nConnection: close\r\n\r\n";
    std::string all = header + res.body;
    size_t sent = 0;
    while (sent < all.size()) {
        int n = send(client, all.c_str() + sent, static_cast<int>(all.size() - sent), 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

static std::string readRequest(socket_t client) {
    std::string data;
    char buf[4096];
    while (data.find("\r\n\r\n") == std::string::npos) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, n);
        if (data.size() > 1024 * 1024) break;
    }
    size_t headerEnd = data.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return data;
    std::string headers = data.substr(0, headerEnd);
    std::string lowerHeaders = headers;
    for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t bodyStart = headerEnd + 4;
    // Chunked transfer encoding: Vite's dev proxy sends large
    // bodies (e.g. a photo upload) this way. Decode the chunks
    // into a plain body, so the rest of the server sees normal JSON.
    if (lowerHeaders.find("transfer-encoding:") != std::string::npos &&
        lowerHeaders.find("chunked") != std::string::npos) {
        std::string decoded;
        std::string rest = data.substr(bodyStart);
        while (true) {
            size_t lineEnd = rest.find("\r\n");
            while (lineEnd == std::string::npos) {
                int n = recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                rest.append(buf, n);
                lineEnd = rest.find("\r\n");
            }
            if (lineEnd == std::string::npos) break;
            size_t chunkSize = 0;
            try { chunkSize = std::stoul(rest.substr(0, lineEnd), nullptr, 16); } catch (...) { break; }
            rest = rest.substr(lineEnd + 2);
            if (chunkSize == 0) break;
            while (rest.size() < chunkSize + 2) {
                int n = recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                rest.append(buf, n);
            }
            if (rest.size() < chunkSize) break;
            decoded.append(rest.substr(0, chunkSize));
            rest = rest.substr(chunkSize + 2);
            if (decoded.size() > 8 * 1024 * 1024) break;
        }
        return headers + "\r\n\r\n" + decoded;
    }
    // Header names are case-insensitive; proxies may lowercase them.
    size_t contentLength = 0;
    size_t cl = lowerHeaders.find("content-length:");
    if (cl != std::string::npos) {
        size_t start = cl + 15;
        while (start < headers.size() && !std::isdigit(static_cast<unsigned char>(headers[start]))) start++;
        size_t end = start;
        while (end < headers.size() && std::isdigit(static_cast<unsigned char>(headers[end]))) end++;
        if (end > start) contentLength = static_cast<size_t>(std::stoul(headers.substr(start, end - start)));
    }
    while (data.size() - bodyStart < contentLength) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, n);
    }
    return data;
}

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf;
    int port = 8080;
    std::string dbPath = "database/civicfix.db";
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) dbPath = argv[2];

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    Database db;
    if (!db.connect(dbPath)) return 1;
    // Demo-password seeding is on by default for local development,
    // and off when CIVICFIX_DEMO_SEED=0 (deployments must set this).
    {
        const char* demoSeed = std::getenv("CIVICFIX_DEMO_SEED");
        if (demoSeed != nullptr && std::string(demoSeed) == "0") {
            std::cout << "Demo password seeding DISABLED (CIVICFIX_DEMO_SEED=0). "
                         "Users without a password cannot log in until an Admin resets theirs.\n";
        } else {
            seedDefaultPasswords(db);
        }
    }

    socket_t serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == SOCKET_INVALID) {
        std::cerr << "Could not create socket.\n";
        return 1;
    }
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Could not bind to port " << port << ".\n";
        CLOSE_SOCKET(serverSocket);
        return 1;
    }
    if (listen(serverSocket, 16) != 0) {
        std::cerr << "Could not listen.\n";
        CLOSE_SOCKET(serverSocket);
        return 1;
    }

    std::cout << "CivicFix API listening on http://localhost:" << port << "\n";
    std::cout << "Database: " << dbPath << "\n";
    {
        const char* origins = std::getenv("CIVICFIX_ALLOWED_ORIGINS");
        std::cout << "CORS allowed origins: "
                  << (origins == nullptr || std::string(origins).empty() ? "*" : origins) << "\n";
    }
    std::cout << "Press Ctrl+C to stop.\n";

    while (true) {
        socket_t client = accept(serverSocket, nullptr, nullptr);
        if (client == SOCKET_INVALID) continue;
        std::string raw = readRequest(client);
        std::string method, target, body;
        size_t lineEnd = raw.find("\r\n");
        if (lineEnd != std::string::npos) {
            std::string line = raw.substr(0, lineEnd);
            size_t sp1 = line.find(' ');
            size_t sp2 = line.find(' ', sp1 + 1);
            if (sp1 != std::string::npos && sp2 != std::string::npos) {
                method = line.substr(0, sp1);
                target = line.substr(sp1 + 1, sp2 - sp1 - 1);
            }
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) body = raw.substr(headerEnd + 4);
        }
        std::string path = target;
        std::map<std::string, std::string> query;
        size_t q = target.find('?');
        if (q != std::string::npos) {
            path = target.substr(0, q);
            std::string qs = target.substr(q + 1);
            size_t start = 0;
            while (start <= qs.size()) {
                size_t amp = qs.find('&', start);
                std::string pair = qs.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
                size_t eq = pair.find('=');
                if (eq != std::string::npos)
                    query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
                if (amp == std::string::npos) break;
                start = amp + 1;
            }
        }
        // Auth identity: "Authorization: Bearer <token>". The token
        // is looked up in the sessions table — a user id sent by the
        // client on its own is never trusted any more. 0 = no valid
        // token, which mutating endpoints reject with 401.
        int actorId = 0;
        std::string authToken;
        {
            size_t headerEnd2 = raw.find("\r\n\r\n");
            std::string headersPart = headerEnd2 == std::string::npos ? raw : raw.substr(0, headerEnd2);
            std::string lowerHeaders = headersPart;
            for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            size_t pos = lowerHeaders.find("authorization:");
            if (pos != std::string::npos) {
                size_t start = pos + 14;
                while (start < headersPart.size() && headersPart[start] == ' ') start++;
                size_t lineEnd = headersPart.find("\r\n", start);
                std::string value = headersPart.substr(start, lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
                const std::string bearer = "Bearer ";
                if (value.rfind(bearer, 0) == 0) {
                    authToken = value.substr(bearer.size());
                    User actor;
                    if (db.getSessionUser(authToken, actor)) actorId = actor.id;
                }
            }
        }
        // The Origin header decides which CORS header the reply gets:
        // see corsAllowedOrigin() above. Browsers send it on
        // cross-origin requests; curl and the Vite proxy do not.
        std::string requestOrigin;
        {
            size_t headerEnd2 = raw.find("\r\n\r\n");
            std::string headersPart = headerEnd2 == std::string::npos ? raw : raw.substr(0, headerEnd2);
            std::string lowerHeaders = headersPart;
            for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            size_t pos = lowerHeaders.find("origin:");
            if (pos != std::string::npos) {
                size_t start = pos + 7;
                while (start < headersPart.size() && headersPart[start] == ' ') start++;
                size_t lineEnd = headersPart.find("\r\n", start);
                requestOrigin = headersPart.substr(start, lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
            }
        }
        Response res = {400, errorJson("Bad request")};
        if (method == "OPTIONS") {
            // CORS preflight: browsers send this before a real
            // cross-origin request. No body, no auth — just the
            // CORS headers that sendResponse() adds to every reply.
            res = {204, ""};
        } else if (!method.empty() && !path.empty())
            res = handleRequest(db, method, path, query, body, actorId, authToken);
        std::cout << method << " " << target << " -> " << res.code << "\n";
        sendResponse(client, res, requestOrigin);
        CLOSE_SOCKET(client);
    }
    // Unreachable in normal use (Ctrl+C stops the process).
    CLOSE_SOCKET(serverSocket);
    db.disconnect();
    return 0;
}
