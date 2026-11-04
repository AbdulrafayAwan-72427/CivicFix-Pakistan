#pragma once

#include "models/models.hpp"

namespace civicfix {

struct QueueNode {
    Complaint data;
    QueueNode* next;

    QueueNode(const Complaint& complaint)
        : data(complaint), next(nullptr) {}
};

class ComplaintQueue {
private:
    QueueNode* front;
    QueueNode* rear;
    int count;

public:
    ComplaintQueue();

    ~ComplaintQueue();

    // Prevent unsafe copying
    ComplaintQueue(const ComplaintQueue&) = delete;
    ComplaintQueue& operator=(const ComplaintQueue&) = delete;

    void enqueue(const Complaint& complaint);

    bool dequeue(Complaint& removedComplaint);

    bool peek(Complaint& nextComplaint) const;

    bool isEmpty() const;

    int size() const;

    void display() const;
};

}