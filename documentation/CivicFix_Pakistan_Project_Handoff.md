# CivicFix Pakistan

## Project Handoff and Continuation Guide

**Purpose:** This document records the project architecture, decisions,
completed work, current code structure, development workflow, and next
steps. It is intended to let another AI model or developer continue the
project without having to reconstruct the earlier discussion.

**Last documented:** 4 November 2026\
**Development environment:** Windows, Visual Studio Code, MinGW-w64
(MSYS2 UCRT64), g++, SQLite\
**Project root:** `D:\UNI\Semister_3\DSA\Lab\Sem_proj\CIvicFix-Pakistan`

------------------------------------------------------------------------

# 1. Project overview

**Project name:** CivicFix Pakistan

CivicFix Pakistan is a municipal complaint management platform. Citizens
can report public-service problems such as:

-   Garbage collection issues
-   Damaged roads
-   Water supply problems
-   Drainage and sewerage issues
-   Streetlight faults
-   Other municipal complaints

The system is intended to support citizens, administrators, and
municipal staff. It combines a C++ application backend, a persistent
SQLite database, custom Data Structures and Algorithms (DSA), a React
frontend, and a separate machine-learning service.

The project is being developed as an advanced semester DSA project. The
goal is to demonstrate meaningful use of DSA inside a practical
application, not merely show isolated algorithm examples.

## Core development rule

The main application and its business logic must be written in **C++**.

Python is reserved for the machine-learning service using Scikit-learn.
It must not replace the C++ application backend or DSA implementation.

The user wants to understand the implementation and proceed step by
step. Future assistance should explain changes, preserve existing code,
and avoid replacing working project components with incomplete examples.

------------------------------------------------------------------------

# 2. Technology stack and responsibilities

  -----------------------------------------------------------------------
  Component               Technology              Responsibility
  ----------------------- ----------------------- -----------------------
  Main backend            C++17                   Application logic,
                                                  complaint management,
                                                  DSA, database access,
                                                  API

  Database                SQLite                  Persistent users,
                                                  areas, complaints,
                                                  assignments, history,
                                                  predictions, admin
                                                  actions

  HTTP server             cpp-httplib (planned)   Expose backend
                                                  endpoints to the
                                                  frontend

  JSON                    nlohmann/json (planned) JSON request and
                                                  response handling

  Frontend                React                   Citizen, staff, and
                                                  admin user interfaces

  Frontend                Google AI Studio Build  Generate and improve
  generation/refinement   mode                    the React UI

  Machine learning        Python, Scikit-learn    Predict complaint
                                                  category from complaint
                                                  text

  ML API                  FastAPI                 Receive text and return
                                                  predicted category and
                                                  confidence

  Build tools             g++, CMake              Compile and manage the
                                                  C++ project

  Version control         Git                     Track project changes
  -----------------------------------------------------------------------

## Intended architecture

``` text
Citizen / Admin / Staff
          |
          v
     React Frontend
          |
          v
      C++ Backend
          |
          +----------------------------+
          |                            |
          v                            v
 Complaint Management             DSA Engine
          |                            |
          |                    +-------+-------+
          |                    |       |       |
          |                  Queue   Stack   Linked List
          |                    |       |       |
          |                  Graph   Sorting Algorithms
          |                            |
          +-------------+--------------+
                        |
                        v
                   SQLite DB
                        ^
                        |
                C++ calls ML API
                        |
                        v
                Python FastAPI
                        |
                        v
              Scikit-learn Model
```

The React frontend should communicate with the C++ backend. It should
not call the Python ML service directly. The C++ backend controls the
main workflow and calls the Python service when a complaint needs
classification.

------------------------------------------------------------------------

# 3. Project folder structure

The planned structure is:

``` text
CivicFix-Pakistan/
├── .vscode/
│   ├── c_cpp_properties.json
│   ├── tasks.json
│   └── launch.json
│
├── backend-cpp/
│   ├── Include/
│   │   ├── models/
│   │   │   └── models.hpp
│   │   ├── dsa/
│   │   │   ├── ComplaintQueue.hpp
│   │   │   ├── ActionStack.hpp
│   │   │   ├── StatusHistory.hpp
│   │   │   ├── Graph.hpp
│   │   │   └── Sorting.hpp
│   │   └── services/
│   │       └── Database.hpp
│   │
│   ├── src/
│   │   ├── main.cpp
│   │   ├── models.cpp
│   │   ├── ComplaintQueue.cpp
│   │   ├── ActionStack.cpp
│   │   ├── StatusHistory.cpp
│   │   ├── Graph.cpp
│   │   ├── Sorting.cpp
│   │   └── Database.cpp
│   │
│   ├── test/
│   └── civicfix.exe
│
├── ml-service/
├── frontend/
├── database/
│   └── civicfix.db
├── datasets/
└── documentation/
```

