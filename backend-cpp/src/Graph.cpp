#include <iostream>
#include <queue>
#include <vector>

#include "dsa/Graph.hpp"

namespace civicfix {

// Constructor
Graph::Graph()
    : vertices(0) {
}

// Validate an area index
bool Graph::isValidArea(int areaId) const {
    return areaId >= 0 && areaId < vertices;
}

// Add a new area
void Graph::addArea(const std::string& areaName) {

    areaNames.push_back(areaName);

    // Create an empty neighbor list
    adjList.push_back(std::vector<int>());

    vertices++;

    std::cout << "Area added: "
              << areaName << "\n";
}

// Add an undirected connection
void Graph::addConnection(int area1, int area2) {

    // Validate area indexes
    if (!isValidArea(area1) || !isValidArea(area2)) {
        std::cout << "Invalid area index!\n";
        return;
    }

    // Prevent self-connections
    if (area1 == area2) {
        std::cout << "An area cannot connect to itself!\n";
        return;
    }

    // Prevent duplicate connections
    for (int neighbor : adjList[area1]) {

        if (neighbor == area2) {
            std::cout << "Connection already exists!\n";
            return;
        }
    }

    // Connect both areas
    adjList[area1].push_back(area2);
    adjList[area2].push_back(area1);

    std::cout << areaNames[area1]
              << " connected to "
              << areaNames[area2] << "\n";
}

// Display the graph
void Graph::displayGraph() const {

    std::cout << "\n========== CIVICFIX AREA NETWORK ==========\n";

    if (vertices == 0) {
        std::cout << "No areas available in the graph.\n";
        return;
    }

    for (int i = 0; i < vertices; i++) {

        std::cout << areaNames[i] << ": ";

        if (adjList[i].empty()) {
            std::cout << "No connections";
        }
        else {

            for (std::size_t j = 0; j < adjList[i].size(); j++) {

                int neighbor = adjList[i][j];

                std::cout << areaNames[neighbor];

                if (j < adjList[i].size() - 1) {
                    std::cout << ", ";
                }
            }
        }

        std::cout << "\n";
    }
}

// Breadth-First Search
void Graph::BFS(int startArea) const {

    if (!isValidArea(startArea)) {
        std::cout << "Invalid starting area!\n";
        return;
    }

    std::vector<bool> visited(vertices, false);

    std::queue<int> q;

    visited[startArea] = true;
    q.push(startArea);

    std::cout << "\n========== BFS TRAVERSAL ==========\n";

    while (!q.empty()) {

        int current = q.front();
        q.pop();

        std::cout << areaNames[current] << " -> ";

        for (int neighbor : adjList[current]) {

            if (!visited[neighbor]) {

                visited[neighbor] = true;

                q.push(neighbor);
            }
        }
    }

    std::cout << "END\n";
}

// Find shortest path using BFS
void Graph::findShortestPath(
    int startArea,
    int targetArea
) const {

    if (!isValidArea(startArea) ||
        !isValidArea(targetArea)) {

        std::cout << "Invalid area index!\n";
        return;
    }

    std::vector<bool> visited(vertices, false);

    std::vector<int> parent(vertices, -1);

    std::queue<int> q;

    visited[startArea] = true;
    q.push(startArea);

    // BFS traversal
    while (!q.empty()) {

        int current = q.front();
        q.pop();

        if (current == targetArea) {
            break;
        }

        for (int neighbor : adjList[current]) {

            if (!visited[neighbor]) {

                visited[neighbor] = true;

                parent[neighbor] = current;

                q.push(neighbor);
            }
        }
    }

    // Check whether destination is reachable
    if (!visited[targetArea]) {

        std::cout << "\nNo path exists between these areas.\n";
        return;
    }

    // Reconstruct the shortest path
    std::vector<int> path;

    int current = targetArea;

    while (current != -1) {

        path.push_back(current);

        current = parent[current];
    }

    // Display path in correct order
    std::cout << "\n========== SHORTEST PATH ==========\n";

    for (int i = static_cast<int>(path.size()) - 1; i >= 0; i--) {

        std::cout << areaNames[path[i]];

        if (i > 0) {
            std::cout << " -> ";
        }
    }

    std::cout << "\nTotal connections: "
              << path.size() - 1 << "\n";
}

// Return total number of areas
int Graph::getAreaCount() const {
    return vertices;
}

}