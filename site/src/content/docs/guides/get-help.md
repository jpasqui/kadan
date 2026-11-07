---
title: Report a problem
description: What to include when you report a problem with Kadan.
---

If something in Kadan isn't working, you can report it on the
[Discord](https://discord.gg/Rg5BnvuwmQ). Before you do, check out
[Troubleshooting](/guides/troubleshooting/), as there might already be a fix for it.

## Details

Include these in your report, as they'll help us find out what went wrong:

- **Kadan's version.** Open **Edit > Plugins** and search for **Kadan**.
  The version is on its entry.
- **Your steps.** Write down what you did in order, what you expected to happen, and what
  happened instead. If the Kadan panel showed a message, then copy that too.
- **Your log file.** Attach the log from the editor session where the problem happened.
- **A crash report**, if the editor crashed.

## Logs

Unreal saves everything from the Output Log to a file in your project's folder, at
`YourProject/Saved/Logs/YourProject.log`. That file is for the editor session that's open now, or
the last one if you closed the editor. Unreal keeps the older sessions in the same folder, with
the date and time in their names, like `YourProject-backup-2026.10.12-14.30.00.log`.

Kadan's lines in the log have `Kadan` in them, but attach the whole file, as the lines around them
can matter too.

If the editor crashed, then Unreal saves a folder for the crash in `YourProject/Saved/Crashes`.
Zip the newest folder in there and attach it to your report.