The actual project root currently uses the spelling `CIvicFix-Pakistan`
in its Windows path. Do not assume the folder has been renamed.

------------------------------------------------------------------------

# 4. Development environment and setup

The project is being developed on Windows using Visual Studio Code.

Installed tools and known paths:

-   CMake 4.5.0-rc2: `D:\cmake-4.5.0-rc2\bin\cmake.exe`
-   g++ 15.2.0: `C:\msys64\ucrt64\bin\g++.exe`
-   gdb 16.3: `C:\msys64\ucrt64\bin\gdb.exe`
-   SQLite CLI 3.51.1
-   SQLite header: `C:\msys64\ucrt64\include\sqlite3.h`
-   SQLite import library: `C:\msys64\ucrt64\lib\libsqlite3.dll.a`

The SQLite command has been tested successfully:

``` powershell
sqlite3 database/civicfix.db
```

The VS Code workspace root should be the CivicFix project root, not only
the `backend-cpp` folder.

## `.vscode/c_cpp_properties.json`

Current configuration:

``` json
{
    "configurations": [
        {
            "name": "CivicFix",
            "compilerPath": "C:/msys64/ucrt64/bin/g++.exe",
            "intelliSenseMode": "windows-gcc-x64",
            "cppStandard": "c++17",
            "cStandard": "c17",
            "includePath": [
                "${workspaceFolder}/backend-cpp/Include",
                "${workspaceFolder}/backend-cpp/Include/**"
            ]
        }
    ],
    "version": 4
}
```

## `.vscode/tasks.json`

The current build task compiles all existing C++ implementation files
and links SQLite:

``` json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build CivicFix",
            "type": "shell",
            "command": "C:/msys64/ucrt64/bin/g++.exe",
            "args": [
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-pedantic",
                "-g",
                "-I",
                "${workspaceFolder}/backend-cpp/Include",
                "${workspaceFolder}/backend-cpp/src/main.cpp",
                "${workspaceFolder}/backend-cpp/src/models.cpp",
                "${workspaceFolder}/backend-cpp/src/ComplaintQueue.cpp",
                "${workspaceFolder}/backend-cpp/src/ActionStack.cpp",
                "${workspaceFolder}/backend-cpp/src/StatusHistory.cpp",
                "${workspaceFolder}/backend-cpp/src/Graph.cpp",
                "${workspaceFolder}/backend-cpp/src/Sorting.cpp",
                "${workspaceFolder}/backend-cpp/src/Database.cpp",
                "-o",
                "${workspaceFolder}/backend-cpp/civicfix.exe",
                "-lsqlite3"
            ],
            "options": {
                "cwd": "${workspaceFolder}"
            },
            "problemMatcher": ["$gcc"],
            "group": {
                "kind": "build",
                "isDefault": true
            }
        }
    ]
}
```

Build using **Ctrl + Shift + B** in VS Code and choose `Build CivicFix`
if prompted.

## `.vscode/launch.json`

The debugger configuration uses:

-   Type: `cppdbg`
-   Debugger mode: `gdb`
-   Debugger path: `C:/msys64/ucrt64/bin/gdb.exe`
-   Program: `${workspaceFolder}/backend-cpp/civicfix.exe`
-   Pre-launch task: `Build CivicFix`

------------------------------------------------------------------------

# 5. Database design

The SQLite database is located at:

`database/civicfix.db`

The database has been created and the following tables have been
verified:

1.  `users`
2.  `areas`
3.  `complaints`
4.  `status_history`
5.  `assignments`
6.  `ai_predictions`

The C++ `Database::connect()` function enables foreign-key enforcement
for its SQLite connection using:

``` sql
PRAGMA foreign_keys = ON;
```

The `admin_actions` table is also created by the C++ connection code if
it does not already exist. It supports persistent undo operations.

## Table responsibilities

### `users`

Stores citizens, administrators, and staff.

Known columns:

