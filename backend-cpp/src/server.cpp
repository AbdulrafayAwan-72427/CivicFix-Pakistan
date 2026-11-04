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
#include <ctime>
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
        ",\"status\":" + jsonStr(toString(c.status)) +
        ",\"createdAt\":" + jsonStr(c.createdAt) +
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
    return false;  // Resolved / Rejected are terminal.
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
};

static Response handleRequest(Database& db, const std::string& method,
                              const std::string& path,
                              const std::map<std::string, std::string>& query,
                              const std::string& body) {
    std::vector<std::string> p = splitPath(path);
    auto bad = [](const std::string& msg) { return Response{400, errorJson(msg)}; };

    // GET /api/health
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "health")
        return {200, "{\"status\":\"ok\",\"service\":\"CivicFix Pakistan\"}"};

    // GET /api/areas
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "areas")
        return {200, listJson<Area>(db.getAllAreas(), areaJson)};

    // GET /api/users, POST /api/users
    if (p.size() == 2 && p[0] == "api" && p[1] == "users") {
        if (method == "GET")
            return {200, listJson<User>(db.getAllUsers(), userJson)};
        if (method == "POST") {
            User u;
            if (!jsonGetString(body, "fullName", u.fullName) || u.fullName.empty())
                return bad("fullName is required");
            if (!jsonGetString(body, "email", u.email) || u.email.empty())
                return bad("email is required");
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

    // POST /api/complaints, GET /api/complaints
    if (p.size() == 2 && p[0] == "api" && p[1] == "complaints") {
        if (method == "GET") {
            std::vector<Complaint> all = db.getAllComplaints();
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
            Complaint c;
            if (!jsonGetInt(body, "citizenId", c.citizenId)) return bad("citizenId is required");
            if (!jsonGetString(body, "title", c.title) || c.title.empty()) return bad("title is required");
            if (!jsonGetString(body, "description", c.description) || c.description.empty()) return bad("description is required");
            std::string catStr;
            if (!jsonGetString(body, "category", catStr) || !parseCategory(catStr, c.category))
                return bad("category must be e.g. Garbage Collection, Road Damage, Water Supply, Drainage, Streetlight or Other");
            if (!jsonGetInt(body, "priorityScore", c.priorityScore)) c.priorityScore = 3;
            if (c.priorityScore < 1 || c.priorityScore > 5) return bad("priorityScore must be 1-5");
            if (!jsonGetInt(body, "areaId", c.areaId)) return bad("areaId is required");
            if (!userExists(db, c.citizenId)) return bad("citizenId does not exist");
            int maxSuffix = 100;
            for (const Complaint& existing : db.getAllComplaints()) {
                if (existing.trackingCode.rfind("CFX-", 0) == 0) {
                    try {
                        int n = std::stoi(existing.trackingCode.substr(4));
                        if (n > maxSuffix) maxSuffix = n;
                    } catch (...) {}
                }
            }
            c.trackingCode = "CFX-" + std::to_string(maxSuffix + 1);
            c.photoPath = "";
            c.status = ComplaintStatus::Submitted;
            c.createdAt = nowTimestamp();
            c.updatedAt = c.createdAt;
            if (!db.insertComplaint(c)) return {500, errorJson("Could not submit complaint")};
            Complaint created;
            if (!db.getComplaintByTrackingCode(c.trackingCode, created)) return {500, errorJson("Complaint saved but could not be read back")};
            return {201, complaintJson(created)};
        }
    }

    // GET /api/complaints/track/{code}
    if (method == "GET" && p.size() == 4 && p[0] == "api" && p[1] == "complaints" && p[2] == "track") {
        Complaint c;
        if (!db.getComplaintByTrackingCode(p[3], c)) return {404, errorJson("Complaint not found")};
        return {200, complaintJson(c)};
    }

    // GET /api/complaints/{id}, GET .../history, PATCH/PUT .../status
    if (p.size() >= 3 && p[0] == "api" && p[1] == "complaints" && isNumber(p[2])) {
        int id = std::stoi(p[2]);
        if (method == "GET" && p.size() == 3) {
            Complaint c;
            if (!db.getComplaintById(id, c)) return {404, errorJson("Complaint not found")};
            return {200, complaintJson(c)};
        }
        if (method == "GET" && p.size() == 4 && p[3] == "history") {
            Complaint c;
            if (!db.getComplaintById(id, c)) return {404, errorJson("Complaint not found")};
            return {200, listJson<StatusUpdate>(db.getComplaintHistory(id), historyJson)};
        }
        if ((method == "PATCH" || method == "PUT") && p.size() == 4 && p[3] == "status") {
            std::string statusStr;
            int changedBy = 0;
            std::string notes;
            if (!jsonGetString(body, "newStatus", statusStr)) return bad("newStatus is required");
            if (!jsonGetInt(body, "changedByUserId", changedBy)) return bad("changedByUserId is required");
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
        int complaintId = 0, staffId = 0, assignedBy = 0;
        if (!jsonGetInt(body, "complaintId", complaintId)) return bad("complaintId is required");
        if (!jsonGetInt(body, "staffId", staffId)) return bad("staffId is required");
        if (!jsonGetInt(body, "assignedByAdminId", assignedBy)) return bad("assignedByAdminId is required");
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
        if (method == "GET" && p.size() == 3)
            return {200, listJson<AdminAction>(db.getAllAdminActions(), actionJson)};
        if (method == "GET" && p.size() == 4 && p[3] == "latest") {
            AdminAction a;
            if (!db.getLatestAdminAction(a)) return {404, errorJson("No pending admin action")};
            return {200, actionJson(a)};
        }
    }
    if (method == "POST" && p.size() == 3 && p[0] == "api" && p[1] == "admin" && p[2] == "undo") {
        int changedBy = 0;
        std::string notes;
        if (!jsonGetInt(body, "changedByUserId", changedBy)) return bad("changedByUserId is required");
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
        std::vector<Complaint> all = db.getAllComplaints();
        std::map<std::string, int> byStatus, byCategory;
        int resolved = 0;
        for (const Complaint& c : all) {
            byStatus[toString(c.status)]++;
            byCategory[toString(c.category)]++;
            if (c.status == ComplaintStatus::Resolved) resolved++;
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
                      ",\"pendingComplaints\":" + std::to_string(static_cast<int>(all.size()) - resolved) +
                      ",\"totalUsers\":" + std::to_string(db.getAllUsers().size()) +
                      ",\"totalAreas\":" + std::to_string(db.getAllAreas().size()) +
                      ",\"byStatus\":" + mapJson(byStatus) +
                      ",\"byCategory\":" + mapJson(byCategory) + "}"};
    }

    // GET /api/queue — active complaints in FIFO order, via the custom queue.
    if (method == "GET" && p.size() == 2 && p[0] == "api" && p[1] == "queue") {
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
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

static void sendResponse(socket_t client, const Response& res) {
    std::string header = "HTTP/1.1 " + std::to_string(res.code) + " " + statusText(res.code) +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(res.body.size()) +
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
    size_t contentLength = 0;
    std::string headers = data.substr(0, headerEnd);
    size_t cl = headers.find("Content-Length:");
    if (cl != std::string::npos) {
        size_t start = cl + 15;
        while (start < headers.size() && !std::isdigit(static_cast<unsigned char>(headers[start]))) start++;
        size_t end = start;
        while (end < headers.size() && std::isdigit(static_cast<unsigned char>(headers[end]))) end++;
        if (end > start) contentLength = static_cast<size_t>(std::stoul(headers.substr(start, end - start)));
    }
    size_t bodyStart = headerEnd + 4;
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
        Response res = {400, errorJson("Bad request")};
        if (!method.empty() && !path.empty())
            res = handleRequest(db, method, path, query, body);
        std::cout << method << " " << target << " -> " << res.code << "\n";
        sendResponse(client, res);
        CLOSE_SOCKET(client);
    }
    // Unreachable in normal use (Ctrl+C stops the process).
    CLOSE_SOCKET(serverSocket);
    db.disconnect();
    return 0;
}
