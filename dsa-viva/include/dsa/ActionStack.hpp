#pragma once

#include <string>

namespace civicfix {

struct AdminAction {
    int id = 0;
    std::string description;
    std::string complaintCode;
    std::string previousStatus;
    std::string newStatus;
    bool isUndone = false;
};

struct StackNode {
    AdminAction data;
    StackNode* next;

    StackNode(const AdminAction& action)
        : data(action), next(nullptr) {}
};

class ActionStack {
private:
    StackNode* top;
    int count;

public:
    ActionStack();
    ~ActionStack();

    void push(const AdminAction& action);
    bool pop(AdminAction& removedAction);
    bool peek(AdminAction& latestAction) const;

    bool isEmpty() const;
    int size() const;
    void display() const;
};

}