-   `id`
-   `full_name`
-   `email`
-   `phone`
-   `role` (Citizen, Admin, Staff)
-   `area_id`
-   `is_active`

`area_id` references `areas.id`.

### `areas`

Stores municipal areas.

Known columns:

-   `id`
-   `name`
-   `city`
-   `province`

### `complaints`

Stores complaint records.

Known columns:

-   `id`
-   `tracking_code` (unique)
-   `citizen_id`
-   `title`
-   `description`
-   `category`
-   `priority_score` (1 to 5)
-   `area_id`
-   `photo_path`
-   `status`
-   `created_at`
-   `updated_at`

Foreign keys:

-   `citizen_id` references `users.id`
-   `area_id` references `areas.id`

### `status_history`

Stores permanent complaint status changes.

Known columns:

-   `id`
-   `complaint_id`
-   `changed_by_user_id`
-   `old_status`
-   `new_status`
-   `notes`
-   `created_at`

Foreign keys:

-   `complaint_id` references `complaints.id`
-   `changed_by_user_id` references `users.id`

This is an audit trail. Real status history should not be deleted during
an undo operation.

### `assignments`

Stores staff assignment records.

Known columns:

-   `id`
-   `complaint_id`
-   `staff_id`
-   `assigned_by_admin_id`
-   `assigned_at`
-   `unassigned_at`

The IDs reference complaints and users.

### `ai_predictions`

Stores machine-learning predictions.

Known columns:

-   `id`
-   `complaint_id`
-   `predicted_category`
-   `confidence`
-   `model_version`
-   `predicted_at`
-   `admin_override`

`complaint_id` references `complaints.id`.

### `admin_actions`

Created by the C++ database connection code if absent.

Known columns:

-   `id`
-   `description`
-   `complaint_code`
-   `previous_status`
-   `new_status`
-   `created_at`, defaulting to SQLite current timestamp
-   `is_undone`, defaulting to 0

This table persists admin status-changing actions so that the latest
action can be undone, including after an application restart.

------------------------------------------------------------------------

# 6. C++ models

File:

`backend-cpp/Include/models/models.hpp`

The project defines these enums:

``` cpp
enum class UserRole {
    Citizen,
    Admin,
    Staff
};

enum class ComplaintCategory {
    GarbageCollection,
    RoadDamage,
    WaterSupply,
    Drainage,
    Streetlight,
    Other
};

enum class ComplaintStatus {
    Submitted,
    UnderReview,
    Assigned,
    InProgress,
    Resolved,
    Rejected
};
```

The following data structures are defined in namespace `civicfix`:

-   `User`
-   `Area`
-   `Complaint`
-   `StatusUpdate`
-   `Assignment`
-   `AiPrediction`

The header also declares:

``` cpp
std::string toString(UserRole role);
std::string toString(ComplaintCategory category);
std::string toString(ComplaintStatus status);
```

Their implementations are in:

`backend-cpp/src/models.cpp`

Examples of display values include `Garbage Collection`, `Road Damage`,
`Water Supply`, `Submitted`, `Under Review`, and `In Progress`.

------------------------------------------------------------------------

# 7. DSA components implemented

The project uses custom data structures for its main DSA demonstrations.
These implementations should remain part of the project. Do not replace
them with STL containers in the main demonstrations without a clear
reason.

## 7.1 Custom Complaint Queue

Files:

-   `backend-cpp/Include/dsa/ComplaintQueue.hpp`
-   `backend-cpp/src/ComplaintQueue.cpp`

Purpose: process complaints in First In, First Out (FIFO) order.

Implementation:

-   Singly linked list nodes.
-   Each node stores a `Complaint`.
-   `front` points to the next complaint to process.
-   `rear` points to the newest complaint.
-   `count` tracks the number of elements.

Methods:

-   Constructor and destructor
-   `enqueue(const Complaint&)`
-   `dequeue(Complaint&)`
-   `peek(Complaint&) const`
-   `isEmpty() const`
-   `size() const`
-   `display() const`

The destructor removes all remaining nodes through dequeue operations.

Copy construction and copy assignment are deleted to prevent unsafe
shallow copying of the linked-list pointers.

Expected behavior:

-   Enqueue at rear.
-   Dequeue from front.
-   Peek without removing.
-   Reset rear to null when the final node is removed.

## 7.2 Custom Admin Action Stack

Files:

