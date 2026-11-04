#include <iostream>
#include "dsa/ActionStack.hpp"

namespace civicfix {

// Constructor
ActionStack::ActionStack()
    : top(nullptr), count(0) {
}

// Destructor
ActionStack::~ActionStack() {
    AdminAction removed;

    while (pop(removed)) {
        // Release all remaining nodes.
    }
}

// Add a new action to the top
void ActionStack::push(const AdminAction& action) {

    StackNode* newNode = new StackNode(action);

    newNode->next = top;
    top = newNode;

    count++;

    std::cout << "Action added: "
              << action.description << "\n";
}

// Remove the most recent action
bool ActionStack::pop(AdminAction& removedAction) {

    if (isEmpty()) {
        return false;
    }

    StackNode* temp = top;

    removedAction = temp->data;

    top = top->next;

    delete temp;
    count--;

    return true;
}

// View the latest action without removing it
bool ActionStack::peek(AdminAction& latestAction) const {

    if (isEmpty()) {
        return false;
    }

    latestAction = top->data;

    return true;
}

// Check whether Stack is empty
bool ActionStack::isEmpty() const {
    return top == nullptr;
}

// Return number of actions
int ActionStack::size() const {
    return count;
}

// Display all actions
void ActionStack::display() const {

    if (isEmpty()) {
        std::cout << "Action Stack is empty.\n";
        return;
    }

    StackNode* current = top;

    std::cout << "\nADMIN ACTION HISTORY (Latest First)\n";

    while (current != nullptr) {

        std::cout << current->data.description
                  << " | Complaint: "
                  << current->data.complaintCode
                  << "\n";

        current = current->next;
    }
}

}