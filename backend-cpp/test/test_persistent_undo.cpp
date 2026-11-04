// CivicFix Pakistan — isolated persistent-undo test harness.
//
// Run this against a COPY of civicfix.db, never the only copy, because
// the undo test deliberately changes CFX-102's status.
// Example (from the project root):
//   cp database/civicfix.db database/test_civicfix.db
//   ./backend-cpp/test_persistent_undo database/test_civicfix.db
//
// Checks, in order:
//   1. The latest pending admin action is CFX-102 Submitted -> Under Review.
//   2. Undo succeeds: CFX-102 returns to Submitted, a new permanent
//      status_history row is added, and the original action is marked
//      is_undone = 1 (it is NOT deleted).
//   3. A second undo finds no pending action and changes nothing.
//   4. Unsafe undo is rejected: after re-applying Under Review, if the
//      complaint is changed to Assigned by another route, undo must
//      refuse and leave status, action and history unchanged.

#include <iostream>
#include <string>
#include <sqlite3.h>

#include "models/models.hpp"
#include "services/Database.hpp"

using namespace civicfix;

static int failures = 0;

static void check(bool ok, const std::string& label) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << "\n";
    if (!ok) failures++;
}

// Run a single-column / single-row text query via the open connection.
static std::string queryText(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    std::string out;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        out = v ? reinterpret_cast<const char*>(v) : "";
    }
    sqlite3_finalize(stmt);
    return out;
}

static int queryInt(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int out = -1;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    if (sqlite3_step(stmt) == SQLITE_ROW) out = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return out;
}

int main(int argc, char* argv[]) {
    std::string dbPath = (argc > 1) ? argv[1] : "database/civicfix.db";
    std::cout << "Testing persistent undo on: " << dbPath << "\n";

    Database database;
    if (!database.connect(dbPath)) return 1;
    sqlite3* conn = database.getConnection();

    // 1. Latest pending action
    AdminAction latest;
    check(database.getLatestAdminAction(latest), "Latest pending admin action exists");
    check(latest.complaintCode == "CFX-102", "Latest action is for CFX-102");
    check(latest.previousStatus == "Submitted" && latest.newStatus == "Under Review",
          "Latest action is Submitted -> Under Review");
    check(queryText(conn, "SELECT status FROM complaints WHERE id = 2;") == "Under Review",
          "CFX-102 is currently Under Review before undo");

    int historyBefore = queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;");
    int actionId = latest.id;

    // 2. Undo it
    check(database.undoLatestAdminAction(2, "Undo test: restored by test harness."),
          "undoLatestAdminAction() returns true");
    check(queryText(conn, "SELECT status FROM complaints WHERE id = 2;") == "Submitted",
          "CFX-102 returned to Submitted after undo");
    check(queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;") == historyBefore + 1,
          "A new permanent status_history row was added by the undo");
    check(queryText(conn, "SELECT new_status FROM status_history WHERE complaint_id = 2 ORDER BY id DESC LIMIT 1;") == "Submitted",
          "Latest history row records the undo to Submitted");
    check(queryInt(conn, "SELECT is_undone FROM admin_actions WHERE id = " + std::to_string(actionId) + ";") == 1,
          "Original admin action is marked is_undone = 1 (not deleted)");
    check(queryInt(conn, "SELECT COUNT(*) FROM admin_actions;") >= 1,
          "Admin actions table still contains the original action");

    // 3. Second undo must not undo the same action again
    int historyAfterUndo = queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;");
    check(!database.undoLatestAdminAction(2, "Second undo attempt."),
          "Second undo is rejected (no pending action left)");
    check(queryText(conn, "SELECT status FROM complaints WHERE id = 2;") == "Submitted",
          "CFX-102 still Submitted after rejected second undo");
    check(queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;") == historyAfterUndo,
          "No extra history row from rejected second undo");

    // 4. Unsafe undo must be rejected
    check(database.updateComplaintStatus(2, ComplaintStatus::UnderReview, 2,
                                         "Re-applied for unsafe-undo test."),
          "Re-apply Under Review to CFX-102 for unsafe test");
    int historyBeforeUnsafe = queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;");
    // Simulate another operation changing the complaint outside admin_actions.
    check(database.execute("UPDATE complaints SET status = 'Assigned' WHERE id = 2;"),
          "Simulate external status change to Assigned");
    check(!database.undoLatestAdminAction(2, "Unsafe undo attempt."),
          "Unsafe undo is rejected when current status != action new_status");
    check(queryText(conn, "SELECT status FROM complaints WHERE id = 2;") == "Assigned",
          "CFX-102 still Assigned after rejected unsafe undo");
    check(queryInt(conn, "SELECT COUNT(*) FROM status_history WHERE complaint_id = 2;") == historyBeforeUnsafe,
          "No history row from rejected unsafe undo");
    check(queryInt(conn, "SELECT COUNT(*) FROM admin_actions WHERE is_undone = 0;") == 1,
          "The re-applied action is still pending (not marked undone)");

    database.disconnect();
    std::cout << (failures == 0 ? "\nALL PERSISTENT UNDO TESTS PASSED\n"
                                : "\nTEST FAILURES: " + std::to_string(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