-   `backend-cpp/Include/dsa/ActionStack.hpp`
-   `backend-cpp/src/ActionStack.cpp`

Purpose: demonstrate Last In, First Out (LIFO) behavior for admin
actions.

`AdminAction` contains:

-   `id`
-   `description`
-   `complaintCode`
-   `previousStatus`
-   `newStatus`

`StackNode` contains an `AdminAction` and a pointer to the next node.

Methods:

-   Constructor and destructor
-   `push(const AdminAction&)`
-   `pop(AdminAction&)`
-   `peek(AdminAction&) const`
-   `isEmpty() const`
-   `size() const`
-   `display() const`

The stack uses a linked list and maintains a `top` pointer and element
count.

The custom stack demonstrates the DSA concept. The SQLite
`admin_actions` table separately provides persistence. The database
implementation must remain the source of truth for undo after an
application restart.

## 7.3 Custom Status History Linked List

Files:

-   `backend-cpp/Include/dsa/StatusHistory.hpp`
-   `backend-cpp/src/StatusHistory.cpp`

Purpose: store and demonstrate complaint status timeline records in a
linked list.

`HistoryNode` stores a `StatusUpdate` and a next pointer.

Methods:

-   `insertAtBeginning`
-   `insertAtEnd`
-   `insertAtPosition`
-   `deleteAtPosition`
-   `searchById`
-   `display`
-   `isEmpty`
-   `size`

Important data-integrity rule:

The linked list can demonstrate deletion using synthetic/test records.
Do not delete real complaint audit records from the database or from the
actual history timeline. Real audit history must remain permanent.

The current `insertAtEnd()` prints a message each time a record is
inserted. This can make loading a large history noisy. It can be refined
later.

## 7.4 Graph using adjacency lists and BFS

Files:

-   `backend-cpp/Include/dsa/Graph.hpp`
-   `backend-cpp/src/Graph.cpp`

Purpose: represent connections between municipal areas and find paths.

Representation:

-   `areaNames` stores area labels.
-   `adjList` stores adjacency lists.
-   `vertices` tracks the number of areas.

Methods include:

-   `addArea`
-   `addConnection`
-   `displayGraph`
-   `BFS`
-   `findShortestPath`
-   `getAreaCount`
-   `isValidArea`

The graph is undirected. Connections are checked for valid IDs,
self-connections, and duplicates.

BFS uses `std::queue` internally for graph traversal. The separate
custom `ComplaintQueue` is still implemented and demonstrated
independently.

Important explanation for the project report:

BFS finds a path with the fewest edges in an unweighted graph. It does
not calculate actual driving distance or road travel time.

## 7.5 Sorting algorithms

Files:

-   `backend-cpp/Include/dsa/Sorting.hpp`
-   `backend-cpp/src/Sorting.cpp`

Two sorting algorithms are implemented.

### Bubble Sort by waiting time

Method:

``` cpp
static void bubbleSortByWaitingTime(
    std::vector<Complaint>& complaints,
    std::vector<int>& waitingDays
);
```

Behavior:

-   Sorts waiting days in descending order.
-   The oldest complaints appear first.
-   Swaps the corresponding `Complaint` at the same time as its
    waiting-day value.
-   Checks that the two vectors have matching sizes.
-   Uses an early-exit flag if no swaps occur during a pass.

Complexity:

-   Best case: O(n), when already sorted.
-   Average and worst case: O(n²).

### Stable Counting Sort by priority

Method:

``` cpp
static void countingSortByPriority(
    std::vector<Complaint>& complaints
);
```

Behavior:

-   Priority scores must be in the range 1 to 5.
-   Higher priority appears first, so 5 is before 4, and so on.
-   Uses a frequency array.
-   Uses cumulative counts.
-   Traverses the input backwards when placing elements to preserve
    stability.

Complexity:

-   O(n + k), where n is the number of complaints and k is the priority
    range.
-   Here k is fixed at 5.

The implementation validates priority scores before sorting.

------------------------------------------------------------------------

# 8. Database service implementation

Files:

-   `backend-cpp/Include/services/Database.hpp`
-   `backend-cpp/src/Database.cpp`

The `Database` class owns an SQLite connection pointer:

``` cpp
sqlite3* db;
```

The header includes:

``` cpp
#include <sqlite3.h>
#include <string>
#include <vector>
#include "models/models.hpp"
#include "dsa/ActionStack.hpp"
```

