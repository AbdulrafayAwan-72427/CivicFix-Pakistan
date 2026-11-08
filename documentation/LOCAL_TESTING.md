# CivicFix Pakistan — Local Testing Guide

Goal: run the whole site on your own machine, test everything, and
only then worry about a public host. Nothing here needs the internet
except the first `npm install` / `pip install`.

## 0. GitHub upload (do this first, it is safe)

From the project root, in VS Code's terminal (PowerShell):

```
git init
git add .
git status
```

Check that `git status` does NOT show any `.exe` files, `node_modules/`,
or test databases — those are covered by `.gitignore` at the project
root. The real `database/civicfix.db` (48 KB, seeded with CFX-101/102/103)
IS included on purpose, so anyone cloning the repo can test locally
without rebuilding the database.

Then:

```
git commit -m "CivicFix Pakistan: C++ API, DSA viva package, frontend, ML service"
git branch -M main
git remote add origin https://github.com/<your-username>/CivicFix-Pakistan.git
git push -u origin main
```

Create the empty repo on github.com first (no README / .gitignore
there, or you will get a merge conflict — this project already has
both). If `git push` asks for a login, use a GitHub Personal Access
Token as the password, not your GitHub password.

> Do NOT commit a test database you mutated on purpose
> (`api_test.db`, `test_civicfix.db`) — they are gitignored anyway.
> Do NOT commit a built binary (`.exe`) either — they are gitignored anyway.

## 1. Test the C++ API locally (works TODAY — already tested)

Terminal 1 — build, then run. In VS Code press Ctrl+Shift+B and
choose **Build CivicFix API**, or run the build command in
`documentation/API.md`. Then, from the project root:

```
copy database\civicfix.db database\api_test.db
backend-cpp\civicfix_api.exe 8080 database\api_test.db
```

You should see:

```
CivicFix API listening on http://localhost:8080
```

Test it in a browser or with curl:

```
curl http://localhost:8080/api/health
curl http://localhost:8080/api/complaints/track/CFX-102
```

Use `api_test.db` for testing so your real database is not changed
by test complaints / status changes / undos. When you want a fresh
test database, just copy `civicfix.db` over it again.

## 2. Test the React frontend locally

The frontend is built and verified (see `frontend/README.md`).

Terminal 2 — from the project root:

```
cd frontend
npm install
npm run dev
```

Then open the URL Vite prints (normally `http://localhost:5173`).
The frontend talks to the API on `http://localhost:8080` — so
Terminal 1 (the API) must still be running. If the site shows no
complaints, check Terminal 1 first: the API is the part that owns
the data.

Test checklist for the site:
- [ ] Areas load, complaints list loads
- [ ] Submit a complaint → it appears, with a new CFX- code
- [ ] Track it by that code → details + history show
- [ ] Admin: change status Submitted → Under Review → Assigned
      (try Submitted → Resolved too — the API should refuse it with 400)
- [ ] Admin: assign a Staff user, then undo the latest action
- [ ] Stats page numbers match the complaints list
- [ ] Priority sort puts priority 5 first

## 3. Test the ML service locally

The ML service is built and verified (see `ml-service/README.md`).

Terminal 3 — from the project root:

```
cd ml-service
pip install -r requirements.txt
python train.py
uvicorn app:app --port 8000
```

Then:

```
curl http://localhost:8000/health
curl -X POST http://localhost:8000/predict \
  -H "Content-Type: application/json" \
  -d "{\"text\": \"Garbage has not been collected for five days\"}"
```

Note: the ML service is for classification only. In the final
architecture React must NOT call it directly — the C++ backend
calls it. For local testing, calling it directly with curl is fine
to prove the model works; the site itself should still go through
the C++ API.

## 4. The three-terminal setup, at a glance

| Terminal | Command | URL |
|---|---|---|
| 1 — C++ API | `backend-cpp\civicfix_api.exe 8080 database\api_test.db` | http://localhost:8080 |
| 2 — React frontend | `cd frontend && npm run dev` | http://localhost:5173 (Vite default) |
| 3 — ML service | `cd ml-service && uvicorn app:app --port 8000` | http://localhost:8000 |

Keep all three running while you test the site. Stop with Ctrl+C
in each terminal.

## 5. If something does not work

- Site loads but shows nothing → API (Terminal 1) is not running,
  or it is running on a different port. Check `http://localhost:8080/api/health`.
- API says "Could not bind to port 8080" → an old API process is
  still running. Close that terminal, or use a different port and
  set the frontend's API URL to match it.
- Complaints look wrong after testing → you were testing on the
  real database. Delete the test database, copy a fresh
  `civicfix.db` to `api_test.db`, and restart the API.
