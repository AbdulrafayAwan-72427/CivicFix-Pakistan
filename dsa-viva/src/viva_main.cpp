// CivicFix Pakistan — DSA Viva Demo (STANDALONE).
//
// This program is deliberately separate from the full system. It uses
// NO SQLite, NO REST API, NO React and NO machine learning. It exists
// so a viva examiner can build one folder and see every required
// custom data structure working, in isolation, with checks.
//
// Build (from the project root):
//   Windows (MSYS2): g++ -std=c++17 -Wall -Wextra -I dsa-viva/include
//     dsa-viva/src/viva_main.cpp dsa-viva/src/models.cpp
//     dsa-viva/src/ComplaintQueue.cpp dsa-viva/src/ActionStack.cpp
//     dsa-viva/src/StatusHistory.cpp dsa-viva/src/Graph.cpp
//     dsa-viva/src/Sorting.cpp -o dsa-viva/dsa_viva.exe
//   Linux: same command, output dsa-viva/dsa_viva
// Run it and every section should print [PASS].

#include <iostream>
#include <string>
#include <vector>

#include "dsa/ActionStack.hpp"
#include "dsa/ComplaintQueue.hpp"
#include "dsa/Graph.hpp"
#include "dsa/Sorting.hpp"
#include "dsa/StatusHistory.hpp"
#include "models/models.hpp"

using namespace civicfix;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << "\n";
    if (!ok) failures++;
}

static Complaint makeComplaint(const std::string& code, int priority) {
    Complaint c;
    c.trackingCode = code;
    c.title = code;
    c.priorityScore = priority;
    c.status = ComplaintStatus::Submitted;
    return c;
}

