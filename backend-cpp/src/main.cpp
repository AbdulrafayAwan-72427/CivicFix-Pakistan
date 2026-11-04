
#include <iostream>
#include <vector>
#include <string>

#include "models/models.hpp"
#include "dsa/ComplaintQueue.hpp"
#include "dsa/ActionStack.hpp"
#include "dsa/StatusHistory.hpp"
#include "dsa/Graph.hpp"
#include "dsa/Sorting.hpp"
#include "services/Database.hpp"

using namespace std;
using namespace civicfix;

// ========================================
// STATUS FROM STRING
// ========================================

ComplaintStatus statusFromString(const string& status) {

    if (status == "Submitted")
        return ComplaintStatus::Submitted;

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
}

// ========================================
// MAIN FUNCTION
// ========================================

int main() {

    cout << "====================================\n";
    cout << "       CIVICFIX PAKISTAN\n";
    cout << "====================================\n";

    // ====================================
    // DATABASE CONNECTION
    // ====================================

    Database database;

    if (!database.connect("database/civicfix.db")) {

        cout << "Unable to start CivicFix database.\n";
        return 1;
    }

    // ====================================
    // DATABASE INSERT TEST
    // ====================================

    cout << "\n========== DATABASE INSERT TEST ==========\n";

    database.insertArea("Gulberg", "Lahore", "Punjab");
    database.insertArea("Model Town", "Lahore", "Punjab");
    database.insertArea("Johar Town", "Lahore", "Punjab");
    database.insertArea("DHA Phase 5", "Lahore", "Punjab");
    database.insertArea("Faisal Town", "Lahore", "Punjab");

    database.displayAllAreas();

    // ====================================
    // USER REGISTRATION TEST
    // ====================================

    cout << "\n========== USER REGISTRATION TEST ==========\n";

    User citizen;

    citizen.fullName = "Ali Khan";
    citizen.email = "ali@example.com";
    citizen.phone = "03001234567";
    citizen.role = UserRole::Citizen;
    citizen.areaId = 1;
    citizen.isActive = true;

    database.insertUser(citizen);
    database.displayAllUsers();

    // ====================================
    // COMPLAINT MANAGEMENT
    // ====================================

    cout << "\n====================================\n";
    cout << "       COMPLAINT MANAGEMENT\n";
    cout << "====================================\n";

    // ------------------------------------
    // INSERT SAMPLE COMPLAINTS ONLY ONCE
    // ------------------------------------

    if (database.getComplaintCount() == 0) {

        cout << "\nNo complaints found. Adding sample complaints.\n";

        // COMPLAINT 1

        Complaint c1;

        c1.trackingCode = "CFX-101";
        c1.citizenId = 1;
        c1.title = "Garbage not collected";
        c1.description =
            "Garbage has not been collected from our street for five days.";

        c1.category = ComplaintCategory::GarbageCollection;
        c1.priorityScore = 3;
        c1.areaId = 1;
        c1.photoPath = "";
        c1.status = ComplaintStatus::Submitted;
        c1.createdAt = "2026-11-04 09:00:00";
        c1.updatedAt = "2026-11-04 09:00:00";

        // COMPLAINT 2

        Complaint c2;

        c2.trackingCode = "CFX-102";
        c2.citizenId = 1;
        c2.title = "Broken road";
        c2.description =
            "A large pothole has damaged the road near the main street.";

        c2.category = ComplaintCategory::RoadDamage;
        c2.priorityScore = 5;
        c2.areaId = 1;
        c2.photoPath = "";
        c2.status = ComplaintStatus::Submitted;
        c2.createdAt = "2026-11-04 09:10:00";
        c2.updatedAt = "2026-11-04 09:10:00";

        // COMPLAINT 3

        Complaint c3;

        c3.trackingCode = "CFX-103";
        c3.citizenId = 1;
        c3.title = "Water shortage";
        c3.description =
            "There has been no water supply in our area since yesterday.";

        c3.category = ComplaintCategory::WaterSupply;
        c3.priorityScore = 4;
        c3.areaId = 1;
        c3.photoPath = "";
        c3.status = ComplaintStatus::Submitted;
        c3.createdAt = "2026-11-04 09:20:00";
        c3.updatedAt = "2026-11-04 09:20:00";

        // SAVE SAMPLE COMPLAINTS

        vector<Complaint> sampleComplaints = {c1, c2, c3};

        for (const Complaint& complaint : sampleComplaints) {
            database.insertComplaint(complaint);
        }

    } else {

        cout << "\nExisting complaints found. Skipping sample insertion.\n";
    }

    // ------------------------------------
    // DISPLAY SAVED COMPLAINTS
    // ------------------------------------

    database.displayAllComplaints();

    // ====================================
    // PERSISTENT STATUS UPDATE TEST
    // ====================================

    cout << "\n====================================\n";
    cout << "   PERSISTENT STATUS UPDATE TEST\n";
    cout << "====================================\n";

    // Change CFX-102 from Submitted to Under Review.
    // If it is already Under Review, the database
    // should prevent a duplicate status update.

    bool statusUpdated = database.updateComplaintStatus(
        2,
        ComplaintStatus::UnderReview,
        2,
        "Complaint reviewed by administrator."
    );

    if (statusUpdated) {
        cout << "\nStatus update saved to database.\n";
    }

    // Display permanent complaint history.
    database.displayComplaintHistory(1);

    // Display updated complaints.
    database.displayAllComplaints();

    // ====================================
    // LINKED LIST: LOAD DATABASE HISTORY
    // ====================================

    cout << "\n====================================\n";
    cout << "   DATABASE HISTORY LINKED LIST\n";
    cout << "====================================\n";

    int historyComplaintId = 1;

    // Retrieve history records from SQLite.
    vector<StatusUpdate> historyRecords =
        database.getComplaintHistory(historyComplaintId);

    // Create a separate Linked List for real database history.
    StatusHistory databaseHistory;

    // Insert each database record into the Linked List.
    for (const StatusUpdate& update : historyRecords) {

        databaseHistory.insertAtEnd(update);
    }

    cout << "\n--- HISTORY LOADED INTO LINKED LIST ---\n";

    databaseHistory.display();

    cout << "\nTotal database history records in Linked List: "
         << databaseHistory.size() << "\n";

    // ------------------------------------
    // SEARCH DATABASE HISTORY
    // ------------------------------------

    if (!historyRecords.empty()) {

        int firstHistoryId = historyRecords.front().id;

        cout << "\n--- SEARCH DATABASE HISTORY RECORD ---\n";

        cout << "Searching for history record ID: "
             << firstHistoryId << "\n";

        databaseHistory.searchById(firstHistoryId);

    } else {

        cout << "\nNo database history records available to search.\n";
    }

    // ====================================
    // LOAD COMPLAINTS FROM DATABASE
    // ====================================

    cout << "\n========== LOADING COMPLAINTS ==========\n";

    ComplaintQueue queue;

    vector<Complaint> savedComplaints = database.getAllComplaints();

    for (const Complaint& complaint : savedComplaints) {

        // Do not add completed or rejected complaints
        // to the active processing Queue.

        if (complaint.status != ComplaintStatus::Resolved &&
            complaint.status != ComplaintStatus::Rejected) {

            queue.enqueue(complaint);
        }
    }

    cout << "\nAll active complaints loaded into Queue.\n";

    // ------------------------------------
    // DISPLAY QUEUE
    // ------------------------------------

    cout << "\n========== COMPLAINT PROCESSING QUEUE ==========\n";

    queue.display();

    cout << "\nTotal complaints in Queue: "
         << queue.size() << "\n";

    // ------------------------------------
    // PEEK OPERATION
    // ------------------------------------

    Complaint next;

    if (queue.peek(next)) {

        cout << "\nNext complaint to process: "
             << next.trackingCode << "\n";

        cout << "Title: "
             << next.title << "\n";

        cout << "Category: "
             << toString(next.category) << "\n";

        cout << "Priority: "
             << next.priorityScore << "\n";
    }

    // ------------------------------------
    // DEQUEUE OPERATION
    // ------------------------------------

    Complaint removed;

    if (queue.dequeue(removed)) {

        cout << "\nRemoved from in-memory Queue: "
             << removed.trackingCode << "\n";
    }

    cout << "\nQUEUE AFTER DEQUEUE\n";

    queue.display();

    cout << "\nRemaining complaints: "
         << queue.size() << "\n";

    // ------------------------------------
    // EMPTY QUEUE TEST
    // ------------------------------------

    cout << "\nREMOVING REMAINING COMPLAINTS\n";

    while (queue.dequeue(removed)) {

        cout << "Removed from Queue: "
             << removed.trackingCode << "\n";
    }

    cout << "\nChecking empty Queue:\n";

    queue.display();

    if (!queue.dequeue(removed)) {

        cout << "Cannot dequeue. Queue is empty.\n";
    }

    cout << "Final Queue size: "
         << queue.size() << "\n";

    cout << "\nNote: These Queue operations do not delete "
         << "complaints from SQLite.\n";

    // ====================================
    // ADMIN ACTION STACK TEST
    // ====================================

    cout << "\n====================================\n";
    cout << "       ADMIN ACTION STACK\n";
    cout << "====================================\n";

    ActionStack actionStack;

    AdminAction a1;

    a1.id = 1;
    a1.description = "Complaint status changed";
    a1.complaintCode = "CFX-101";
    a1.previousStatus = "Submitted";
    a1.newStatus = "Under Review";

    AdminAction a2;

    a2.id = 2;
    a2.description = "Complaint assigned to staff";
    a2.complaintCode = "CFX-102";

    AdminAction a3;

    a3.id = 3;
    a3.description = "Complaint marked resolved";
    a3.complaintCode = "CFX-103";
    a3.previousStatus = "In Progress";
    a3.newStatus = "Resolved";

    cout << "\nADDING ADMIN ACTIONS\n";

    actionStack.push(a1);
    actionStack.push(a2);
    actionStack.push(a3);

    actionStack.display();

    cout << "\nTotal actions: "
         << actionStack.size() << "\n";

    AdminAction latest;

    if (actionStack.peek(latest)) {

        cout << "\nLatest action: "
             << latest.description << "\n";
    }

    AdminAction undone;

    if (actionStack.pop(undone)) {

        cout << "\nUNDO OPERATION\n";

        cout << "Undone action: "
             << undone.description << "\n";

        cout << "Complaint: "
             << undone.complaintCode << "\n";
    }

    actionStack.display();

    cout << "\nRemaining actions: "
         << actionStack.size() << "\n";

    // ====================================
    // REAL COMPLAINT STATUS UNDO
    // ====================================

    cout << "\n====================================\n";
    cout << "       REAL UNDO OPERATION\n";
    cout << "====================================\n";

    Complaint undoComplaint;

    undoComplaint.trackingCode = "CFX-104";
    undoComplaint.title = "Streetlight not working";
    undoComplaint.status = ComplaintStatus::InProgress;

    AdminAction undoAction;

    undoAction.id = 4;
    undoAction.description = "Complaint status changed";
    undoAction.complaintCode = undoComplaint.trackingCode;

    undoAction.previousStatus = "In Progress";
    undoAction.newStatus = "Resolved";

    undoComplaint.status = ComplaintStatus::Resolved;

    actionStack.push(undoAction);

    cout << "\nStatus before Undo: "
         << toString(undoComplaint.status) << "\n";

    AdminAction lastAction;

    if (actionStack.pop(lastAction)) {

        if (lastAction.complaintCode == undoComplaint.trackingCode) {

            undoComplaint.status =
                statusFromString(lastAction.previousStatus);

            cout << "Undo successful!\n";
        }
    }

    cout << "Status after Undo: "
         << toString(undoComplaint.status) << "\n";

    // ====================================
    // STAGE 5: CUSTOM LINKED LIST DEMO
    // ====================================

    cout << "\n====================================\n";
    cout << "       LINKED LIST TEST\n";
    cout << "====================================\n";

    // This is a separate DSA demonstration.
    // It does not modify permanent SQLite history.

    StatusHistory history;

    StatusUpdate h1;

    h1.id = 1;
    h1.complaintId = 101;
    h1.changedByUserId = 1;
    h1.oldStatus = ComplaintStatus::Submitted;
    h1.newStatus = ComplaintStatus::UnderReview;
    h1.notes = "Complaint submitted for review";
    h1.createdAt = "2026-11-03 10:00";

    StatusUpdate h2;

    h2.id = 2;
    h2.complaintId = 101;
    h2.changedByUserId = 2;
    h2.oldStatus = ComplaintStatus::UnderReview;
    h2.newStatus = ComplaintStatus::Assigned;
    h2.notes = "Assigned to municipal staff";
    h2.createdAt = "2026-11-03 11:00";

    StatusUpdate h3;

    h3.id = 3;
    h3.complaintId = 101;
    h3.changedByUserId = 2;
    h3.oldStatus = ComplaintStatus::Assigned;
    h3.newStatus = ComplaintStatus::InProgress;
    h3.notes = "Repair work started";
    h3.createdAt = "2026-11-03 12:00";

    cout << "\n--- INSERT AT END ---\n";

    history.insertAtEnd(h1);
    history.insertAtEnd(h3);

    cout << "\n--- INSERT AT POSITION 2 ---\n";

    history.insertAtPosition(h2, 2);

    history.display();

    cout << "\nTotal history records: "
         << history.size() << "\n";

    cout << "\n--- SEARCH RECORD ---\n";

    history.searchById(2);

    cout << "\n--- DELETE AT POSITION 2 ---\n";

    history.deleteAtPosition(2);

    history.display();

    cout << "\nIs history empty? "
         << (history.isEmpty() ? "Yes" : "No") << "\n";

    cout << "Final list size: "
         << history.size() << "\n";

    // ====================================
    // STAGE 6: GRAPH AND BFS
    // ====================================

    cout << "\n====================================\n";
    cout << "       CIVICFIX GRAPH TEST\n";
    cout << "====================================\n";

    Graph cityGraph;

    cityGraph.addArea("Gulberg");
    cityGraph.addArea("Model Town");
    cityGraph.addArea("Johar Town");
    cityGraph.addArea("DHA Phase 5");
    cityGraph.addArea("Faisal Town");

    cityGraph.addConnection(0, 1);
    cityGraph.addConnection(0, 2);
    cityGraph.addConnection(1, 3);
    cityGraph.addConnection(2, 3);
    cityGraph.addConnection(3, 4);

    cout << "\nTotal Areas: "
         << cityGraph.getAreaCount() << "\n";

    cityGraph.displayGraph();

    cout << "\n--- BFS FROM GULBERG ---\n";

    cityGraph.BFS(0);

    cout << "\n--- SHORTEST PATH ---\n";

    cityGraph.findShortestPath(0, 4);

    // ====================================
    // STAGE 7: BUBBLE SORT
    // ====================================

    cout << "\n====================================\n";
    cout << "       BUBBLE SORT TEST\n";
    cout << "====================================\n";

    vector<Complaint> complaints(4);

    vector<int> waitingDays = {5, 2, 8, 1};

    complaints[0].trackingCode = "CFX-201";
    complaints[0].title = "Garbage not collected";

    complaints[1].trackingCode = "CFX-202";
    complaints[1].title = "Broken road";

    complaints[2].trackingCode = "CFX-203";
    complaints[2].title = "Water shortage";

    complaints[3].trackingCode = "CFX-204";
    complaints[3].title = "Streetlight not working";

    cout << "\n--- BEFORE SORTING ---\n";

    for (size_t i = 0; i < complaints.size(); i++) {

        cout << complaints[i].trackingCode
             << " | " << complaints[i].title
             << " | Waiting: " << waitingDays[i]
             << " days\n";
    }

    Sorting::bubbleSortByWaitingTime(
        complaints,
        waitingDays
    );

    cout << "\n--- AFTER SORTING ---\n";

    for (size_t i = 0; i < complaints.size(); i++) {

        cout << complaints[i].trackingCode
             << " | " << complaints[i].title
             << " | Waiting: " << waitingDays[i]
             << " days\n";
    }

    // ====================================
    // STAGE 8: COUNTING SORT
    // ====================================

    cout << "\n====================================\n";
    cout << "       COUNTING SORT TEST\n";
    cout << "====================================\n";

    vector<Complaint> priorityComplaints(6);

    priorityComplaints[0].trackingCode = "CFX-301";
    priorityComplaints[0].title = "Garbage collection";
    priorityComplaints[0].priorityScore = 3;

    priorityComplaints[1].trackingCode = "CFX-302";
    priorityComplaints[1].title = "Dangerous road damage";
    priorityComplaints[1].priorityScore = 5;

    priorityComplaints[2].trackingCode = "CFX-303";
    priorityComplaints[2].title = "Minor streetlight issue";
    priorityComplaints[2].priorityScore = 1;

    priorityComplaints[3].trackingCode = "CFX-304";
    priorityComplaints[3].title = "Water supply problem";
    priorityComplaints[3].priorityScore = 4;

    priorityComplaints[4].trackingCode = "CFX-305";
    priorityComplaints[4].title = "Drainage issue";
    priorityComplaints[4].priorityScore = 2;

    priorityComplaints[5].trackingCode = "CFX-306";
    priorityComplaints[5].title = "Major road damage";
    priorityComplaints[5].priorityScore = 5;

    cout << "\n--- BEFORE SORTING ---\n";

    for (const Complaint& c : priorityComplaints) {

        cout << c.trackingCode
             << " | " << c.title
             << " | Priority: " << c.priorityScore << "\n";
    }

    Sorting::countingSortByPriority(priorityComplaints);

    cout << "\n--- AFTER SORTING ---\n";

    for (const Complaint& c : priorityComplaints) {

        cout << c.trackingCode
             << " | " << c.title
             << " | Priority: " << c.priorityScore << "\n";
    }

    // ====================================
    // PROGRAM FINISHED
    // ====================================

    database.disconnect();

    cout << "\n====================================\n";
    cout << "       CIVICFIX TESTS COMPLETED\n";
    cout << "====================================\n";

    return 0;
}
