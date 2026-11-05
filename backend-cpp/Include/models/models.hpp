
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
    // Password hash, format: <salt hex>$<sha256(salt + password)>.
    // NEVER returned by the API (userJson omits it).
    std::string passwordHash;
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
    // Map location picked by the citizen (0,0 = not set; Lahore is
    // around 31.5, 74.3, so 0,0 is never a real complaint location).
    double latitude = 0.0;
    double longitude = 0.0;
    // AI results, filled by the C++ server after it asks the ML
    // service (text category + image detection). Empty = ML service
    // was not reachable when the complaint was saved.
    std::string aiSuggestedCategory;
    double aiConfidence = 0.0;
    std::string aiImageLabel;
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