The header declares the following functionality:

### Connection and general operations

-   `Database()`
-   `~Database()`
-   `connect(const std::string& path)`
-   `disconnect()`
-   `execute(const std::string& sql)`
-   `getConnection() const`

### Area management

-   `insertArea(name, city, province)`
-   `displayAllAreas()`

### User management

-   `insertUser(const User&)`
-   `displayAllUsers()`

### Complaint management

-   `insertComplaint(const Complaint&)`
-   `displayAllComplaints()`
-   `getAllComplaints()`
-   `getComplaintCount()`

### Status management

-   `updateComplaintStatus(...)`
-   `displayComplaintHistory(int complaintId)`
-   `getComplaintHistory(int complaintId)`

### Persistent admin action stack

-   `getLatestAdminAction(AdminAction& action)`
-   `undoLatestAdminAction(int changedByUserId, const std::string& notes)`

The implementation uses SQLite prepared statements for data insertion
and retrieval. Status updates use transactions.

## Status update transaction

The current status update workflow is designed to:

1.  Start a transaction using `BEGIN IMMEDIATE`.
2.  Retrieve the complaint's current status and tracking code.
3.  Reject a request if the status is unchanged.
4.  Update the complaint's status and `updated_at`.
5.  Insert a permanent row into `status_history`.
6.  Insert an admin action into `admin_actions`.
7.  Commit the transaction.
8.  Roll back if an operation fails.

This keeps the complaint status, audit history, and undo record
consistent.

## Persistent undo workflow

The database undo operation is designed to:

1.  Start a transaction.
2.  Find the newest admin action where `is_undone = 0`.
3.  Find the complaint using its tracking code.
4.  Verify that the complaint's current status still matches the
    action's `new_status`.
5.  Restore the `previous_status`.
6.  Add a new status-history record documenting the undo.
7.  Mark the original action as undone.
8.  Commit the transaction, or roll back on failure.

The status-history row is never deleted. Undo is itself recorded as a
new event.

------------------------------------------------------------------------

# 9. Current `Database.hpp` state and latest recommended changes

The user most recently provided the complete `Database.hpp`. It already
includes `ActionStack.hpp` and declares both persistent admin action
methods.

The recommended updated header adds deleted copy operations because the
class owns a raw SQLite pointer. This prevents two `Database` objects
from accidentally owning and closing the same connection.

Add these declarations to the public section:

``` cpp
Database(const Database&) = delete;
Database& operator=(const Database&) = delete;
```

The complete recommended header is:

``` cpp
#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

#include "models/models.hpp"
#include "dsa/ActionStack.hpp"

namespace civicfix {

class Database {

private:
    sqlite3* db;

public:

    Database();
    ~Database();

    // Prevent unsafe copying of the database connection
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& path);
    void disconnect();

    bool execute(const std::string& sql);

    sqlite3* getConnection() const;

    // Area Management
    bool insertArea(
        const std::string& name,
        const std::string& city,
        const std::string& province
    );

    void displayAllAreas();

    // User Management
    bool insertUser(const User& user);
    void displayAllUsers();

    // Complaint Management
    bool insertComplaint(const Complaint& complaint);
    void displayAllComplaints();

    std::vector<Complaint> getAllComplaints();
    int getComplaintCount();

    // Complaint Status Management
    bool updateComplaintStatus(
        int complaintId,
        ComplaintStatus newStatus,
        int changedByUserId,
        const std::string& notes
    );

    void displayComplaintHistory(int complaintId);

    std::vector<StatusUpdate> getComplaintHistory(int complaintId);

    // Persistent Admin Action Stack
    bool getLatestAdminAction(AdminAction& action);

    bool undoLatestAdminAction(
        int changedByUserId,
        const std::string& notes
    );
};

}
```

Recommended implementation adjustments discussed for `Database.cpp`:

1.  Initialize the connection pointer in the constructor initializer
    list:

``` cpp
Database::Database()
    : db(nullptr) {
}
```

2.  At the start of `connect()`, close an existing connection before
    opening another:

``` cpp
if (db != nullptr) {
    disconnect();
}
```

3.  Use `sqlite3_close_v2()` in `disconnect()` and only set `db` to null
    when closing succeeds. Check the actual current source before
    applying this change, because the exact existing implementation
    should be preserved.

