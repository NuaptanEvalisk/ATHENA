# Gemini CLI Critical Instructions

These instructions are absolute mandates for the Gemini CLI agent working within this repository:

1. **`sed` is COMPLETELY BANNED.** Do not use `sed` under any circumstances. If text manipulation is required, use Python 3. Furthermore, when performing any bulk string replacement, you must ALWAYS EXPLICITLY EXCLUDE the `.git/` directory.

2. **NEVER run `git commit` on your own.** You are strictly prohibited from creating commits autonomously. You may only execute a `git commit` command when the user explicitly and directly instructs you to do so.