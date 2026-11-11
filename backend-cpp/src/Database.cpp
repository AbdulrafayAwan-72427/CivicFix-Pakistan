#include "services/Database.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace civicfix {

// Phase 3 (11 Nov 2026, handoff-2) helpers, defined with the
// lifecycle methods near the end of this file; declared here so
// assignComplaint (earlier in the file) can use them.
static bool insertHistoryEvent(sqlite3* db, int complaintId, int changedByUserId,
                               const std::string& status, const std::string& notes);
static bool getUserFullName(sqlite3* db, int userId, std::string& name);

Database::Database()
    : db(nullptr) {
}

Database::~Database() {
    disconnect();
}

// ========================================
// DATABASE CONNECTION
// ========================================

bool Database::connect(const std::string& path) {

    // Close an existing connection before opening another one.
    if (db != nullptr) {
        disconnect();

        if (db != nullptr) {
            std::cerr << "Could not close the existing database connection.\n";
            return false;
        }
    }

    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {

        std::cerr << "Database connection failed: "
                  << (db != nullptr ? sqlite3_errmsg(db) : "Unknown SQLite error")
                  << '\n';

        disconnect();
        return false;
    }

    if (!execute("PRAGMA foreign_keys = ON;")) {
        disconnect();
        return false;
    }

    // Baseline fix, 9 Nov 2026 (handoff-2, phase 1): a fresh database
    // path used to start "healthy" (/api/health 200) but registration
    // then failed with 500, because only admin_actions and sessions
    // were created here -- users, areas, complaints and the rest did
    // not exist. Create every core table from database/schema.sql
    // (IF NOT EXISTS, so an existing seeded database is untouched),
    // then seed the Lahore areas if the areas table is empty, so a
    // fresh database can register a user (default areaId 1) at once.
    // The password_hash / latitude / AI columns below are still added
    // by ALTER TABLE for databases created from the older schema.
    const char* createCoreTables[] = {
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "full_name TEXT NOT NULL,"
        "email TEXT UNIQUE NOT NULL,"
        "phone TEXT,"
        "role TEXT NOT NULL CHECK(role IN ('Citizen', 'Admin', 'Staff')),"
        "area_id INTEGER,"
        "is_active INTEGER DEFAULT 1,"
        "FOREIGN KEY (area_id) REFERENCES areas(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS areas ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "city TEXT NOT NULL,"
        "province TEXT NOT NULL"
        ");",
        "CREATE TABLE IF NOT EXISTS complaints ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "tracking_code TEXT UNIQUE NOT NULL,"
        "citizen_id INTEGER NOT NULL,"
        "title TEXT NOT NULL,"
        "description TEXT NOT NULL,"
        "category TEXT NOT NULL,"
        "priority_score INTEGER CHECK(priority_score BETWEEN 1 AND 5),"
        "area_id INTEGER NOT NULL,"
        "photo_path TEXT,"
        "status TEXT DEFAULT 'Submitted',"
        "rejection_reason TEXT DEFAULT '',"
        "duplicate_of TEXT DEFAULT '',"
        "resolution_note TEXT DEFAULT '',"
        "escalated INTEGER DEFAULT 0,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (citizen_id) REFERENCES users(id),"
        "FOREIGN KEY (area_id) REFERENCES areas(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS status_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "complaint_id INTEGER NOT NULL,"
        "changed_by_user_id INTEGER NOT NULL,"
        "old_status TEXT NOT NULL,"
        "new_status TEXT NOT NULL,"
        "notes TEXT,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (complaint_id) REFERENCES complaints(id),"
        "FOREIGN KEY (changed_by_user_id) REFERENCES users(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS assignments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "complaint_id INTEGER NOT NULL,"
        "staff_id INTEGER NOT NULL,"
        "assigned_by_admin_id INTEGER NOT NULL,"
        "assigned_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "unassigned_at TEXT,"
        "FOREIGN KEY (complaint_id) REFERENCES complaints(id),"
        "FOREIGN KEY (staff_id) REFERENCES users(id),"
        "FOREIGN KEY (assigned_by_admin_id) REFERENCES users(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS ai_predictions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "complaint_id INTEGER NOT NULL,"
        "predicted_category TEXT NOT NULL,"
        "confidence REAL NOT NULL,"
        "model_version TEXT,"
        "predicted_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "admin_override INTEGER DEFAULT 0,"
        "FOREIGN KEY (complaint_id) REFERENCES complaints(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS complaint_notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "complaint_id INTEGER NOT NULL,"
        "author_id INTEGER NOT NULL,"
        "author_role TEXT NOT NULL,"
        "body TEXT NOT NULL,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (complaint_id) REFERENCES complaints(id),"
        "FOREIGN KEY (author_id) REFERENCES users(id)"
        ");",
        "CREATE TABLE IF NOT EXISTS notifications ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "complaint_id INTEGER NOT NULL,"
        "kind TEXT NOT NULL,"
        "message TEXT NOT NULL,"
        "is_read INTEGER DEFAULT 0,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (user_id) REFERENCES users(id),"
        "FOREIGN KEY (complaint_id) REFERENCES complaints(id)"
        ");",
    };
    for (const char* sql : createCoreTables) {
        if (!execute(sql)) {
            std::cerr << "Could not create a core table.\n";
            disconnect();
            return false;
        }
    }
    // Seed the same five Lahore areas as the seeded database, with
    // the same ids, but only when the table is empty. INSERT OR
    // IGNORE keeps an existing seeded database untouched.
    if (!execute(
            "INSERT OR IGNORE INTO areas (id, name, city, province) VALUES "
            "(1, 'Gulberg', 'Lahore', 'Punjab'),"
            "(5, 'Model Town', 'Lahore', 'Punjab'),"
            "(6, 'Johar Town', 'Lahore', 'Punjab'),"
            "(7, 'DHA Phase 5', 'Lahore', 'Punjab'),"
            "(8, 'Faisal Town', 'Lahore', 'Punjab');")) {
        std::cerr << "Could not seed areas.\n";
        disconnect();
        return false;
    }

    // Persistent Admin Action Stack.
    const char* createActionsTable =
        "CREATE TABLE IF NOT EXISTS admin_actions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "description TEXT NOT NULL,"
        "complaint_code TEXT NOT NULL,"
        "previous_status TEXT NOT NULL,"
        "new_status TEXT NOT NULL,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "is_undone INTEGER DEFAULT 0"
        ");";

    if (!execute(createActionsTable)) {
        std::cerr << "Could not create admin_actions table.\n";
        disconnect();
        return false;
    }

    // Login sessions (tokens). One row per login; expired rows are
    // ignored by getSessionUser and can be deleted on logout.
    const char* createSessionsTable =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "token TEXT PRIMARY KEY,"
        "user_id INTEGER NOT NULL,"
        "expires_at TEXT NOT NULL,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (user_id) REFERENCES users(id)"
        ");";
    if (!execute(createSessionsTable)) {
        std::cerr << "Could not create sessions table.\n";
        disconnect();
        return false;
    }

    // Password column, added 5 Nov 2026. Old databases keep their
    // users; the server seeds a demo password for any user whose
    // hash is still empty (see seedDefaultPasswords in server.cpp).
    {
        sqlite3_stmt* stmt = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(users);", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name && std::string(reinterpret_cast<const char*>(name)) == "password_hash") { exists = true; break; }
            }
        }
        sqlite3_finalize(stmt);
        if (!exists) execute("ALTER TABLE users ADD COLUMN password_hash TEXT DEFAULT '';");
    }

    // Map + AI columns, added 5 Nov 2026. ALTER TABLE ... ADD COLUMN
    // is safe on an existing database: old rows keep their data and
    // the new columns default to 0 / ''.
    {
        const char* newColumns[][2] = {
            {"latitude", "REAL DEFAULT 0"},
            {"longitude", "REAL DEFAULT 0"},
            {"ai_category", "TEXT DEFAULT ''"},
            {"ai_confidence", "REAL DEFAULT 0"},
            {"ai_image_label", "TEXT DEFAULT ''"},
            {"rejection_reason", "TEXT DEFAULT ''"},
            {"duplicate_of", "TEXT DEFAULT ''"},
            {"resolution_note", "TEXT DEFAULT ''"},
            {"escalated", "INTEGER DEFAULT 0"},
        };
        for (const auto& col : newColumns) {
            sqlite3_stmt* stmt = nullptr;
            bool exists = false;
            if (sqlite3_prepare_v2(db, "PRAGMA table_info(complaints);", -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const unsigned char* name = sqlite3_column_text(stmt, 1);
                    if (name && std::string(reinterpret_cast<const char*>(name)) == col[0]) { exists = true; break; }
                }
            }
            sqlite3_finalize(stmt);
            if (!exists) execute(std::string("ALTER TABLE complaints ADD COLUMN ") + col[0] + " " + col[1] + ";");
        }
    }

    std::cout << "Database connected successfully.\n";
    return true;
}

