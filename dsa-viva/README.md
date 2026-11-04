# CivicFix Pakistan — DSA Viva Package (Standalone)

**Use this folder for the viva. Use the rest of the project for GitHub / live demo.**

This folder is a self-contained copy of the project's custom DSA code.
It needs **no SQLite, no REST API, no React, no ML, and no database
file**. An examiner can build and run it in under a minute and see
every required structure working, with a PASS/FAIL check for each one.

> Snapshot note: these files are copies of the DSA files in
> `backend-cpp/` as of 4 November 2026. The full system uses the
> originals in `backend-cpp/` (the API's `/api/queue` endpoint uses the
> same ComplaintQueue, and `?sort=priority` uses the same Counting
> Sort). If you change a DSA file in `backend-cpp/`, copy it here
> again before the viva so the two do not drift apart.

## Build and run

From the **project root** (not from inside `dsa-viva/`):

### Windows (MSYS2 UCRT64)

```
g++ -std=c++17 -Wall -Wextra -I dsa-viva/include ^
  dsa-viva/src/viva_main.cpp dsa-viva/src/models.cpp ^
  dsa-viva/src/ComplaintQueue.cpp dsa-viva/src/ActionStack.cpp ^
  dsa-viva/src/StatusHistory.cpp dsa-viva/src/Graph.cpp ^
  dsa-viva/src/Sorting.cpp -o dsa-viva/dsa_viva.exe
dsa-viva\dsa_viva.exe
```

### Linux / macOS

```
g++ -std=c++17 -Wall -Wextra -I dsa-viva/include \
  dsa-viva/src/viva_main.cpp dsa-viva/src/models.cpp \
  dsa-viva/src/ComplaintQueue.cpp dsa-viva/src/ActionStack.cpp \
  dsa-viva/src/StatusHistory.cpp dsa-viva/src/Graph.cpp \
  dsa-viva/src/Sorting.cpp -o dsa-viva/dsa_viva
./dsa-viva/dsa_viva
```

Expected last line: `ALL DSA VIVA CHECKS PASSED` (exit code 0).

## What is demonstrated, and what to say in the viva

| # | Structure | Implementation | Key viva point |
|---|---|---|---|
| 1 | ComplaintQueue | Singly linked list, front + rear pointers, count | FIFO: first complaint in is first out. Dequeue does **not** delete anything from a database — this demo has no database at all. Enqueue/dequeue/peek are O(1). |
| 2 | ActionStack | Singly linked list, top pointer, count | LIFO: last action in is first out, which is why it models undo. Push/pop/peek are O(1). The SQLite `admin_actions` table is the *persistent* version of this idea in the full system — do not confuse the two. |
| 3 | StatusHistory | Singly linked list, head pointer, count | insertAtBeginning / insertAtEnd / insertAtPosition / deleteAtPosition / searchById. Deletion here is only on demo data; real audit history in SQLite is never deleted. |
| 4 | Graph + BFS | Adjacency list (`vector<vector<int>>`) | BFS visits areas level by level and gives the path with the **fewest edges**, not the shortest driving distance. BFS is O(V + E). |
| 5 | Bubble Sort | In-place, swaps complaint + waiting days together, early exit if no swaps | Longest-waiting first. Best O(n), average/worst O(n²). Used in the demo; the API's priority sort is the next row. |
| 6 | Counting Sort | Frequency array + cumulative counts, backwards placement for stability | Priority 5 first, 1 last, stable (two priority-5 complaints keep their order). O(n + k) with k = 5, because priorities are only 1–5. |

## Files

```
dsa-viva/
├── README.md                 (this file)
├── include/
│   ├── dsa/                  (ActionStack, ComplaintQueue, Graph, Sorting, StatusHistory)
│   └── models/models.hpp     (Complaint, StatusUpdate, enums — needed by the DSA code)
└── src/
    ├── viva_main.cpp         (the only viva entry point; runs all checks)
    ├── models.cpp
    └── <one .cpp per DSA class>
```

Nothing in this folder opens a database, starts a server, or calls
Python. If a viva question is about the full system, answer from the
main project (`backend-cpp/`, `documentation/API.md`); if it is about
a data structure, answer from this folder.
