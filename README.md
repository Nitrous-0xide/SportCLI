# SportCLI
Uses ESPN's unofficial API to get quick updates on live standings for different sporting events.

---

# Current supported events:
- FIFA world cup
- UFC (Sort of)
---

# Coming soon events:
- Premier League
- NBA

---

# Add a new event
To add a new sport or league, update the `eventDefinitions` table in `sportcli.c`.

Each entry needs:
- A menu label
- A screen title
- The ESPN scoreboard URL

After adding the new entry, rebuild with `make` and run `./sportcli` to check that the menu and live view load correctly.
