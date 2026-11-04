
#pragma once

#include <string>

namespace civicfix {

enum class UserRole {
    Citizen,
    Admin,
    Staff
};

enum class ComplaintCategory {
    GarbageCollection,
    RoadDamage,
    WaterSupply,
    Drainage,
    Streetlight,
    Other
};

enum class ComplaintStatus {
    Submitted,
    UnderReview,
    Assigned,
    InProgress,
    Resolved,
    Rejected
};

struct User {
    int id = 0;
    std::string fullName;
    std::string email;
    std::string phone;
    UserRole role = UserRole::Citizen;
    int areaId = 0;
    bool isActive = true;
};

struct Area {
    int id = 0;
    std::string name;
    std::string city;
    std::string province;
};

struct Complaint {
    int id = 0;
    std::string trackingCode;
    int citizenId = 0;
    std::string title;
    std::string description;
    ComplaintCategory category = ComplaintCategory::Other;
    int priorityScore = 3;
    int areaId = 0;
    std::string photoPath;
    ComplaintStatus status = ComplaintStatus::Submitted;
    std::string createdAt;
    std::string updatedAt;
};

struct StatusUpdate {
    int id = 0;
    int complaintId = 0;
    int changedByUserId = 0;
    ComplaintStatus oldStatus = ComplaintStatus::Submitted;
    ComplaintStatus newStatus = ComplaintStatus::Submitted;
    std::string notes;
    std::string createdAt;
};

struct Assignment {
    int id = 0;
    int complaintId = 0;
    int staffId = 0;
    int assignedByAdminId = 0;
    std::string assignedAt;
    std::string unassignedAt;
};

struct AiPrediction {
    int id = 0;
    int complaintId = 0;
    ComplaintCategory predictedCategory = ComplaintCategory::Other;
    double confidence = 0.0;
    std::string modelVersion;
    std::string predictedAt;
    bool adminOverride = false;
};

std::string toString(UserRole role);
std::string toString(ComplaintCategory category);
std::string toString(ComplaintStatus status);

}
