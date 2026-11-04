#include <iostream>
#include "dsa/ComplaintQueue.hpp"

namespace civicfix {

ComplaintQueue::ComplaintQueue()
    : front(nullptr), rear(nullptr), count(0) {
}

ComplaintQueue::~ComplaintQueue() {
    Complaint removed;

    while (dequeue(removed)) {
        // Clear all remaining nodes.
    }
}

// Add a complaint at the rear
void ComplaintQueue::enqueue(const Complaint& complaint) {

    QueueNode* newNode = new QueueNode(complaint);

    if (isEmpty()) {
        front = newNode;
        rear = newNode;
    }
    else {
        rear->next = newNode;
        rear = newNode;
    }

    count++;

    std::cout << "Complaint "
              << complaint.trackingCode
              << " added to Queue.\n";
}

// Remove the oldest complaint
bool ComplaintQueue::dequeue(Complaint& removedComplaint) {

    if (isEmpty()) {
        return false;
    }

    QueueNode* temp = front;

    removedComplaint = temp->data;

    front = front->next;

    if (front == nullptr) {
        rear = nullptr;
    }

    delete temp;
    count--;

    return true;
}

// View the oldest complaint without removing it
bool ComplaintQueue::peek(Complaint& nextComplaint) const {

    if (isEmpty()) {
        return false;
    }

    nextComplaint = front->data;

    return true;
}

// Check whether Queue is empty
bool ComplaintQueue::isEmpty() const {
    return front == nullptr;
}

// Return total complaints
int ComplaintQueue::size() const {
    return count;
}

// Display all complaints
void ComplaintQueue::display() const {

    if (isEmpty()) {
        std::cout << "Queue is empty.\n";
        return;
    }

    const QueueNode* current = front;

    std::cout << "\nComplaint Processing Queue:\n";

    while (current != nullptr) {

        std::cout << current->data.trackingCode
                  << " -> ";

        current = current->next;
    }

    std::cout << "NULL\n";
}

}