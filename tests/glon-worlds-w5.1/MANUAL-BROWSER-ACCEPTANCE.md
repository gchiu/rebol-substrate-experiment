# Glon Worlds W5.1 — manual browser acceptance

Automated browser automation is not installed on this VPS. The W5.1 automated
tests use HTTPS clients and do **not** substitute for real-browser verification.
Run this procedure before any public cutover.

Use two independent real browser profiles (for example a normal window and a
private window), both pointed at the **loopback/staging** service, never at the
public landing page until cutover is approved.

1. Create two single-use invitation codes on the staging host:

   ```sh
   sudo -u glon-worlds python3 /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py \
     --db /var/lib/glon-worlds/users.db --label manual-a --days 1
   sudo -u glon-worlds python3 /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py \
     --db /var/lib/glon-worlds/users.db --label manual-b --days 1
   ```

2. In browser A, open the staging URL. Confirm the Sign in and Register panels
   are visible and the chat/send controls are hidden.

3. Register user A with the first invitation code. Confirm invalid/blank codes
   are rejected and a successful registration leads to the sign-in step.
4. Sign in as user A. Confirm the chat history and send box appear and the
   browser remains signed in after a page reload.
5. In browser B, register user B with the second code and sign in.
6. Send a message from A. Without clicking Refresh, confirm it appears in B
   within roughly two polling intervals.
7. Send a reply from B. Without clicking Refresh, confirm it appears in A.
8. Confirm each message author is the signed-in user, not any value typed into
   the message body.
9. In browser B, open the browser devtools or another tab and confirm a message
   POST without `X-CSRF-Token` is rejected (403). The application UI should
   still send the token normally.
10. Log out in browser A. Confirm the chat disappears, a page reload remains
    signed out, and replaying the old cookie is rejected.
11. Expire A's session in a temporary test database, reload the browser and
    confirm it returns to the sign-in panel without a server halt.
12. Restart the staging Glon service, sign in again, and confirm users plus
    message history persist.
13. With both browsers signed out, confirm reading or posting messages returns
    an authentication error.
14. Confirm incorrect passwords repeatedly return a controlled error and a
    sixth rapid attempt returns a rate-limit message.
15. Record browser versions, URL, timestamps and any deviations. Real-browser
    acceptance is not complete until this record exists.
