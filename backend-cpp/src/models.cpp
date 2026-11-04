
#include "models/models.hpp"

namespace civicfix {

std::string toString(UserRole role) {
    switch (role) {
        case UserRole::Citizen: return "Citizen";
        case UserRole::Admin: return "Admin";
        case UserRole::Staff: return "Staff";
    }
    return "Unknown";
}

std::string toString(ComplaintCategory category) {
    switch (category) {
        case ComplaintCategory::GarbageCollection: return "Garbage Collection";
        case ComplaintCategory::RoadDamage: return "Road Damage";
        case ComplaintCategory::WaterSupply: return "Water Supply";
        case ComplaintCategory::Drainage: return "Drainage";
        case ComplaintCategory::Streetlight: return "Streetlight";
        case ComplaintCategory::Other: return "Other";
    }
    return "Unknown";
}

std::string toString(ComplaintStatus status) {
    switch (status) {
        case ComplaintStatus::Submitted: return "Submitted";
        case ComplaintStatus::UnderReview: return "Under Review";
        case ComplaintStatus::Assigned: return "Assigned";
        case ComplaintStatus::InProgress: return "In Progress";
        case ComplaintStatus::Resolved: return "Resolved";
        case ComplaintStatus::Rejected: return "Rejected";
    }
    return "Unknown";
}

}
