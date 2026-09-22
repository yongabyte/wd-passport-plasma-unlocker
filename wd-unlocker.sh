#!/bin/bash
# wd-passport-unlocker: rootless KDE prompt frontend.
# Runs as the active user -- either device-triggered via the systemd user unit
# (udev SYSTEMD_USER_WANTS, see 70-wd-passport.rules), via the Device Notifier
# solid action, or manually via terminal / systemctl --user start
# wd-passport-unlocker.service. Inherits the full session environment (HOME,
# XDG_*, KDE_*, QT_*, DBUS_SESSION_BUS_ADDRESS), so kdialog follows the KDE
# theme with no manual environment forwarding and no root/sudo anywhere.
#
# Wrong passwords re-prompt in place (up to WD_UNLOCKER_MAX_ATTEMPTS, default
# 5) so a typo does not force the user to hunt for a launcher to retry.
# WD_UNLOCKER_KDIALOG / WD_UNLOCKER_BACKEND override the helper paths
# (defaults below); used by tests, leave unset in production.
set -u

KDIALOG="${WD_UNLOCKER_KDIALOG:-/usr/bin/kdialog}"
BACKEND="${WD_UNLOCKER_BACKEND:-/usr/bin/wd-unlocker-backend}"
MAX_ATTEMPTS="${WD_UNLOCKER_MAX_ATTEMPTS:-5}"

attempt=1
while [ "$attempt" -le "$MAX_ATTEMPTS" ]; do
    if [ "$attempt" -gt 1 ]; then
        TITLE="Enter WD My Passport Password (attempt $attempt of $MAX_ATTEMPTS):"
    else
        TITLE="Enter WD My Passport Password:"
    fi

    if ! PASSWORD=$("$KDIALOG" --password "$TITLE"); then
        exit 0 # user pressed Cancel/closed the dialog: quiet exit
    fi
    if [ -z "${PASSWORD-}" ]; then
        unset PASSWORD
        exit 0 # empty password: treat like cancel, do not hammer the drive
    fi

    MSG=$(printf '%s' "$PASSWORD" | "$BACKEND" 2>&1)
    RC=$?
    PASSWORD=
    unset PASSWORD

    if [ "$RC" -eq 0 ]; then
        "$KDIALOG" --msgbox "Drive Unlocked Successfully! Open Dolphin to access your files."
        exit 0
    fi

    if [ "$attempt" -ge "$MAX_ATTEMPTS" ]; then
        "$KDIALOG" --error "Failed to unlock drive after $MAX_ATTEMPTS attempts. ${MSG:-Please check your password and try again later.}"
        exit "$RC"
    fi
    if ! "$KDIALOG" --warningyesno "Failed to unlock drive. ${MSG:-Please check your password and try again.}\n\nTry again? (attempt $attempt of $MAX_ATTEMPTS)"; then
        exit "$RC" # user declined to retry: keep backend's exit code for the logs
    fi
    attempt=$((attempt + 1))
done