These are recommendations from the latest code review. **The user has
not yet confirmed that these changes have been applied or that the
project builds after them.**

------------------------------------------------------------------------

# 10. Current `main.cpp` and integration progress

File:

`backend-cpp/src/main.cpp`

The main program currently integrates the database and the DSA
demonstrations.

Known functionality:

-   Connects to `database/civicfix.db`.
-   Inserts sample municipal areas such as Gulberg, Model Town, Johar
    Town, DHA Phase 5, and Faisal Town in Lahore, Punjab.
-   Creates a sample citizen:
    -   Name: Ali Khan
    -   Email: `ali@example.com`
    -   Phone: `03001234567`
    -   Role: Citizen
    -   Area ID: 1
-   The sample user already exists in the database with ID 1.
-   Demonstrates complaint insertion and retrieval.
-   Demonstrates the custom complaint queue.
-   Demonstrates the custom admin action stack.
-   Demonstrates status history linked list operations.
-   Demonstrates graph construction and BFS.
-   Demonstrates Bubble Sort and Counting Sort.
-   Loads database complaint history into the custom linked list.
-   Searches a history record.
-   Calls database status update and displays complaint history and all
    complaints.

Known sample complaints:

  Tracking code   Title                   Category              Priority
  --------------- ----------------------- ------------------- ----------
  CFX-101         Garbage not collected   GarbageCollection            3
  CFX-102         Broken road             RoadDamage                   5
  CFX-103         Water shortage          WaterSupply                  4

The sample complaints use citizen ID 1 and area ID 1. Their sample
timestamps were set to 4 November 2026 at 09:00, 09:10, and 09:20.

## Seeding and duplicate-record handling

The database has unique constraints. Re-running the program originally
caused duplicate-email and duplicate-tracking-code errors.

The sample complaint insertion was changed to run only when the
complaint count is zero. The sample user already exists, so attempting
to insert the same email again will correctly fail due to the unique
constraint.

Do not remove unique constraints to silence these messages. Improve the
seed logic so it checks whether a particular sample user, area, or
complaint already exists before inserting it.

## Current status-update test

A fixed test currently exists in `main.cpp`:

``` cpp
bool statusUpdated = database.updateComplaintStatus(
    1,
    ComplaintStatus::UnderReview,
    2,
    "Complaint reviewed by administrator."
);

if (statusUpdated) {
    std::cout << "\nStatus update saved to database.\n";
}

database.displayComplaintHistory(1);
database.displayAllComplaints();
```

This is a test, not the final user workflow. It may fail to add a new
history row on subsequent runs because the complaint may already have
the `UnderReview` status. The update function correctly rejects a
same-status change.

Later, replace this fixed test with an interactive admin menu or a
proper backend API endpoint.

## In-memory versus persistent undo

The original stack demo uses a local in-memory complaint such as CFX-104
and demonstrates restoring its previous status after popping an action.

The database now also has persistent admin action methods. The
persistent database operation should be the real undo mechanism for
actual complaint records. The in-memory custom stack can remain as a DSA
demonstration, but the two workflows should eventually be connected or
clearly distinguished to avoid inconsistent state.

## Known status mapping concern

A helper in `main.cpp` named `statusFromString()` maps status strings to
enum values. Unknown strings currently default to `Submitted`.

This is convenient for an early demonstration but can hide invalid
database values. Later, replace it with validated conversion, such as a
function returning `bool` and writing the converted enum through an
output parameter, or an `std::optional<ComplaintStatus>`.

The same concern applies to category mapping in database retrieval.

------------------------------------------------------------------------

# 11. AI / machine-learning plan

The planned ML feature predicts the complaint category from the
citizen's written description.

Proposed model:

-   Scikit-learn
-   TF-IDF text features
-   Logistic Regression classifier
-   Python FastAPI service

The dataset should include English, Urdu, and Roman Urdu municipal
complaint descriptions.

Suggested starting plan:

-   Begin with around 300 labeled examples for an initial experiment.
-   Work toward 600 to 1,000 balanced examples for a more credible
    evaluation.
-   Include examples across all supported complaint categories.
-   Consider character n-grams alongside word-level features because
    Roman Urdu spelling varies.
-   Use a train/test split.
-   Evaluate with accuracy, precision, recall, macro F1, classification
    report, and confusion matrix.
-   Save the model and vectorizer for inference.
-   Return predicted category, confidence, and model version from the ML
    service.
