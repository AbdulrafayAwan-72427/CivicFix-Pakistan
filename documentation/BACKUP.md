# CivicFix Pakistan — backup and restore

A backup is two things, always together: the SQLite database
(`database/civicfix.db`) **and** the `uploads/` folder (complaint
photos). A database without its photos is a set of reports pointing
at missing files; photos without the database are files with no
complaint attached. Back both up on the same run.

## Back up (operator runbook)

Stop the API first — copying a SQLite file while the server is
writing to it can produce a corrupt copy. The server is
single-threaded, but "probably idle" is not a backup strategy.

Windows (PowerShell, from the project folder):

```powershell
# 1. Stop the API (Ctrl+C in its terminal, or stop the process).
# 2. Copy the database and the photos, stamped with today's date:
Copy-Item database\civicfix.db "backups\civicfix-2026-11-09.db"
Copy-Item uploads -Recurse "backups\uploads-2026-11-09"
```

Linux:

```bash
# 1. Stop the API.
# 2. Copy:
cp database/civicfix.db "backups/civicfix-2026-11-09.db"
cp -r uploads "backups/uploads-2026-11-09"
```

Keep at least the last few dated copies, on a different disk or
machine from the one running the API. A backup on the same disk
does not survive a disk failure.

## Restore (operator runbook)

```powershell
# 1. Stop the API.
# 2. Put the backup back (keep the damaged current files aside,
#    do not delete them until the restore is verified):
Copy-Item database\civicfix.db database\civicfix-damaged.db
Copy-Item "backups\civicfix-2026-11-09.db" database\civicfix.db
Remove-Item uploads -Recurse
Copy-Item "backups\uploads-2026-11-09" uploads -Recurse
# 3. Start the API and verify before telling anyone it is back:
#    - GET /api/ready returns 200 {"status":"ready"}
#    - GET /api/areas returns the 5 Lahore areas
#    - Log in as Admin and check the complaint count and one
#      complaint with a photo (the photo must load, byte-identical
#      to the backup — compare file sizes if in doubt).
# 4. Only then tell users the service is back.
```

## Restore test (9 Nov 2026, handoff-2 phase 2)

Tested on this VM, not just documented: a test database with a
real photo upload (a 1×1 PNG, uploaded through the API) was backed
up (database file + `uploads/` copied), the live copies were
removed, the backup was restored to a new path, and the API was
started on it. `/api/ready` returned 200, the complaint and its
tracking code were intact, and `GET /uploads/<file>` served the
photo byte-identical to the original (same size and bytes).
Repeat this test — same steps, your own backup — before the pilot
goes live, and again after any change to where the database or
uploads live.

## What is not backed up by this

- The ML service models (`ml-service/models/`) — rebuild them with
  `python train.py` or copy the folder separately; they are not
  needed to restore complaints.
- The frontend build (`frontend/dist/`) — rebuild with
  `npm run build`.
- The code itself — that lives in git, not in this backup.
