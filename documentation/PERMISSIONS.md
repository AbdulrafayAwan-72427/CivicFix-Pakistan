# CivicFix Pakistan — permissions matrix and pilot privacy rules

Decided 9 Nov 2026 (handoff-2, phase 2). This is the authority for who
can do what. The C++ API enforces every row server-side — hiding a
button in React is never the protection. Tests: `workspace/civicfix/
handoff2_phase2_test.sh` (role boundaries) plus the 47-check security
matrix `workspace/civicfix/sec_test.sh`.

## Roles

- **Anonymous** — no login. Only tracking, areas, health/readiness.
- **Citizen** — reports problems, sees only their own.
- **Staff** — fieldwork: sees cases, works the queue, sees their own
  assignments. Cannot change status, assign, or see stats.
- **Admin** — accountable for resolution: triage, assignment, status
  changes (including reopen), undo, user management, stats, audit trail.

## Matrix

| Capability | Anonymous | Citizen | Staff | Admin |
|---|:-:|:-:|:-:|:-:|
| `GET /api/health`, `GET /api/ready` | yes | yes | yes | yes |
| `GET /api/areas` | yes | yes | yes | yes |
| Track by code `GET /api/complaints/track/{code}` | yes, minimal* | yes, minimal* | yes, minimal* | yes, minimal* |
| Register `POST /api/auth/register` (Citizen only) | yes | — | — | — |
| Login / logout, `GET /api/auth/me` | login | yes | yes | yes |
| Submit complaint `POST /api/complaints` | no | own only† | no | no |
| List complaints `GET /api/complaints` | no | own only | all | all |
| One complaint / history | no | own only | all | all |
| Add photo to a complaint | no | own only | no | no |
| Work queue `GET /api/queue` | no | no | yes | yes |
| My assignments `GET /api/me/assignments` | no | no | own only | no (403) |
| Change status (incl. reopen) | no | no | no | yes |
| Assign `POST /api/assignments` | no | no | no | yes |
| Stats `GET /api/stats` | no | no | no | yes |
| Audit trail `GET /api/admin/actions`, undo | no | no | no | yes |
| List users `GET /api/users` | no | id+name+role only | id+name+role only | full records |
| Create user `POST /api/users` | no | no | no | yes |
| Disable / enable user, reset password | no | no | no | yes |
| `GET /uploads/<file>` (photo) | capability URL‡ | capability URL‡ | capability URL‡ | capability URL‡ |

\* Public tracking returns only `trackingCode`, `status`, `updatedAt`
(phase 1, 9 Nov 2026) — no citizen id, description, coordinates or
photo path.

† `POST /api/complaints` still requires `citizenId` in the body and
rejects a citizen submitting as anyone else (403).

‡ Photos are capability URLs, not a public listing: the file name is
`uploads/<random-tracking-code>.jpg|.png`, the code has ~1 billion
combinations, there is no directory listing, a file name containing
`..` or `\` is rejected (400), and a path with extra segments never
reaches the photo handler (404). No file outside `uploads/` is ever
served. Knowing a tracking code is knowing
its photo name — that is the accepted trade-off, because an `<img>`
tag cannot send a Bearer token. Locking photos behind login needs a
signed-URL or cookie change, not a one-line fix.

## Decisions made in phase 2

- **Staff visibility (pilot scope):** all Staff see all cases. A small
  pilot team needs the shared queue to pick up work, and fieldwork
  needs the reporter's name. Staff do **not** see citizens' emails or
  phones (`GET /api/users` redacts those for non-Admins). If the pilot
  grows to several areas or teams, restrict Staff to their assigned
  cases plus unassigned cases in their area — flagged, not built.
- **Assign:** Admin only. **Reopen:** Admin only, Resolved or
  Rejected → Under Review (back to triage, not to Submitted). Every
  status change, including a reopen, writes a row to `status_history`
  and to `admin_actions`, so the timeline and undo both see it.
- **Audit entries:** status changes are audited. Assignments are
  **not** recorded in `admin_actions` (writing one there would
  pollute the undo stack, which restores a previous status). The
  Admin Actions panel says so. A unified audit log is still open.
- **Sessions:** tokens are random strings in the `sessions` table,
  24-hour expiry, checked on every request (`expires_at >
  datetime('now')`). Logout deletes the token. Disabling a user
  deletes **all** their sessions and the session check also requires
  `is_active = 1`, so a live token dies immediately on disable —
  not only at the next login. Resetting a password also deletes all
  that user's sessions. An Admin cannot disable their own account
  (400) — that would lock the operator out with no Admin left.
- **Account recovery:** there is no self-service password reset
  (no email sending in the product). Recovery is operator-assisted:
  an Admin resets the password with `POST /api/users/{id}/
  reset-password`. The user then logs in with the new password.
- **Demo credentials, isolated from deployment:** on startup the
  server seeds predictable demo passwords (`admin123` / `ali123` /
  `staff123`) only for users whose password hash is still empty.
  Set `CIVICFIX_DEMO_SEED=0` to disable seeding entirely — a
  deployment **must** set this, rotate or disable the demo accounts,
  and never run a pilot with the default passwords. With seeding
  disabled, an empty-hash user simply cannot log in until an Admin
  resets their password.
- **Upload validation:** JPG/PNG only, under 5 MB, and the decoded
  bytes must start with the right magic bytes (JPEG `FF D8 FF`,
  PNG `89 50 4E 47`); anything else is rejected with 400. The file
  name is always `uploads/<tracking-code>.<ext>` — the uploader
  cannot choose a path.
- **Abuse controls:** login is rate-limited (5 failed attempts per
  email in 15 minutes → 429). Registration and complaint submission
  are **not** rate-limited — the single-threaded server does not
  track client IPs. Pilot mitigation: the pilot is small and
  invite-led, the Admin can disable abusive accounts immediately,
  and a reverse proxy with rate limiting belongs in front of any
  public deployment (see `DEPLOYMENT.md`).
- **Retention and deletion:** there is no hard-delete endpoint for
  complaints or users — deleting a complaint would destroy the audit
  trail. For the pilot, keep every record. Removing a person means
  disabling their account (above); their complaints stay, attributed
  to a disabled account. True deletion / anonymisation needs an
  operator decision and is still open.
- **Logging:** the server logs to stdout only (startup, seeded demo
  passwords by email, errors). It never logs tokens, passwords, or
  request bodies. Do not add request logging that captures
  `Authorization` headers.

## Pilot gate (phase 2)

The pilot may start when all of these are true:

1. Role tests pass — `sec_test.sh` 47/47 and
   `handoff2_phase2_test.sh` (reopen, disable/enable, reset-password,
   upload validation, traversal) all pass.
2. Public responses contain only approved fields — public tracking is
   minimal (above); `GET /api/users` redacts contact details for
   non-Admins.
3. An operator can revoke access — disable a user (sessions deleted,
   live tokens dead) and reset a password, from the Admin Team tab or
   the API.
4. An operator can recover the database and its uploads from backup
   — see `BACKUP.md`; the restore is tested, not just documented.
5. Deployment checklist in `DEPLOYMENT.md` is done:
   `CIVICFIX_DEMO_SEED=0`, demo passwords rotated/disabled,
   `CIVICFIX_ALLOWED_ORIGINS` set to the real frontend origin(s),
   HTTPS in front of the API.

Still open after phase 2: unified audit log (assignments included);
Staff visibility restriction for multi-team scale; self-service
password recovery; registration/complaint rate limiting; true
deletion / anonymisation; photo delivery behind login.
