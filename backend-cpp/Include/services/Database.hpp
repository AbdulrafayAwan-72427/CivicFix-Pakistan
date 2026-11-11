#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

#include "models/models.hpp"
#include "dsa/ActionStack.hpp"

namespace civicfix {

class Database {

private:
    sqlite3* db;

public:

    Database();
    ~Database();

    // Prevent unsafe copying of the database connection
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& path);
    void disconnect();

    // Readiness (9 Nov 2026, handoff-2 phase 1; ten tables since
    // phase 3, 11 Nov 2026): true only when all core tables exist. /api/health stays process-only ("the
    // server is up"); /api/ready uses this ("the database can serve
    // requests"). A fresh path that only created admin_actions and
    // sessions used to report healthy while registration failed.
    bool isReady();

    bool execute(const std::string& sql);

    sqlite3* getConnection() const;

    // Area Management
    bool insertArea(
        const std::string& name,
        const std::string& city,
        const std::string& province
    );

    void displayAllAreas();

    // User Management
    bool insertUser(const User& user);
    void displayAllUsers();

    // Complaint Management
    bool insertComplaint(const Complaint& complaint);
    bool updateComplaintPhoto(int complaintId, const std::string& photoPath);
    bool updateComplaintAi(int complaintId, const std::string& aiCategory, double aiConfidence, const std::string& aiImageLabel);
    void displayAllComplaints();

    std::vector<Complaint> getAllComplaints();
    int getComplaintCount();

    // Complaint Status Management
    bool updateComplaintStatus(
        int complaintId,
        ComplaintStatus newStatus,
        int changedByUserId,
        const std::string& notes
    );

    void displayComplaintHistory(int complaintId);

    std::vector<StatusUpdate> getComplaintHistory(int complaintId);

    // API helpers: single-record lookup, lists, assignments
    bool getComplaintById(int complaintId, Complaint& complaint);
    // Authentication: password hashes and login sessions (tokens).
    bool getUserByEmail(const std::string& email, User& user);
    bool setUserPasswordHash(int userId, const std::string& hash);
    bool createSession(int userId, const std::string& token, const std::string& expiresAt);
    bool getSessionUser(const std::string& token, User& user);
    bool deleteSession(const std::string& token);
    // Revocation (9 Nov 2026, handoff-2 phase 2): delete every
    // session of one user, and enable/disable their account.
    bool deleteSessionsForUser(int userId);
    bool setUserActive(int userId, bool active);
    bool getComplaintByTrackingCode(const std::string& trackingCode, Complaint& complaint);
    std::vector<Area> getAllAreas();
    std::vector<User> getAllUsers();
    bool assignComplaint(int complaintId, int staffId, int assignedByAdminId, int& previousStaffId);
    bool getCurrentAssignee(int complaintId, int& staffId);
    // Phase 3 (11 Nov 2026, handoff-2): duplicates, escalation,
    // progress notes and in-app notifications. Each method runs
    // its writes inside one transaction together with its audit
    // rows, so a failure leaves no half-applied change behind.
    bool markComplaintDuplicate(int complaintId, const std::string& duplicateOfCode, int changedByUserId, const std::string& notes);
    bool escalateComplaint(int complaintId, int changedByUserId, const std::string& reason);
    bool addComplaintNote(int complaintId, int authorId, const std::string& authorRole, const std::string& body, int& noteId);
    std::vector<ComplaintNote> getComplaintNotes(int complaintId);
    bool addNotification(int userId, int complaintId, const std::string& kind, const std::string& message);
    std::vector<Notification> getNotificationsForUser(int userId);
    bool markNotificationRead(int notificationId, int userId);
    // Complaints currently assigned to one staff member, in assignment
    // order (assignments.id). A complaint assigned twice counts once,
    // at its latest assignment.
    std::vector<Complaint> getComplaintsAssignedTo(int staffId);
    std::vector<AdminAction> getAllAdminActions();

    // Persistent Admin Action Stack
    bool getLatestAdminAction(AdminAction& action);

    bool undoLatestAdminAction(
        int changedByUserId,
        const std::string& notes
    );
};

}