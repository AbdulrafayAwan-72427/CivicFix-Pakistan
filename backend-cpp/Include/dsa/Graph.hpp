#pragma once

#include <vector>
#include <string>

namespace civicfix {

class Graph {
private:
    int vertices;

    std::vector<std::string> areaNames;
    std::vector<std::vector<int>> adjList;

public:
    Graph();

    void addArea(const std::string& areaName);

    void addConnection(int area1, int area2);

    void displayGraph() const;

    void BFS(int startArea) const;

    void findShortestPath(int startArea, int targetArea) const;

    int getAreaCount() const;

    bool isValidArea(int areaId) const;
};

}