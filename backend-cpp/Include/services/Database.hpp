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
    bool getComplaintByTrackingCode(const std::string& trackingCode, Complaint& complaint);
    std::vector<Area> getAllAreas();
    std::vector<User> getAllUsers();
    bool assignComplaint(int complaintId, int staffId, int assignedByAdminId);
    std::vector<AdminAction> getAllAdminActions();

    // Persistent Admin Action Stack
    bool getLatestAdminAction(AdminAction& action);

    bool undoLatestAdminAction(
        int changedByUserId,
        const std::string& notes
    );
};

}