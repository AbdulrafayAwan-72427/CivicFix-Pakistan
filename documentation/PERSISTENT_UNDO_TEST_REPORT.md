# CivicFix Pakistan — Persistent Undo Test Report

**Date tested:** 4 November 2026
**Tested on:** A copy of the uploaded database, never the original file.
The original `database/civicfix.db` was verified unchanged after testing
(md5 matched the backup taken before any test run).

## 1. Database verification (before undo)

From the uploaded database:

| Check | Result |
|---|---|
| `admin_actions` | 1 row: CFX-102, Submitted → Under Review, `is_undone = 0` |
| `status_history` for complaint 2 | Submitted → Under Review, "Complaint reviewed by administrator." |
| `complaints` id 2 (CFX-102) | Under Review |

This confirms the earlier status-update run saved correctly in all three places.

## 2. What was fixed before testing

**`backend-cpp/src/Database.cpp`**
- `insertUser()` now checks whether the email already exists and skips
  re-inserting it, instead of failing with
  `UNIQUE constraint failed: users.email` on every re-run.
- `insertArea()` now checks whether the area already exists before
  inserting. The old `ON CONFLICT(name, city, province) DO NOTHING`
  could never fire because the `areas` table has no UNIQUE constraint
  on those columns — that is also why the uploaded area IDs are
  1, 5, 6, 7, 8 instead of 1–5.

No custom DSA code, models, or project structure were changed.

**`backend-cpp/src/main.cpp`**
- Fixed a misleading comment: the persistent status-update test targets
  CFX-102 (complaint id 2), not CFX-101.

## 3. Build and main-program run

Built with g++ (C++17) from the VS Code task's source list, linked
against SQLite. The main program compiled with no errors and ran to
`CIVICFIX TESTS COMPLETED`, with:
- All 5 areas reported as "Area already exists" (no duplicates added)
- Ali Khan reported as "User already exists" (no UNIQUE error)
- CFX-102 status update correctly rejected as "already has this status"
  (so no duplicate admin action was created)
- Areas still 5, users still 2, complaints unchanged

## 4. Persistent undo test

**Test file:** `backend-cpp/test/test_persistent_undo.cpp`

**How to run it (Windows, from the project root):**
```
copy database\civicfix.db database\test_civicfix.db
g++ -std=c++17 -Wall -Wextra -I backend-cpp/Include ^
  backend-cpp/test/test_persistent_undo.cpp ^
  backend-cpp/src/models.cpp backend-cpp/src/ComplaintQueue.cpp ^
  backend-cpp/src/ActionStack.cpp backend-cpp/src/StatusHistory.cpp ^
  backend-cpp/src/Graph.cpp backend-cpp/src/Sorting.cpp ^
  backend-cpp/src/Database.cpp ^
  -o backend-cpp/test_persistent_undo.exe -lsqlite3
backend-cpp\test_persistent_undo.exe database\test_civicfix.db
```

**Result: ALL TESTS PASSED (19 checks, exit code 0)**

- Undo succeeds: CFX-102 returns from Under Review to Submitted.
- A new permanent `status_history` row records the undo
  (Under Review → Submitted); no history was deleted.
- The original admin action is kept and marked `is_undone = 1`.
- A second undo of the same action is rejected and changes nothing.
- Unsafe undo is rejected: after re-applying Under Review, changing
  CFX-102 to Assigned by another route makes undo refuse, leaving the
  status, the pending action, and history unchanged.

## 5. What this does NOT mean

- The test database used for the unsafe-undo check ends with CFX-102
  in Assigned status and a second pending admin action. That is the
  disposable test copy only — the delivered `database/civicfix.db`
  still has CFX-102 Under Review and one pending action, exactly as
  uploaded.
- The in-memory "REAL UNDO OPERATION" section in `main.cpp` is still
  only a DSA demonstration with a fake CFX-104 complaint. It is not
  the persistent undo, and `main.cpp` does not call
  `undoLatestAdminAction()` on every run (that would undo the latest
  action every time the program starts, which is not wanted).
- The React frontend, C++ REST API, and ML service are still the
  planned later phases and are not in this test report.