bool Database::isReady() {
    if (db == nullptr) return false;
    const char* sql =
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
        "('users', 'areas', 'complaints', 'status_history', 'assignments', "
        "'ai_predictions', 'admin_actions', 'sessions', "
        "'complaint_notes', 'notifications');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count == 10;
}

void Database::disconnect() {

    if (db != nullptr) {

        int result = sqlite3_close_v2(db);

        if (result == SQLITE_OK) {
            db = nullptr;
            std::cout << "Database disconnected.\n";
        } else {
            std::cerr << "Database disconnection failed: "
                      << sqlite3_errstr(result) << '\n';
        }
    }
}

bool Database::execute(const std::string& sql) {

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return false;
    }

    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql.c_str(),
        nullptr,
        nullptr,
        &errorMessage
    );

    if (result != SQLITE_OK) {

        std::cerr << "SQL Error: "
                  << (errorMessage ? errorMessage : "Unknown error")
                  << '\n';

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

sqlite3* Database::getConnection() const {
    return db;
}

// ========================================
// INSERT AREA
// ========================================

bool Database::insertArea(
    const std::string& name,
    const std::string& city,
    const std::string& province
) {

    if (db == nullptr) return false;

    // Areas has no UNIQUE constraint, so ON CONFLICT would never fire.
    // Check first, so re-running the program does not create duplicates.
    {
        const char* checkSQL =
            "SELECT 1 FROM areas WHERE name = ? AND city = ? AND province = ? LIMIT 1;";
        sqlite3_stmt* checkStmt = nullptr;
        if (sqlite3_prepare_v2(db, checkSQL, -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(checkStmt, 2, city.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(checkStmt, 3, province.c_str(), -1, SQLITE_TRANSIENT);
            bool exists = (sqlite3_step(checkStmt) == SQLITE_ROW);
            sqlite3_finalize(checkStmt);
            if (exists) {
                std::cout << "Area already exists: " << name << '\n';
                return true;
            }
        }
    }

    const char* sql =
        "INSERT INTO areas (name, city, province) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Area preparation failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    int bindResult = sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 2, city.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 3, province.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Area parameter binding failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(stmt);
        return false;
    }

    int result = sqlite3_step(stmt);

    if (result == SQLITE_DONE) {
        if (sqlite3_changes(db) > 0) {
            std::cout << "Area added: " << name << '\n';
        } else {
            std::cout << "Area already exists: " << name << '\n';
        }
    } else {
        std::cerr << "Area insertion failed: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

// ========================================
// DISPLAY AREAS
// ========================================

void Database::displayAllAreas() {

    if (db == nullptr) return;

    const char* sql =
        "SELECT id, name, city, province FROM areas;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not retrieve areas: "
                  << sqlite3_errmsg(db) << '\n';
        return;
    }

    std::cout << "\n========== SAVED AREAS ==========\n";

    int result;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        auto getText = [stmt](int column) -> const char* {
            const unsigned char* value = sqlite3_column_text(stmt, column);
            return value ? reinterpret_cast<const char*>(value) : "";
        };

        std::cout
            << "ID: " << sqlite3_column_int(stmt, 0)
            << " | Name: " << getText(1)
            << " | City: " << getText(2)
            << " | Province: " << getText(3)
            << '\n';
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error reading areas: " << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
}

// ========================================
// INSERT USER
// ========================================

bool Database::insertUser(const User& user) {

    if (db == nullptr) return false;

    // Skip re-inserting an existing user instead of failing on UNIQUE(email).
    {
        const char* checkSQL = "SELECT 1 FROM users WHERE email = ? LIMIT 1;";
        sqlite3_stmt* checkStmt = nullptr;
        if (sqlite3_prepare_v2(db, checkSQL, -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, user.email.c_str(), -1, SQLITE_TRANSIENT);
            bool exists = (sqlite3_step(checkStmt) == SQLITE_ROW);
            sqlite3_finalize(checkStmt);
            if (exists) {
                std::cout << "User already exists: " << user.email << '\n';
                return true;
            }
        }
    }

    const char* sql =
        "INSERT INTO users "
        "(full_name, email, phone, role, area_id, is_active, password_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "User preparation failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    std::string role;

    switch (user.role) {
        case UserRole::Citizen:
            role = "Citizen";
            break;
        case UserRole::Admin:
            role = "Admin";
            break;
        case UserRole::Staff:
            role = "Staff";
            break;
        default:
            std::cerr << "Invalid user role.\n";
            sqlite3_finalize(stmt);
            return false;
    }

    int bindResult = sqlite3_bind_text(
        stmt, 1, user.fullName.c_str(), -1, SQLITE_TRANSIENT
    );

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 2, user.email.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 3, user.phone.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(stmt, 5, user.areaId);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(stmt, 6, user.isActive ? 1 : 0);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 7, user.passwordHash.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "User parameter binding failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(stmt);
        return false;
    }

    int result = sqlite3_step(stmt);

    if (result == SQLITE_DONE) {
        std::cout << "User registered successfully: "
                  << user.fullName << '\n';
    } else {
        std::cerr << "User registration failed: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

// ========================================
// DISPLAY USERS
// ========================================

void Database::displayAllUsers() {

    if (db == nullptr) return;

    const char* sql =
        "SELECT id, full_name, email, phone, role, area_id "
        "FROM users;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not retrieve users: "
                  << sqlite3_errmsg(db) << '\n';
        return;
    }

    std::cout << "\n========== SAVED USERS ==========\n";

    int result;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        auto getText = [stmt](int column) -> const char* {
            const unsigned char* value = sqlite3_column_text(stmt, column);
            return value ? reinterpret_cast<const char*>(value) : "";
        };

        std::cout
            << "ID: " << sqlite3_column_int(stmt, 0)
            << " | Name: " << getText(1)
            << " | Email: " << getText(2)
            << " | Phone: " << getText(3)
            << " | Role: " << getText(4)
            << " | Area ID: " << sqlite3_column_int(stmt, 5)
            << '\n';
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error reading users: " << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
}

// ========================================
// INSERT COMPLAINT
// ========================================

bool Database::insertComplaint(const Complaint& complaint) {

    if (db == nullptr) return false;

    const char* sql =
        "INSERT INTO complaints "
        "(tracking_code, citizen_id, title, description, category, "
        "priority_score, area_id, photo_path, status, created_at, updated_at, "
        "latitude, longitude, ai_category, ai_confidence, ai_image_label) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Complaint preparation failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    std::string category = toString(complaint.category);
    std::string status = toString(complaint.status);

    int bindResult = sqlite3_bind_text(
        stmt, 1, complaint.trackingCode.c_str(), -1, SQLITE_TRANSIENT
    );

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(stmt, 2, complaint.citizenId);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 3, complaint.title.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 4, complaint.description.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 5, category.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(stmt, 6, complaint.priorityScore);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(stmt, 7, complaint.areaId);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 8, complaint.photoPath.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 9, status.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 10, complaint.createdAt.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 11, complaint.updatedAt.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_double(stmt, 12, complaint.latitude);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_double(stmt, 13, complaint.longitude);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 14, complaint.aiSuggestedCategory.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_double(stmt, 15, complaint.aiConfidence);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(stmt, 16, complaint.aiImageLabel.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Complaint parameter binding failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(stmt);
        return false;
    }

    int result = sqlite3_step(stmt);

    if (result == SQLITE_DONE) {
        std::cout << "Complaint saved successfully: "
                  << complaint.trackingCode << '\n';
    } else {
        std::cerr << "Complaint insertion failed: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool Database::updateComplaintPhoto(int complaintId, const std::string& photoPath) {
    if (db == nullptr) return false;
    const char* sql = "UPDATE complaints SET photo_path = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, photoPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, complaintId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::updateComplaintAi(int complaintId, const std::string& aiCategory, double aiConfidence, const std::string& aiImageLabel) {
    if (db == nullptr) return false;
    const char* sql = "UPDATE complaints SET ai_category = ?, ai_confidence = ?, ai_image_label = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, aiCategory.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, aiConfidence);
    sqlite3_bind_text(stmt, 3, aiImageLabel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, complaintId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// ========================================
// DISPLAY COMPLAINTS
// ========================================

void Database::displayAllComplaints() {

    if (db == nullptr) return;

    const char* sql =
        "SELECT id, tracking_code, title, category, priority_score, "
        "status, citizen_id, area_id FROM complaints ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not retrieve complaints: "
                  << sqlite3_errmsg(db) << '\n';
        return;
    }

    std::cout << "\n========== SAVED COMPLAINTS ==========\n";

    int result;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        auto getText = [stmt](int column) -> const char* {
            const unsigned char* value = sqlite3_column_text(stmt, column);
            return value ? reinterpret_cast<const char*>(value) : "";
        };

        std::cout
            << "ID: " << sqlite3_column_int(stmt, 0)
            << " | Tracking: " << getText(1)
            << " | Title: " << getText(2)
            << " | Category: " << getText(3)
            << " | Priority: " << sqlite3_column_int(stmt, 4)
            << " | Status: " << getText(5)
            << " | Citizen ID: " << sqlite3_column_int(stmt, 6)
            << " | Area ID: " << sqlite3_column_int(stmt, 7)
            << '\n';
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error reading complaints: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
}

// ========================================
// COUNT COMPLAINTS
// ========================================

int Database::getComplaintCount() {

    if (db == nullptr) return 0;

    const char* sql = "SELECT COUNT(*) FROM complaints;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not count complaints: "
                  << sqlite3_errmsg(db) << '\n';
        return 0;
    }

    int count = 0;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

// ========================================
// LOAD COMPLAINTS FROM DATABASE
// ========================================

std::vector<Complaint> Database::getAllComplaints() {

    std::vector<Complaint> complaints;

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return complaints;
    }

    const char* sql =
        "SELECT id, tracking_code, citizen_id, title, description, "
        "category, priority_score, area_id, photo_path, status, "
        "created_at, updated_at, latitude, longitude, ai_category, "
        "ai_confidence, ai_image_label, rejection_reason, duplicate_of, "
        "resolution_note, escalated "
        "FROM complaints ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Complaint loading failed: "
                  << sqlite3_errmsg(db) << '\n';
        return complaints;
    }

    int result;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        Complaint complaint;

        auto getText = [stmt](int column) -> std::string {
            const unsigned char* value = sqlite3_column_text(stmt, column);

            return value
                ? reinterpret_cast<const char*>(value)
                : "";
        };

        complaint.id = sqlite3_column_int(stmt, 0);
        complaint.trackingCode = getText(1);
        complaint.citizenId = sqlite3_column_int(stmt, 2);
        complaint.title = getText(3);
        complaint.description = getText(4);

        const std::string category = getText(5);

        if (category == "Garbage Collection")
            complaint.category = ComplaintCategory::GarbageCollection;
        else if (category == "Road Damage")
            complaint.category = ComplaintCategory::RoadDamage;
        else if (category == "Water Supply")
            complaint.category = ComplaintCategory::WaterSupply;
        else if (category == "Drainage")
            complaint.category = ComplaintCategory::Drainage;
        else if (category == "Streetlight")
            complaint.category = ComplaintCategory::Streetlight;
        else
            complaint.category = ComplaintCategory::Other;

        complaint.priorityScore = sqlite3_column_int(stmt, 6);
        complaint.areaId = sqlite3_column_int(stmt, 7);
        complaint.photoPath = getText(8);

        const std::string status = getText(9);

        if (status == "Under Review")
            complaint.status = ComplaintStatus::UnderReview;
        else if (status == "Assigned")
            complaint.status = ComplaintStatus::Assigned;
        else if (status == "In Progress")
            complaint.status = ComplaintStatus::InProgress;
        else if (status == "Resolved")
            complaint.status = ComplaintStatus::Resolved;
        else if (status == "Rejected")
            complaint.status = ComplaintStatus::Rejected;
        else
            complaint.status = ComplaintStatus::Submitted;

        complaint.createdAt = getText(10);
        complaint.updatedAt = getText(11);
        complaint.latitude = sqlite3_column_double(stmt, 12);
        complaint.longitude = sqlite3_column_double(stmt, 13);
        complaint.aiSuggestedCategory = getText(14);
        complaint.aiConfidence = sqlite3_column_double(stmt, 15);
        complaint.aiImageLabel = getText(16);

        complaints.push_back(complaint);
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error while loading complaints: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);

    std::cout << "\nLoaded " << complaints.size()
              << " complaints from the database.\n";

    return complaints;
}

// ========================================
// UPDATE COMPLAINT STATUS
// AND SAVE HISTORY + ADMIN ACTION
// ========================================

bool Database::updateComplaintStatus(
    int complaintId,
    ComplaintStatus newStatus,
    int changedByUserId,
    const std::string& notes
) {

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return false;
    }

    if (!execute("BEGIN IMMEDIATE TRANSACTION;"))
        return false;

    // Retrieve current status and tracking code.
    const char* selectSQL =
        "SELECT status, tracking_code, citizen_id "
        "FROM complaints WHERE id = ?;";

    sqlite3_stmt* selectStmt = nullptr;

    if (sqlite3_prepare_v2(db, selectSQL, -1, &selectStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare complaint lookup: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    if (sqlite3_bind_int(selectStmt, 1, complaintId) != SQLITE_OK) {
        std::cerr << "Could not bind complaint ID.\n";
        sqlite3_finalize(selectStmt);
        execute("ROLLBACK;");
        return false;
    }

    int result = sqlite3_step(selectStmt);

    if (result != SQLITE_ROW) {
        std::cerr << "Complaint not found. ID: "
                  << complaintId << '\n';

        sqlite3_finalize(selectStmt);
        execute("ROLLBACK;");
        return false;
    }

    const unsigned char* oldText = sqlite3_column_text(selectStmt, 0);
    const unsigned char* codeText = sqlite3_column_text(selectStmt, 1);

    std::string oldStatus =
        oldText ? reinterpret_cast<const char*>(oldText) : "";

    std::string complaintCode =
        codeText ? reinterpret_cast<const char*>(codeText) : "";
    int citizenId = sqlite3_column_int(selectStmt, 2);

    sqlite3_finalize(selectStmt);

    std::string newStatusText = toString(newStatus);

    if (oldStatus == newStatusText) {
        std::cout << "Complaint already has this status.\n";
        execute("ROLLBACK;");
        return false;
    }

    // Update complaint.
    const char* updateSQL =
        "UPDATE complaints "
        "SET status = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?;";

    sqlite3_stmt* updateStmt = nullptr;

    if (sqlite3_prepare_v2(db, updateSQL, -1, &updateStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare status update: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    int bindResult = sqlite3_bind_text(
        updateStmt, 1, newStatusText.c_str(), -1, SQLITE_TRANSIENT
    );

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(updateStmt, 2, complaintId);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Could not bind status update values: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(updateStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (result != SQLITE_DONE) {
        std::cerr << "Complaint status update failed: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    // Phase 3 (11 Nov 2026, handoff-2): keep the lifecycle columns
    // in step with the status, in the same transaction. A rejection
    // stores its reason on the complaint (the API requires one);
    // reopening a closed complaint clears the rejection reason,
    // the duplicate link and the resolution note, so the case
    // comes back to triage clean.
    if (newStatus == ComplaintStatus::Rejected) {
        const char* reasonSQL =
            "UPDATE complaints SET rejection_reason = ? WHERE id = ?;";
        sqlite3_stmt* reasonStmt = nullptr;
        if (sqlite3_prepare_v2(db, reasonSQL, -1, &reasonStmt, nullptr) != SQLITE_OK) {
            std::cerr << "Could not prepare rejection reason update: "
                      << sqlite3_errmsg(db) << '\n';
            execute("ROLLBACK;");
            return false;
        }
        if (sqlite3_bind_text(reasonStmt, 1, notes.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int(reasonStmt, 2, complaintId) != SQLITE_OK) {
            std::cerr << "Could not bind rejection reason values: "
                      << sqlite3_errmsg(db) << '\n';
            sqlite3_finalize(reasonStmt);
            execute("ROLLBACK;");
            return false;
        }
        result = sqlite3_step(reasonStmt);
        sqlite3_finalize(reasonStmt);
        if (result != SQLITE_DONE) {
            std::cerr << "Rejection reason update failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute("ROLLBACK;");
            return false;
        }
    } else if (newStatus == ComplaintStatus::UnderReview &&
               (oldStatus == "Resolved" || oldStatus == "Rejected")) {
        if (!execute("UPDATE complaints SET rejection_reason = '', "
                     "duplicate_of = '', resolution_note = '' "
                     "WHERE id = " + std::to_string(complaintId) + ";")) {
            std::cerr << "Could not clear closed-case fields on reopen: "
                      << sqlite3_errmsg(db) << '\n';
            execute("ROLLBACK;");
            return false;
        }
    }

    // Insert permanent status history.
    const char* historySQL =
        "INSERT INTO status_history "
        "(complaint_id, changed_by_user_id, old_status, "
        "new_status, notes, created_at) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";

    sqlite3_stmt* historyStmt = nullptr;

    if (sqlite3_prepare_v2(db, historySQL, -1, &historyStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare status history insert: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    bindResult = sqlite3_bind_int(historyStmt, 1, complaintId);

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(historyStmt, 2, changedByUserId);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 3, oldStatus.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 4, newStatusText.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 5, notes.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Could not bind status history values: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(historyStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(historyStmt);
    sqlite3_finalize(historyStmt);

    if (result != SQLITE_DONE) {
        std::cerr << "Status history insertion failed: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    // Save the action in the persistent Stack.
    const char* actionSQL =
        "INSERT INTO admin_actions "
        "(description, complaint_code, previous_status, new_status) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* actionStmt = nullptr;

    if (sqlite3_prepare_v2(db, actionSQL, -1, &actionStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare admin action insert: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    std::string description = "Complaint status changed";

    bindResult = sqlite3_bind_text(
        actionStmt, 1, description.c_str(), -1, SQLITE_TRANSIENT
    );

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(actionStmt, 2, complaintCode.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(actionStmt, 3, oldStatus.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(actionStmt, 4, newStatusText.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Could not bind admin action values: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(actionStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(actionStmt);
    sqlite3_finalize(actionStmt);

    if (result != SQLITE_DONE) {
        std::cerr << "Admin action insertion failed: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    // Notify the citizen in the same transaction: if the status
    // change commits, the notification exists; if it rolls back,
    // so does the notification. No orphan rows either way.
    if (!addNotification(citizenId, complaintId, "status",
            "Your report " + complaintCode + " is now " + newStatusText)) {
        std::cerr << "Status notification insert failed: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    if (!execute("COMMIT;")) {
        execute("ROLLBACK;");
        return false;
    }

    std::cout << "\nComplaint status updated successfully!\n";
    std::cout << "Complaint ID: " << complaintId << '\n';
    std::cout << "Old Status: " << oldStatus << '\n';
    std::cout << "New Status: " << newStatusText << '\n';
    std::cout << "Admin action saved permanently.\n";

    return true;
}

// ========================================
// DISPLAY COMPLAINT STATUS HISTORY
// ========================================

void Database::displayComplaintHistory(int complaintId) {

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return;
    }

    const char* sql =
        "SELECT id, complaint_id, changed_by_user_id, "
        "old_status, new_status, notes, created_at "
        "FROM status_history "
        "WHERE complaint_id = ? "
        "ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not retrieve complaint history: "
                  << sqlite3_errmsg(db) << '\n';
        return;
    }

    if (sqlite3_bind_int(stmt, 1, complaintId) != SQLITE_OK) {
        std::cerr << "Could not bind complaint ID for history query.\n";
        sqlite3_finalize(stmt);
        return;
    }

    std::cout << "\n========== COMPLAINT STATUS HISTORY ==========\n";
    std::cout << "Complaint ID: " << complaintId << "\n\n";

    int count = 0;
    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        auto getText = [stmt](int column) -> std::string {
            const unsigned char* value = sqlite3_column_text(stmt, column);

            return value
                ? reinterpret_cast<const char*>(value)
                : "";
        };

        std::cout
            << "History ID: " << sqlite3_column_int(stmt, 0) << '\n'
            << "Complaint ID: " << sqlite3_column_int(stmt, 1) << '\n'
            << "Changed By User ID: " << sqlite3_column_int(stmt, 2) << '\n'
            << "Old Status: " << getText(3) << '\n'
            << "New Status: " << getText(4) << '\n'
            << "Notes: " << getText(5) << '\n'
            << "Date: " << getText(6) << '\n'
            << "--------------------------------------\n";

        count++;
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error reading complaint history: "
                  << sqlite3_errmsg(db) << '\n';
    }

    if (count == 0) {
        std::cout << "No history records found.\n";
    }

    std::cout << "\nTotal history records: " << count << '\n';

    sqlite3_finalize(stmt);
}

// ========================================
// LOAD COMPLAINT HISTORY INTO VECTOR
// ========================================

std::vector<StatusUpdate> Database::getComplaintHistory(int complaintId) {

    std::vector<StatusUpdate> history;

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return history;
    }

    const char* sql =
        "SELECT id, complaint_id, changed_by_user_id, "
        "old_status, new_status, notes, created_at "
        "FROM status_history "
        "WHERE complaint_id = ? "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "History loading failed: "
                  << sqlite3_errmsg(db) << '\n';
        return history;
    }

    if (sqlite3_bind_int(stmt, 1, complaintId) != SQLITE_OK) {
        std::cerr << "Could not bind complaint ID for history loading.\n";
        sqlite3_finalize(stmt);
        return history;
    }

    auto convertStatus = [](const std::string& status) -> ComplaintStatus {

        if (status == "Under Review")
            return ComplaintStatus::UnderReview;

        if (status == "Assigned")
            return ComplaintStatus::Assigned;

        if (status == "In Progress")
            return ComplaintStatus::InProgress;

        if (status == "Resolved")
            return ComplaintStatus::Resolved;

        if (status == "Rejected")
            return ComplaintStatus::Rejected;

        return ComplaintStatus::Submitted;
    };

    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        StatusUpdate update;

        update.id = sqlite3_column_int(stmt, 0);
        update.complaintId = sqlite3_column_int(stmt, 1);
        update.changedByUserId = sqlite3_column_int(stmt, 2);

        auto getText = [stmt](int column) -> std::string {
            const unsigned char* value = sqlite3_column_text(stmt, column);

            return value
                ? reinterpret_cast<const char*>(value)
                : "";
        };

        update.oldStatus = convertStatus(getText(3));
        update.newStatus = convertStatus(getText(4));
        update.notes = getText(5);
        update.createdAt = getText(6);

        history.push_back(update);
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Error loading complaint history: "
                  << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);

    std::cout << "\nLoaded " << history.size()
              << " history records for Complaint ID "
              << complaintId << ".\n";

    return history;
}

// ========================================
// GET LATEST ADMIN ACTION
// ========================================

bool Database::getLatestAdminAction(AdminAction& action) {

    if (db == nullptr) return false;

    const char* sql =
        "SELECT id, description, complaint_code, "
        "previous_status, new_status "
        "FROM admin_actions "
        "WHERE is_undone = 0 "
        "ORDER BY id DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not retrieve latest admin action: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    int result = sqlite3_step(stmt);

    if (result != SQLITE_ROW) {
        if (result != SQLITE_DONE) {
            std::cerr << "Error reading latest admin action: "
                      << sqlite3_errmsg(db) << '\n';
        }

        sqlite3_finalize(stmt);
        return false;
    }

    auto getText = [stmt](int column) -> std::string {
        const unsigned char* value = sqlite3_column_text(stmt, column);

        return value
            ? reinterpret_cast<const char*>(value)
            : "";
    };

    action.id = sqlite3_column_int(stmt, 0);
    action.description = getText(1);
    action.complaintCode = getText(2);
    action.previousStatus = getText(3);
    action.newStatus = getText(4);

    sqlite3_finalize(stmt);
    return true;
}

// ========================================
// UNDO LATEST PERSISTENT ADMIN ACTION
// ========================================

bool Database::undoLatestAdminAction(
    int changedByUserId,
    const std::string& notes
) {

    if (db == nullptr) {
        std::cerr << "Database is not connected.\n";
        return false;
    }

    if (!execute("BEGIN IMMEDIATE TRANSACTION;"))
        return false;

    // Retrieve the latest action that has not been undone.
    const char* actionSQL =
        "SELECT id, complaint_code, previous_status, new_status "
        "FROM admin_actions "
        "WHERE is_undone = 0 "
        "ORDER BY id DESC LIMIT 1;";

    sqlite3_stmt* actionStmt = nullptr;

    if (sqlite3_prepare_v2(db, actionSQL, -1, &actionStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare latest action lookup: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    int result = sqlite3_step(actionStmt);

    if (result != SQLITE_ROW) {

        if (result != SQLITE_DONE) {
            std::cerr << "Could not read latest admin action: "
                      << sqlite3_errmsg(db) << '\n';
        } else {
            std::cout << "No admin actions available to undo.\n";
        }

        sqlite3_finalize(actionStmt);
        execute("ROLLBACK;");
        return false;
    }

    int actionId = sqlite3_column_int(actionStmt, 0);

    auto getActionText = [actionStmt](int column) -> std::string {
        const unsigned char* value = sqlite3_column_text(actionStmt, column);

        return value
            ? reinterpret_cast<const char*>(value)
            : "";
    };

    std::string complaintCode = getActionText(1);
    std::string previousStatus = getActionText(2);
    std::string newStatus = getActionText(3);

    sqlite3_finalize(actionStmt);

    // Find the complaint associated with the action.
    const char* complaintSQL =
        "SELECT id, status FROM complaints WHERE tracking_code = ?;";

    sqlite3_stmt* complaintStmt = nullptr;

    if (sqlite3_prepare_v2(db, complaintSQL, -1, &complaintStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare complaint lookup: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    if (sqlite3_bind_text(
            complaintStmt, 1, complaintCode.c_str(), -1, SQLITE_TRANSIENT
        ) != SQLITE_OK) {

        std::cerr << "Could not bind complaint tracking code.\n";
        sqlite3_finalize(complaintStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(complaintStmt);

    if (result != SQLITE_ROW) {

        std::cerr << "Complaint for this action was not found.\n";

        sqlite3_finalize(complaintStmt);
        execute("ROLLBACK;");
        return false;
    }

    int complaintId = sqlite3_column_int(complaintStmt, 0);

    const unsigned char* currentText = sqlite3_column_text(complaintStmt, 1);

    std::string currentStatus =
        currentText
            ? reinterpret_cast<const char*>(currentText)
            : "";

    sqlite3_finalize(complaintStmt);

    // Prevent undo if another operation has changed the complaint.
    if (currentStatus != newStatus) {

        std::cerr << "Undo rejected. Complaint status has changed "
                  << "since this action was recorded.\n";

        execute("ROLLBACK;");
        return false;
    }

    // Restore the previous complaint status.
    const char* updateSQL =
        "UPDATE complaints "
        "SET status = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?;";

    sqlite3_stmt* updateStmt = nullptr;

    if (sqlite3_prepare_v2(db, updateSQL, -1, &updateStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare complaint restoration: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    int bindResult = sqlite3_bind_text(
        updateStmt, 1, previousStatus.c_str(), -1, SQLITE_TRANSIENT
    );

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(updateStmt, 2, complaintId);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Could not bind complaint restoration values: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(updateStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (result != SQLITE_DONE || sqlite3_changes(db) != 1) {
        std::cerr << "Could not restore complaint status: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    // Record the undo in permanent status history.
    const char* historySQL =
        "INSERT INTO status_history "
        "(complaint_id, changed_by_user_id, old_status, "
        "new_status, notes, created_at) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";

    sqlite3_stmt* historyStmt = nullptr;

    if (sqlite3_prepare_v2(db, historySQL, -1, &historyStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare undo history record: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    std::string undoNotes = notes.empty()
        ? "Administrator undid the previous action."
        : notes;

    bindResult = sqlite3_bind_int(historyStmt, 1, complaintId);

    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_int(historyStmt, 2, changedByUserId);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 3, newStatus.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 4, previousStatus.c_str(), -1, SQLITE_TRANSIENT);
    if (bindResult == SQLITE_OK)
        bindResult = sqlite3_bind_text(historyStmt, 5, undoNotes.c_str(), -1, SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK) {
        std::cerr << "Could not bind undo history values: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(historyStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(historyStmt);
    sqlite3_finalize(historyStmt);

    if (result != SQLITE_DONE) {
        std::cerr << "Undo history insertion failed: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    // Mark the original action as undone. Keep it for audit purposes.
    const char* markSQL =
        "UPDATE admin_actions "
        "SET is_undone = 1 "
        "WHERE id = ? AND is_undone = 0;";

    sqlite3_stmt* markStmt = nullptr;

    if (sqlite3_prepare_v2(db, markSQL, -1, &markStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Could not prepare admin action update: "
                  << sqlite3_errmsg(db) << '\n';
        execute("ROLLBACK;");
        return false;
    }

    if (sqlite3_bind_int(markStmt, 1, actionId) != SQLITE_OK) {
        std::cerr << "Could not bind admin action ID.\n";
        sqlite3_finalize(markStmt);
        execute("ROLLBACK;");
        return false;
    }

    result = sqlite3_step(markStmt);
    int changedRows = sqlite3_changes(db);
    sqlite3_finalize(markStmt);

    if (result != SQLITE_DONE || changedRows != 1) {
        std::cerr << "Could not mark admin action as undone.\n";
        execute("ROLLBACK;");
        return false;
    }

    if (!execute("COMMIT;")) {
        execute("ROLLBACK;");
        return false;
    }

    std::cout << "\n========== ADMIN UNDO SUCCESSFUL ==========\n";
    std::cout << "Action ID: " << actionId << '\n';
    std::cout << "Complaint: " << complaintCode << '\n';
    std::cout << "Previous Status: " << newStatus << '\n';
    std::cout << "Restored Status: " << previousStatus << '\n';
    std::cout << "Undo saved in database history.\n";

    return true;
}


// ========================================
// API HELPERS
// ========================================

static ComplaintCategory categoryFromString(const std::string& s) {
    if (s == "Garbage Collection") return ComplaintCategory::GarbageCollection;
    if (s == "Road Damage") return ComplaintCategory::RoadDamage;
    if (s == "Water Supply") return ComplaintCategory::WaterSupply;
    if (s == "Drainage") return ComplaintCategory::Drainage;
    if (s == "Streetlight") return ComplaintCategory::Streetlight;
    return ComplaintCategory::Other;
}

static ComplaintStatus statusFromStringDb(const std::string& s) {
    if (s == "Under Review") return ComplaintStatus::UnderReview;
    if (s == "Assigned") return ComplaintStatus::Assigned;
    if (s == "In Progress") return ComplaintStatus::InProgress;
    if (s == "Resolved") return ComplaintStatus::Resolved;
    if (s == "Rejected") return ComplaintStatus::Rejected;
    return ComplaintStatus::Submitted;
}

static bool fillComplaintFromRow(sqlite3_stmt* stmt, Complaint& c) {
    auto getText = [stmt](int column) -> std::string {
        const unsigned char* v = sqlite3_column_text(stmt, column);
        return v ? reinterpret_cast<const char*>(v) : "";
    };
    c.id = sqlite3_column_int(stmt, 0);
    c.trackingCode = getText(1);
    c.citizenId = sqlite3_column_int(stmt, 2);
    c.title = getText(3);
    c.description = getText(4);
    c.category = categoryFromString(getText(5));
    c.priorityScore = sqlite3_column_int(stmt, 6);
    c.areaId = sqlite3_column_int(stmt, 7);
    c.photoPath = getText(8);
    c.status = statusFromStringDb(getText(9));
    c.createdAt = getText(10);
    c.updatedAt = getText(11);
    c.latitude = sqlite3_column_double(stmt, 12);
    c.longitude = sqlite3_column_double(stmt, 13);
    c.aiSuggestedCategory = getText(14);
    c.aiConfidence = sqlite3_column_double(stmt, 15);
    c.aiImageLabel = getText(16);
    c.rejectionReason = getText(17);
    c.duplicateOf = getText(18);
    c.resolutionNote = getText(19);
    c.escalated = sqlite3_column_int(stmt, 20) != 0;
    return true;
}

bool Database::getComplaintById(int complaintId, Complaint& complaint) {
    if (db == nullptr) return false;
    const char* sql =
        "SELECT id, tracking_code, citizen_id, title, description, "
        "category, priority_score, area_id, photo_path, status, "
        "created_at, updated_at, latitude, longitude, ai_category, "
        "ai_confidence, ai_image_label, rejection_reason, duplicate_of, "
        "resolution_note, escalated FROM complaints WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, complaintId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillComplaintFromRow(stmt, complaint);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::getComplaintByTrackingCode(const std::string& trackingCode, Complaint& complaint) {
    if (db == nullptr) return false;
    const char* sql =
        "SELECT id, tracking_code, citizen_id, title, description, "
        "category, priority_score, area_id, photo_path, status, "
        "created_at, updated_at, latitude, longitude, ai_category, "
        "ai_confidence, ai_image_label, rejection_reason, duplicate_of, "
        "resolution_note, escalated FROM complaints WHERE tracking_code = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, trackingCode.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        fillComplaintFromRow(stmt, complaint);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<Area> Database::getAllAreas() {
    std::vector<Area> areas;
    if (db == nullptr) return areas;
    const char* sql = "SELECT id, name, city, province FROM areas ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return areas;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Area a;
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        a.id = sqlite3_column_int(stmt, 0);
        a.name = getText(1);
        a.city = getText(2);
        a.province = getText(3);
        areas.push_back(a);
    }
    sqlite3_finalize(stmt);
    return areas;
}

std::vector<User> Database::getAllUsers() {
    std::vector<User> users;
    if (db == nullptr) return users;
    const char* sql =
        "SELECT id, full_name, email, phone, role, area_id, is_active, password_hash "
        "FROM users ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return users;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        u.id = sqlite3_column_int(stmt, 0);
        u.fullName = getText(1);
        u.email = getText(2);
        u.phone = getText(3);
        std::string role = getText(4);
        u.role = (role == "Admin") ? UserRole::Admin
               : (role == "Staff") ? UserRole::Staff : UserRole::Citizen;
        u.areaId = sqlite3_column_int(stmt, 5);
        u.isActive = sqlite3_column_int(stmt, 6) != 0;
        u.passwordHash = getText(7);
        users.push_back(u);
    }
    sqlite3_finalize(stmt);
    return users;
}

bool Database::getUserByEmail(const std::string& email, User& user) {
    if (db == nullptr) return false;
    const char* sql =
        "SELECT id, full_name, email, phone, role, area_id, is_active, password_hash "
        "FROM users WHERE email = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        user.id = sqlite3_column_int(stmt, 0);
        user.fullName = getText(1);
        user.email = getText(2);
        user.phone = getText(3);
        std::string role = getText(4);
        user.role = (role == "Admin") ? UserRole::Admin
                  : (role == "Staff") ? UserRole::Staff : UserRole::Citizen;
        user.areaId = sqlite3_column_int(stmt, 5);
        user.isActive = sqlite3_column_int(stmt, 6) != 0;
        user.passwordHash = getText(7);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::setUserPasswordHash(int userId, const std::string& hash) {
    if (db == nullptr) return false;
    const char* sql = "UPDATE users SET password_hash = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::createSession(int userId, const std::string& token, const std::string& expiresAt) {
    if (db == nullptr) return false;
    const char* sql = "INSERT INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, userId);
    sqlite3_bind_text(stmt, 3, expiresAt.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::getSessionUser(const std::string& token, User& user) {
    if (db == nullptr) return false;
    // Expired sessions are ignored: expires_at is a UTC timestamp
    // string, so a plain string comparison against SQLite's now works.
    const char* sql =
        "SELECT u.id, u.full_name, u.email, u.phone, u.role, u.area_id, u.is_active, u.password_hash "
        "FROM users u JOIN sessions s ON s.user_id = u.id "
        "WHERE s.token = ? AND s.expires_at > datetime('now') AND u.is_active = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        user.id = sqlite3_column_int(stmt, 0);
        user.fullName = getText(1);
        user.email = getText(2);
        user.phone = getText(3);
        std::string role = getText(4);
        user.role = (role == "Admin") ? UserRole::Admin
                  : (role == "Staff") ? UserRole::Staff : UserRole::Citizen;
        user.areaId = sqlite3_column_int(stmt, 5);
        user.isActive = sqlite3_column_int(stmt, 6) != 0;
        user.passwordHash = getText(7);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::deleteSessionsForUser(int userId) {
    if (db == nullptr) return false;
    const char* sql = "DELETE FROM sessions WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::setUserActive(int userId, bool active) {
    if (db == nullptr) return false;
    const char* sql = "UPDATE users SET is_active = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, active ? 1 : 0);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteSession(const std::string& token) {
    if (db == nullptr) return false;
    const char* sql = "DELETE FROM sessions WHERE token = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::assignComplaint(int complaintId, int staffId, int assignedByAdminId, int& previousStaffId) {
    previousStaffId = 0;
    if (db == nullptr) return false;
    // Look up the complaint and the current assignee first, so the
    // audit trail can name both sides of a reassignment.
    Complaint c;
    if (!getComplaintById(complaintId, c)) {
        std::cerr << "Assignment failed: complaint not found.\n";
        return false;
    }
    int currentStaff = 0;
    bool hasCurrent = getCurrentAssignee(complaintId, currentStaff);
    if (hasCurrent) previousStaffId = currentStaff;
    std::string staffName, prevName;
    if (!getUserFullName(db, staffId, staffName)) {
        std::cerr << "Assignment failed: staff user not found.\n";
        return false;
    }
    if (hasCurrent && !getUserFullName(db, currentStaff, prevName)) prevName = "";

    if (!execute("BEGIN IMMEDIATE TRANSACTION;")) return false;
    auto fail = [&]() { execute("ROLLBACK;"); return false; };
    // Close the previous assignment (if any) so "currently
    // assigned" stays exactly one row; the old row keeps its
    // history through unassigned_at.
    if (hasCurrent) {
        const char* closeSQL =
            "UPDATE assignments SET unassigned_at = CURRENT_TIMESTAMP "
            "WHERE complaint_id = ? AND unassigned_at IS NULL;";
        sqlite3_stmt* closeStmt = nullptr;
        if (sqlite3_prepare_v2(db, closeSQL, -1, &closeStmt, nullptr) != SQLITE_OK) return fail();
        sqlite3_bind_int(closeStmt, 1, complaintId);
        if (sqlite3_step(closeStmt) != SQLITE_DONE) {
            std::cerr << "Could not close previous assignment: " << sqlite3_errmsg(db) << '\n';
            sqlite3_finalize(closeStmt);
            return fail();
        }
        sqlite3_finalize(closeStmt);
    }
    const char* sql =
        "INSERT INTO assignments (complaint_id, staff_id, assigned_by_admin_id) "
        "VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return fail();
    sqlite3_bind_int(stmt, 1, complaintId);
    sqlite3_bind_int(stmt, 2, staffId);
    sqlite3_bind_int(stmt, 3, assignedByAdminId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Assignment failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(stmt);
        return fail();
    }
    sqlite3_finalize(stmt);
    // Audit trail: assignment used to be silent - the timeline
    // never showed who got a case or when it changed hands. A
    // same-status history event records it without touching the
    // undo stack (assignments stay out of admin_actions by design).
    std::string event = hasCurrent
        ? "Reassigned from " + prevName + " to " + staffName
        : "Assigned to " + staffName;
    if (!insertHistoryEvent(db, complaintId, assignedByAdminId, toString(c.status), event)) return fail();
    // Notify the new staff member and the citizen, in the same
    // transaction as the assignment itself.
    if (!addNotification(staffId, complaintId, "assignment",
            "You were assigned " + c.trackingCode + ": " + c.title)) return fail();
    if (!addNotification(c.citizenId, complaintId, "assignment",
            "Your report " + c.trackingCode + " was assigned to " + staffName)) return fail();
    if (!execute("COMMIT;")) return fail();
    return true;
}

std::vector<Complaint> Database::getComplaintsAssignedTo(int staffId) {
    std::vector<Complaint> assigned;
    if (db == nullptr) return assigned;
    // "Currently assigned" = the latest assignment row for the
    // complaint (nothing ever sets unassigned_at, so the newest row
    // wins on re-assignment) whose staff is this user and which was
    // never unassigned. Ordered by assignments.id = assignment order.
    const char* sql =
        "SELECT c.id, c.tracking_code, c.citizen_id, c.title, c.description, "
        "c.category, c.priority_score, c.area_id, c.photo_path, c.status, "
        "c.created_at, c.updated_at, c.latitude, c.longitude, c.ai_category, "
        "c.ai_confidence, c.ai_image_label, c.rejection_reason, c.duplicate_of, "
        "c.resolution_note, c.escalated FROM complaints c "
        "JOIN assignments a ON a.complaint_id = c.id "
        "WHERE a.staff_id = ? AND a.unassigned_at IS NULL "
        "AND a.id = (SELECT MAX(a2.id) FROM assignments a2 "
        "WHERE a2.complaint_id = a.complaint_id) "
        "ORDER BY a.id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return assigned;
    sqlite3_bind_int(stmt, 1, staffId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Complaint c;
        fillComplaintFromRow(stmt, c);
        assigned.push_back(c);
    }
    sqlite3_finalize(stmt);
    return assigned;
}

std::vector<AdminAction> Database::getAllAdminActions() {
    std::vector<AdminAction> actions;
    if (db == nullptr) return actions;
    const char* sql =
        "SELECT id, description, complaint_code, previous_status, new_status, is_undone "
        "FROM admin_actions ORDER BY id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return actions;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AdminAction a;
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        a.id = sqlite3_column_int(stmt, 0);
        a.description = getText(1);
        a.complaintCode = getText(2);
        a.previousStatus = getText(3);
        a.newStatus = getText(4);
        a.isUndone = sqlite3_column_int(stmt, 5) != 0;
        actions.push_back(a);
    }
    sqlite3_finalize(stmt);
    return actions;
}


// ========================================
// PHASE 3 - LIFECYCLE (11 Nov 2026)
// ========================================
// Duplicates, escalation, progress notes and in-app notifications.
// Every method below runs its writes inside one transaction
// together with its audit rows, following the pattern of
// updateComplaintStatus: a failure rolls everything back, so a
// retry never leaves a half-applied change behind.

// A timeline event that does not change the status (assignment,
// reassignment, escalation): same-status history row, so the
// incident timeline explains who did what and when.
static bool insertHistoryEvent(sqlite3* db, int complaintId, int changedByUserId,
                               const std::string& status, const std::string& notes) {
    const char* sql =
        "INSERT INTO status_history "
        "(complaint_id, changed_by_user_id, old_status, new_status, notes, created_at) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool ok = sqlite3_bind_int(stmt, 1, complaintId) == SQLITE_OK &&
              sqlite3_bind_int(stmt, 2, changedByUserId) == SQLITE_OK &&
              sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
              sqlite3_bind_text(stmt, 4, status.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
              sqlite3_bind_text(stmt, 5, notes.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    if (ok) ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) std::cerr << "History event insert failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(stmt);
    return ok;
}

static bool getUserFullName(sqlite3* db, int userId, std::string& name) {
    const char* sql = "SELECT full_name FROM users WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, userId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        name = v ? reinterpret_cast<const char*>(v) : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::getCurrentAssignee(int complaintId, int& staffId) {
    staffId = 0;
    if (db == nullptr) return false;
    const char* sql =
        "SELECT staff_id FROM assignments "
        "WHERE complaint_id = ? AND unassigned_at IS NULL "
        "ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, complaintId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        staffId = sqlite3_column_int(stmt, 0);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::markComplaintDuplicate(int complaintId, const std::string& duplicateOfCode, int changedByUserId, const std::string& notes) {
    if (db == nullptr) return false;
    Complaint c, target;
    if (!getComplaintById(complaintId, c)) return false;
    if (!getComplaintByTrackingCode(duplicateOfCode, target)) return false;
    // A report cannot duplicate itself, duplicate a report that
    // is itself a duplicate (no chains), or be marked duplicate
    // once it is already closed.
    if (target.id == complaintId || !target.duplicateOf.empty() ||
        c.status == ComplaintStatus::Resolved || c.status == ComplaintStatus::Rejected) {
        return false;
    }
    std::string oldStatusText = toString(c.status);
    if (!execute("BEGIN IMMEDIATE TRANSACTION;")) return false;
    auto fail = [&]() { execute("ROLLBACK;"); return false; };
    std::string resolution = "Duplicate of " + duplicateOfCode;
    if (!notes.empty()) resolution += " - " + notes;
    const char* updateSQL =
        "UPDATE complaints SET status = 'Resolved', duplicate_of = ?, "
        "resolution_note = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* updateStmt = nullptr;
    if (sqlite3_prepare_v2(db, updateSQL, -1, &updateStmt, nullptr) != SQLITE_OK) return fail();
    sqlite3_bind_text(updateStmt, 1, duplicateOfCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(updateStmt, 2, resolution.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(updateStmt, 3, complaintId);
    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
        std::cerr << "Duplicate marking failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(updateStmt);
        return fail();
    }
    sqlite3_finalize(updateStmt);
    const char* historySQL =
        "INSERT INTO status_history "
        "(complaint_id, changed_by_user_id, old_status, new_status, notes, created_at) "
        "VALUES (?, ?, ?, 'Resolved', ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt* historyStmt = nullptr;
    if (sqlite3_prepare_v2(db, historySQL, -1, &historyStmt, nullptr) != SQLITE_OK) return fail();
    sqlite3_bind_int(historyStmt, 1, complaintId);
    sqlite3_bind_int(historyStmt, 2, changedByUserId);
    sqlite3_bind_text(historyStmt, 3, oldStatusText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 4, resolution.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(historyStmt) != SQLITE_DONE) {
        std::cerr << "Duplicate history insert failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(historyStmt);
        return fail();
    }
    sqlite3_finalize(historyStmt);
    const char* actionSQL =
        "INSERT INTO admin_actions (description, complaint_code, previous_status, new_status) "
        "VALUES ('Marked as duplicate', ?, ?, 'Resolved');";
    sqlite3_stmt* actionStmt = nullptr;
    if (sqlite3_prepare_v2(db, actionSQL, -1, &actionStmt, nullptr) != SQLITE_OK) return fail();
    sqlite3_bind_text(actionStmt, 1, c.trackingCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(actionStmt, 2, oldStatusText.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(actionStmt) != SQLITE_DONE) {
        std::cerr << "Duplicate admin action insert failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(actionStmt);
        return fail();
    }
    sqlite3_finalize(actionStmt);
    if (!addNotification(c.citizenId, complaintId, "duplicate",
            "Your report " + c.trackingCode + " was marked as a duplicate of " + duplicateOfCode)) return fail();
    if (!execute("COMMIT;")) return fail();
    return true;
}

bool Database::escalateComplaint(int complaintId, int changedByUserId, const std::string& reason) {
    if (db == nullptr || reason.empty()) return false;
    Complaint c;
    if (!getComplaintById(complaintId, c)) return false;
    if (c.escalated) return false;
    if (c.status == ComplaintStatus::Resolved || c.status == ComplaintStatus::Rejected) return false;
    if (!execute("BEGIN IMMEDIATE TRANSACTION;")) return false;
    auto fail = [&]() { execute("ROLLBACK;"); return false; };
    if (!execute("UPDATE complaints SET priority_score = 5, escalated = 1, "
                 "updated_at = CURRENT_TIMESTAMP WHERE id = " + std::to_string(complaintId) + ";")) {
        std::cerr << "Escalation update failed: " << sqlite3_errmsg(db) << '\n';
        return fail();
    }
    if (!insertHistoryEvent(db, complaintId, changedByUserId, toString(c.status),
            "Escalated: " + reason)) return fail();
    int assignee = 0;
    if (getCurrentAssignee(complaintId, assignee)) {
        if (!addNotification(assignee, complaintId, "escalation",
                c.trackingCode + " was escalated: " + reason)) return fail();
    }
    if (!execute("COMMIT;")) return fail();
    return true;
}

bool Database::addComplaintNote(int complaintId, int authorId, const std::string& authorRole, const std::string& body, int& noteId) {
    noteId = 0;
    if (db == nullptr || body.empty()) return false;
    Complaint c;
    if (!getComplaintById(complaintId, c)) return false;
    if (!execute("BEGIN IMMEDIATE TRANSACTION;")) return false;
    auto fail = [&]() { execute("ROLLBACK;"); return false; };
    const char* sql =
        "INSERT INTO complaint_notes (complaint_id, author_id, author_role, body) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return fail();
    sqlite3_bind_int(stmt, 1, complaintId);
    sqlite3_bind_int(stmt, 2, authorId);
    sqlite3_bind_text(stmt, 3, authorRole.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, body.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Note insert failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(stmt);
        return fail();
    }
    sqlite3_finalize(stmt);
    // The bundled sqlite3.h does not declare
    // sqlite3_last_insert_rowid, so read the id back with SQL.
    {
        const char* rowidSQL = "SELECT last_insert_rowid();";
        sqlite3_stmt* rowidStmt = nullptr;
        if (sqlite3_prepare_v2(db, rowidSQL, -1, &rowidStmt, nullptr) == SQLITE_OK &&
            sqlite3_step(rowidStmt) == SQLITE_ROW) {
            noteId = sqlite3_column_int(rowidStmt, 0);
        }
        sqlite3_finalize(rowidStmt);
    }
    // Route the notification to whoever needs to act on the note:
    // a citizen follow-up goes to the assigned staff member (or
    // the admins when nobody is assigned yet); a staff/admin
    // update goes to the citizen who filed the report.
    if (authorRole == "Citizen") {
        int assignee = 0;
        if (getCurrentAssignee(complaintId, assignee)) {
            if (!addNotification(assignee, complaintId, "note",
                    "Citizen added a follow-up on " + c.trackingCode)) return fail();
        } else {
            const char* adminSQL = "SELECT id FROM users WHERE role = 'Admin' AND is_active = 1;";
            sqlite3_stmt* adminStmt = nullptr;
            if (sqlite3_prepare_v2(db, adminSQL, -1, &adminStmt, nullptr) != SQLITE_OK) return fail();
            while (sqlite3_step(adminStmt) == SQLITE_ROW) {
                int adminId = sqlite3_column_int(adminStmt, 0);
                if (!addNotification(adminId, complaintId, "note",
                        "Citizen added a follow-up on " + c.trackingCode)) {
                    sqlite3_finalize(adminStmt);
                    return fail();
                }
            }
            sqlite3_finalize(adminStmt);
        }
    } else {
        if (!addNotification(c.citizenId, complaintId, "note",
                authorRole + " added an update on your report " + c.trackingCode)) return fail();
    }
    if (!execute("COMMIT;")) return fail();
    return true;
}

std::vector<ComplaintNote> Database::getComplaintNotes(int complaintId) {
    std::vector<ComplaintNote> notes;
    if (db == nullptr) return notes;
    const char* sql =
        "SELECT id, complaint_id, author_id, author_role, body, created_at "
        "FROM complaint_notes WHERE complaint_id = ? ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return notes;
    sqlite3_bind_int(stmt, 1, complaintId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ComplaintNote n;
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        n.id = sqlite3_column_int(stmt, 0);
        n.complaintId = sqlite3_column_int(stmt, 1);
        n.authorId = sqlite3_column_int(stmt, 2);
        n.authorRole = getText(3);
        n.body = getText(4);
        n.createdAt = getText(5);
        notes.push_back(n);
    }
    sqlite3_finalize(stmt);
    return notes;
}

bool Database::addNotification(int userId, int complaintId, const std::string& kind, const std::string& message) {
    // Plain INSERT, no transaction of its own: every caller runs
    // inside its own transaction, so the notification commits or
    // rolls back together with the event that caused it.
    if (db == nullptr) return false;
    const char* sql =
        "INSERT INTO notifications (user_id, complaint_id, kind, message) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Notification insert failed: " << sqlite3_errmsg(db) << '\n';
        return false;
    }
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, complaintId);
    sqlite3_bind_text(stmt, 3, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) std::cerr << "Notification insert failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Notification> Database::getNotificationsForUser(int userId) {
    std::vector<Notification> out;
    if (db == nullptr) return out;
    const char* sql =
        "SELECT id, user_id, complaint_id, kind, message, is_read, created_at "
        "FROM notifications WHERE user_id = ? ORDER BY id DESC LIMIT 50;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Notification n;
        auto getText = [stmt](int column) -> std::string {
            const unsigned char* v = sqlite3_column_text(stmt, column);
            return v ? reinterpret_cast<const char*>(v) : "";
        };
        n.id = sqlite3_column_int(stmt, 0);
        n.userId = sqlite3_column_int(stmt, 1);
        n.complaintId = sqlite3_column_int(stmt, 2);
        n.kind = getText(3);
        n.message = getText(4);
        n.isRead = sqlite3_column_int(stmt, 5) != 0;
        n.createdAt = getText(6);
        out.push_back(n);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::markNotificationRead(int notificationId, int userId) {
    if (db == nullptr) return false;
    const char* sql = "UPDATE notifications SET is_read = 1 WHERE id = ? AND user_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, notificationId);
    sqlite3_bind_int(stmt, 2, userId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace civicfix
