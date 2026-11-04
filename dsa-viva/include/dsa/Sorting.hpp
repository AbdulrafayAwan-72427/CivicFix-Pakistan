
#pragma once

#include <vector>
#include "models/models.hpp"

namespace civicfix {

class Sorting {
public:

    // Sort complaints by waiting time
    static void bubbleSortByWaitingTime(
        std::vector<Complaint>& complaints,
        std::vector<int>& waitingDays
    );
    static void countingSortByPriority(
        std::vector<Complaint>& complaints
    );
};

}