int main() {
    std::cout << "====================================\n";
    std::cout << "  CIVICFIX DSA VIVA DEMO (standalone)\n";
    std::cout << "====================================\n";

    // ---- 1. Complaint Queue: custom linked list, FIFO ----
    std::cout << "\n--- 1. ComplaintQueue (linked list, FIFO) ---\n";
    ComplaintQueue queue;
    queue.enqueue(makeComplaint("CFX-101", 3));
    queue.enqueue(makeComplaint("CFX-102", 5));
    queue.enqueue(makeComplaint("CFX-103", 4));
    check(queue.size() == 3, "Queue size is 3 after 3 enqueues");
    Complaint next;
    check(queue.peek(next) && next.trackingCode == "CFX-101", "Peek returns CFX-101 without removing it");
    Complaint removed;
    check(queue.dequeue(removed) && removed.trackingCode == "CFX-101", "First dequeue removes CFX-101 (FIFO)");
    check(queue.size() == 2, "Queue size is 2 after one dequeue");
    while (queue.dequeue(removed)) {}
    check(queue.isEmpty(), "Queue is empty after removing everything");
    check(!queue.dequeue(removed), "Dequeue on an empty queue fails safely");

    // ---- 2. Admin Action Stack: custom linked list, LIFO ----
    std::cout << "\n--- 2. ActionStack (linked list, LIFO) ---\n";
    ActionStack stack;
    AdminAction a1{1, "Status changed", "CFX-101", "Submitted", "Under Review"};
    AdminAction a2{2, "Assigned to staff", "CFX-102", "Under Review", "Assigned"};
    AdminAction a3{3, "Marked resolved", "CFX-103", "In Progress", "Resolved"};
    stack.push(a1);
    stack.push(a2);
    stack.push(a3);
    AdminAction top;
    check(stack.peek(top) && top.id == 3, "Peek returns the last pushed action (id 3)");
    AdminAction popped;
    check(stack.pop(popped) && popped.id == 3, "First pop removes id 3 (LIFO, undo order)");
    check(stack.size() == 2, "Stack size is 2 after one pop");

    // ---- 3. Status History: custom linked list ----
    std::cout << "\n--- 3. StatusHistory (linked list) ---\n";
    StatusHistory history;
    StatusUpdate h1{1, 101, 1, ComplaintStatus::Submitted, ComplaintStatus::UnderReview, "For review", ""};
    StatusUpdate h2{2, 101, 2, ComplaintStatus::UnderReview, ComplaintStatus::Assigned, "Assigned", ""};
    StatusUpdate h3{3, 101, 2, ComplaintStatus::Assigned, ComplaintStatus::InProgress, "Work started", ""};
    history.insertAtEnd(h1);
    history.insertAtEnd(h3);
    check(history.insertAtPosition(h2, 2), "insertAtPosition(2) succeeds");
    check(history.size() == 3, "History size is 3");
    check(history.searchById(2), "searchById(2) finds the middle record");
    check(history.deleteAtPosition(2), "deleteAtPosition(2) succeeds");
    check(history.size() == 2, "History size is 2 after deletion");
    history.insertAtBeginning(h2);
    check(history.size() == 3, "insertAtBeginning works");

    // ---- 4. Graph + BFS: adjacency list ----
    std::cout << "\n--- 4. Graph + BFS (adjacency list) ---\n";
    Graph graph;
    graph.addArea("Gulberg");
    graph.addArea("Model Town");
    graph.addArea("Johar Town");
    graph.addArea("DHA Phase 5");
    graph.addArea("Faisal Town");
    graph.addConnection(0, 1);
    graph.addConnection(0, 2);
    graph.addConnection(1, 3);
    graph.addConnection(2, 3);
    graph.addConnection(3, 4);
    check(graph.getAreaCount() == 5, "Graph has 5 areas");
    check(graph.isValidArea(4) && !graph.isValidArea(99), "Area index validation works");
    std::cout << "BFS from Gulberg (expect Gulberg, Model Town, Johar Town, DHA Phase 5, Faisal Town):\n";
    graph.BFS(0);
    std::cout << "Shortest path Gulberg -> Faisal Town (fewest edges, NOT road distance):\n";
    graph.findShortestPath(0, 4);

    // ---- 5. Bubble Sort by waiting time ----
    std::cout << "\n--- 5. Bubble Sort (waiting time, longest first) ---\n";
    std::vector<Complaint> waiting = {makeComplaint("CFX-201", 0), makeComplaint("CFX-202", 0),
                                      makeComplaint("CFX-203", 0), makeComplaint("CFX-204", 0)};
    std::vector<int> days = {5, 2, 8, 1};
    Sorting::bubbleSortByWaitingTime(waiting, days);
    check(days == std::vector<int>({8, 5, 2, 1}), "Waiting days sorted 8, 5, 2, 1");
    check(waiting[0].trackingCode == "CFX-203" && waiting[3].trackingCode == "CFX-204",
          "Complaints moved together with their waiting days");

    // ---- 6. Counting Sort by priority (stable, O(n+k)) ----
    std::cout << "\n--- 6. Counting Sort (priority 5 first, stable) ---\n";
    std::vector<Complaint> prio = {makeComplaint("CFX-301", 3), makeComplaint("CFX-302", 5),
                                   makeComplaint("CFX-303", 1), makeComplaint("CFX-304", 4),
                                   makeComplaint("CFX-305", 2), makeComplaint("CFX-306", 5)};
    Sorting::countingSortByPriority(prio);
    check(prio[0].priorityScore == 5 && prio[1].priorityScore == 5 && prio[5].priorityScore == 1,
          "Priorities sorted 5, 5, 4, 3, 2, 1");
    check(prio[0].trackingCode == "CFX-302" && prio[1].trackingCode == "CFX-306",
          "Stable: the two priority-5 complaints kept their original order");

    std::cout << "\n====================================\n";
    if (failures == 0) {
        std::cout << "ALL DSA VIVA CHECKS PASSED\n";
    } else {
        std::cout << "DSA VIVA FAILURES: " << failures << "\n";
    }
    std::cout << "====================================\n";
    return failures == 0 ? 0 : 1;
}