-   Store predictions in the `ai_predictions` table.

Low-confidence predictions should be sent for human review.
Administrators should be able to override a predicted category. Do not
present the classifier as perfectly accurate.

## Priority scoring

Initial priority should be transparent and rule-based, not described as
an AI prediction without evidence.

Possible factors:

-   Issue type
-   Immediate safety hazard
-   Number of affected households
-   How long the issue has existed
-   Whether an essential service is affected

Priority score range: 1 to 5.

The project can later evaluate whether a learned prioritization model is
justified, but the initial rules should be explainable.

------------------------------------------------------------------------

# 12. Frontend plan

The frontend is planned in React and will be generated or refined using
Google AI Studio Build mode.

The frontend should provide separate experiences for:

-   Citizen
-   Administrator
-   Municipal staff

The UI should communicate with the C++ backend API.

Potential screens include:

-   Citizen registration and login
-   Citizen dashboard
-   Submit complaint
-   Complaint tracking
-   Complaint details and status timeline
-   Admin dashboard
-   Complaint review and prioritization
-   Staff assignment
-   Staff work queue
-   Status update interface
-   Reports and statistics

Do not claim the React frontend or C++ HTTP API is complete. They are
planned stages unless new work has been completed since this handoff was
written.

------------------------------------------------------------------------

# 13. Development workflow and collaboration rules

The user wants to work through the project incrementally.

For future assistance:

1.  Ask for or inspect the current file before rewriting it.
2.  Explain the purpose of the file and any issue found.
3.  Make the smallest safe change that solves the issue.
4.  When a complete replacement is requested, provide the complete file,
    not a fragment that accidentally removes existing functions.
5.  Preserve working DSA implementations and database behavior.
6.  Give exact file paths.
7.  Tell the user when to save and build.
8.  Ask for the actual compiler output if an error occurs.
9.  Do not move to the next subsystem until the current change is
    tested.
10. Avoid large batches of unrelated changes.

Preferred language: simple English, direct and practical. Avoid
unnecessary jargon. Explain important concepts and complexity where
useful.

## Build and test cycle

After a code change:

1.  Save the file.
2.  Press `Ctrl + Shift + B`.
3.  Confirm that the build succeeds.
4.  Run `backend-cpp/civicfix.exe`.
5.  Test the specific feature changed.
6.  If there is an error, share the complete compiler or runtime output
    before changing more files.

Do not assume a code change has been tested merely because the code was
provided.

------------------------------------------------------------------------

# 14. Completed work versus pending work

## Implemented or established

-   Project concept and architecture defined.
-   C++17 selected for the main application.
-   SQLite selected and installed.
-   SQLite database created.
-   Core database tables created and verified.
-   Foreign-key enforcement enabled in the C++ connection.
-   C++ model enums and structures implemented.
-   `toString()` functions implemented.
-   Custom linked-list complaint queue implemented.
-   Custom linked-list admin action stack implemented.
-   Custom status history linked list implemented.
-   Graph adjacency list and BFS implemented.
-   Shortest path using BFS implemented.
-   Bubble Sort by waiting time implemented.
-   Stable Counting Sort by priority implemented.
-   Database insert and retrieval operations implemented for core
    records.
-   Database complaint status update transaction implemented.
-   Permanent status history retrieval and display implemented.
-   Persistent admin action table creation implemented.
-   Persistent latest-action retrieval and undo methods added to the
    database implementation.
-   Main program integrates the DSA demonstrations and database history.
-   Duplicate sample complaint insertion was guarded using complaint
    count.

The user previously confirmed that the program completed its remaining
tests without errors before the latest persistent admin action/header
review. The latest changes described in this handoff still require a
fresh build and test.

## Current immediate task

The latest work is reviewing `Database.hpp` and `Database.cpp`.

The user supplied the current `Database.hpp`. It already declares:

``` cpp
bool getLatestAdminAction(AdminAction& action);

bool undoLatestAdminAction(
    int changedByUserId,
    const std::string& notes
);
```

The latest recommendation is to add deleted copy constructor and copy
assignment declarations to the `Database` class and carefully apply the
connection-safety improvements to `Database.cpp`.

**Next action:** Apply or confirm those changes, then build with
`Ctrl + Shift + B`. Do not proceed to frontend or ML integration until
the C++ database layer compiles and its operations are tested.

## Pending C++ backend work

