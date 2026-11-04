
#include <iostream>
#include "dsa/StatusHistory.hpp"

using namespace std;

namespace civicfix {

// Constructor
StatusHistory::StatusHistory() {
    head = nullptr;
    count = 0;
}

// Destructor
StatusHistory::~StatusHistory() {
    while (head != nullptr) {
        HistoryNode* temp = head;
        head = head->next;
        delete temp;
    }

    count = 0;
}

void StatusHistory::insertAtBeginning(const StatusUpdate& update) {

    HistoryNode* newNode = new HistoryNode(update);

    newNode->next = head;

    head = newNode;

    count++;

    cout << "History record added at beginning.\n";
}

void StatusHistory::insertAtEnd(const StatusUpdate& update) {

    HistoryNode* newNode = new HistoryNode(update);

    // If the list is empty
    if (head == nullptr) {
        head = newNode;
    }
    else {
        HistoryNode* temp = head;

        // Move to the last node
        while (temp->next != nullptr) {
            temp = temp->next;
        }

        // Connect the last node to the new node
        temp->next = newNode;
    }

    count++;

    cout << "History record added at end.\n";
}

bool StatusHistory::insertAtPosition(
    const StatusUpdate& update,
    int position
) {

    // Validate position
    if (position < 1 || position > count + 1) {
        cout << "Invalid position!\n";
        return false;
    }

    // Insert at beginning
    if (position == 1) {
        insertAtBeginning(update);
        return true;
    }

    HistoryNode* newNode = new HistoryNode(update);

    HistoryNode* temp = head;

    // Move to the node before the target position
    for (int i = 1; i < position - 1; i++) {
        temp = temp->next;
    }

    // Connect the new node
    newNode->next = temp->next;
    temp->next = newNode;

    count++;

    cout << "History record inserted at position "
         << position << ".\n";

    return true;
}

bool StatusHistory::deleteAtPosition(int position) {

    // Check whether the list is empty or position is invalid
    if (head == nullptr || position < 1 || position > count) {
        cout << "Invalid position or empty list!\n";
        return false;
    }

    HistoryNode* temp = head;

    // Delete the first node
    if (position == 1) {
        head = head->next;
        delete temp;

        count--;

        cout << "History node deleted successfully.\n";
        return true;
    }

    // Find the node before the target
    for (int i = 1; i < position - 1; i++) {
        temp = temp->next;
    }

    HistoryNode* nodeToDelete = temp->next;

    // Skip the node being deleted
    temp->next = nodeToDelete->next;

    delete nodeToDelete;

    count--;

    cout << "History node deleted successfully.\n";

    return true;
}

bool StatusHistory::searchById(int id) const {

    HistoryNode* temp = head;

    while (temp != nullptr) {

        if (temp->data.id == id) {
            cout << "History record found!\n";
            cout << "Record ID: " << temp->data.id << "\n";
            cout << "Complaint ID: "
                 << temp->data.complaintId << "\n";

            cout << "New Status: "
                 << toString(temp->data.newStatus) << "\n";

            return true;
        }

        temp = temp->next;
    }

    cout << "History record not found!\n";

    return false;
}

void StatusHistory::display() const {

    if (head == nullptr) {
        cout << "Complaint history is empty!\n";
        return;
    }

    HistoryNode* temp = head;

    cout << "\n========== COMPLAINT HISTORY ==========\n";

    while (temp != nullptr) {

        cout << "History ID: " << temp->data.id << "\n";

        cout << "Complaint ID: "
             << temp->data.complaintId << "\n";

        cout << "Old Status: "
             << toString(temp->data.oldStatus) << "\n";

        cout << "New Status: "
             << toString(temp->data.newStatus) << "\n";

        cout << "Notes: " << temp->data.notes << "\n";

        cout << "Date: " << temp->data.createdAt << "\n";

        cout << "--------------------------------------\n";

        temp = temp->next;
    }
}
bool StatusHistory::isEmpty() const {
    return head == nullptr;
}
int StatusHistory::size() const {
    return count;
}


}
