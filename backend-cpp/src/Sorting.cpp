#include <iostream>
#include <vector>
#include "dsa/Sorting.hpp"

namespace civicfix {

// Sort complaints by waiting time (longest wait first)
void Sorting::bubbleSortByWaitingTime(
    std::vector<Complaint>& complaints,
    std::vector<int>& waitingDays
) {

    // Check that both vectors have equal sizes
    if (complaints.size() != waitingDays.size()) {
        std::cout << "Error: Complaint and waiting-day counts do not match!\n";
        return;
    }

    std::size_t n = complaints.size();

    // Bubble Sort algorithm
    for (std::size_t i = 0; i + 1 < n; i++) {

        bool swapped = false;

        for (std::size_t j = 0; j < n - i - 1; j++) {

            // Sort in descending order
            if (waitingDays[j] < waitingDays[j + 1]) {

                std::swap(waitingDays[j], waitingDays[j + 1]);

                // Keep complaints matched with waiting days
                std::swap(complaints[j], complaints[j + 1]);

                swapped = true;
            }
        }

        // Stop if already sorted
        if (!swapped) {
            break;
        }
    }

    std::cout << "Complaints sorted by waiting time successfully!\n";
}

// Sort complaints by priority using Counting Sort
void Sorting::countingSortByPriority(
    std::vector<Complaint>& complaints
) {

    const int MAX_PRIORITY = 5;
    const int MIN_PRIORITY = 1;

    // Step 1: Create frequency array
    std::vector<int> count(MAX_PRIORITY + 1, 0);

    // Step 2: Count each priority
    for (const Complaint& complaint : complaints) {

        if (complaint.priorityScore < MIN_PRIORITY ||
            complaint.priorityScore > MAX_PRIORITY) {

            std::cout << "Invalid priority score! Expected 1 to 5.\n";
            return;
        }

        count[complaint.priorityScore]++;
    }

    // Step 3: Calculate cumulative counts
    for (int i = MAX_PRIORITY - 1; i >= MIN_PRIORITY; i--) {
        count[i] += count[i + 1];
    }

    // Step 4: Create output array
    std::vector<Complaint> output(complaints.size());

    // Step 5: Place complaints in sorted positions
    // Reverse traversal maintains stability
    for (std::size_t i = complaints.size(); i > 0; i--) {

        const Complaint& complaint = complaints[i - 1];

        int priority = complaint.priorityScore;

        output[--count[priority]] = complaint;
    }

    // Step 6: Copy sorted results back
    complaints = output;

    std::cout << "Complaints sorted by priority successfully!\n";
}

}