-   Confirm the latest Database header and source compile together.
-   Test persistent admin action insertion and retrieval.
-   Test undo across application restarts.
-   Confirm the undo operation creates a permanent history row.
-   Integrate persistent undo with the main admin workflow.
-   Replace fixed test actions in `main.cpp` with interactive menus or
    API calls.
-   Improve idempotent sample-data seeding.
-   Validate enum conversions instead of silently defaulting unknown
    values.
-   Add complaint status transition rules.
-   Add staff assignment and unassignment operations.
-   Add authentication and role-based authorization.
-   Add unit tests for DSA and database functions.
-   Add robust SQLite statement and error handling.
-   Integrate cpp-httplib and nlohmann/json.
-   Expose C++ API endpoints for the frontend.

## Pending ML work

-   Prepare and balance the multilingual complaint dataset.
-   Train the TF-IDF + Logistic Regression classifier.
-   Evaluate the model.
-   Build the FastAPI inference service.
-   Connect the C++ backend to the ML service.
-   Store model predictions and administrator overrides.

## Pending frontend work

-   Generate and refine React UI.
-   Implement citizen, admin, and staff screens.
-   Connect frontend requests to the C++ API.
-   Test the complete workflow from complaint submission to resolution.

## Pending final project work

-   End-to-end testing.
-   Documentation and diagrams.
-   DSA complexity analysis.
-   Database schema diagram.
-   API documentation.
-   ML evaluation results.
-   Demonstration dataset and test cases.
-   Final report and presentation.

------------------------------------------------------------------------

# 15. Important engineering considerations for the next developer or AI model

1.  **Do not replace C++ with Python.** Python is only for ML
    inference/training.
2.  **Do not remove the custom DSA implementations.** They are central
    to the semester project.
3.  **Do not delete audit history when undoing an action.** Record the
    reversal as another history event.
4.  **Do not describe BFS as a real-world distance or traffic
    algorithm.** It finds the fewest edges in an unweighted graph.
5.  **Do not use a naive Counting Sort implementation that scans all
    complaints repeatedly for each priority.** The current
    cumulative-count method is O(n + k).
6.  **Keep database operations transactional** when one logical action
    changes multiple tables.
7.  **Use prepared statements** for values supplied by users.
8.  **Validate role permissions in the backend.** Hiding a frontend
    button is not authorization.
9.  **Do not assume the current build passes after untested edits.**
10. **Do not invent missing source code.** Ask the user to provide the
    current file if exact content is needed.
11. **Keep the user involved.** Explain each change and proceed in
    manageable steps.

------------------------------------------------------------------------

# 16. Prompt for continuing with another AI model

The user can paste the following prompt together with this handoff
document:

> I am continuing my semester project, CivicFix Pakistan. Read the
> entire project handoff document first and use it as the source of
> truth for the project architecture, completed work, file paths, design
> decisions, and development preferences.
>
> The main application must remain C++17. Python is only for the
> Scikit-learn ML service. The project uses SQLite, custom DSA
> implementations, and a planned React frontend.
>
> I am a student and want to understand the code. Work with me step by
> step. Do not replace working files with incomplete code. Before
> changing a file, inspect the current version or ask me to paste it.
> Preserve all existing functionality. Give exact file paths and
> complete files when I request complete replacements.
>
> Our immediate next task is to finish reviewing and testing
> `Database.hpp` and `Database.cpp`, especially persistent admin action
> undo. The latest recommendations have not yet been confirmed as
> applied or tested. Start by asking me whether I have applied those
> changes and requesting the build output, or inspect the current files
> if I provide them. Do not assume the latest code compiles.
>
> Use simple English, concise explanations, and practical instructions.
> After each change, tell me to save, build with Ctrl + Shift + B, and
> share any errors. Do not jump ahead to ML or frontend integration
> until the current C++ backend step is stable.

------------------------------------------------------------------------

# 17. Source-of-truth reminder

This handoff records the project state as known on 4 November 2026. It
is a continuation guide, not a substitute for the actual source code or
database.

When continuing:

-   Treat the files currently present in the user's project folder as
    the final authority for exact code.
-   Treat this document as the authority for project intent,
    architecture, design decisions, and development history.
-   If the code differs from this document, inspect the code and clarify
    the difference rather than blindly overwriting it.
-   Update this handoff after major milestones so that future sessions
    remain consistent.
