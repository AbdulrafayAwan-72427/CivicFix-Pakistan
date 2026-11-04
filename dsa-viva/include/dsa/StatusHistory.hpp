
#pragma once

#include "models/models.hpp"

namespace civicfix {

struct HistoryNode {
    StatusUpdate data;
    HistoryNode* next;

    HistoryNode(const StatusUpdate& update)
        : data(update), next(nullptr) {}
};

class StatusHistory {
private:
    HistoryNode* head;
    int count;

public:
    StatusHistory();

    ~StatusHistory();

    void insertAtBeginning(const StatusUpdate& update);

    void insertAtEnd(const StatusUpdate& update);

    bool insertAtPosition(const StatusUpdate& update, int position);

    bool deleteAtPosition(int position);

    bool searchById(int id) const;

    void display() const;

    bool isEmpty() const;

    int size() const;
};

}
