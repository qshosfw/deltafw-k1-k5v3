---
trigger: always_on
---

1. When developing, you will always test changes by verifying that "make fusion" runs successfully. Then you'll report ram and flash usage to me.
2. You will suggest a short concise commit title and brief description for git.
3. Apply new version info to fw. bump major minor fix etc.
4. For particular new feature sets, add them by default true to fusion, but create their own #if structure ENABLE_... so they can be disabled to tweak ram & flash usage.
