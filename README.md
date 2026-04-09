# BDS Essentials (BDSE)

A lightweight **Levilamina** plugin for **Bedrock Dedicated Server (BDS)** that enhances vanilla gameplay with scoreboard-based stat tracking, achievement fixes, and custom chat formatting — without breaking the vanilla experience.

---

## Features

### Real-Time Scoreboard Stats

#### Health Display (Below Name)
- Displays each player's **current HP** below their nametag on the scoreboard (`PlayerHealth` objective).
- Updates in real-time whenever a player's health changes via `MobHealthChangeAfterEvent`.
- Health value is ranged from 0-20.

#### XP Level Leaderboard (Sidebar)
- Tracks each player's **total XP levels** on a sidebar scoreboard (`MostLVL` objective).
- Sorted in **descending order** so the highest-level player is always shown at the top.
- Synced when players join and updated live as they gain XP levels.
- Resets to `0` upon player death (lose your levels when you die!).

---

### Achievements Enabled in All Worlds
- Hooks into `LevelData` to **prevent achievements from being disabled**, even in worlds with cheats or plugins.
- Both `achievementsWillBeDisabledOnLoad` and `disableAchievements` are silently bypassed — achievements stay enabled always.

---

### Farmland Decay Prevention
- Listens to `FarmDecayBeforeEvent` from ILA and **cancels it**, preventing farmland from trampling back into dirt.
- No more accidentally ruining your farms by jumping on them.

---

### Custom Chat Formatting
- Overrides the default chat system with a **custom formatted message**: `§b<PlayerName>§f: <message>`.
- Messages are broadcast manually to all online players.
- Original chat event is **cancelled** to avoid duplicate messages.
- All chat messages are also logged server-side via the plugin logger.

---

### Scoreboard Lifecycle Management

| Event | Behavior |
|---|---|
| Player Joins | Creates/gets scoreboard ID, sets Health & XP scores from current values |
| Player Disconnects | Removes the player's scoreboard entry entirely |
| Player Dies | Resets XP level score to `0` |
| Health Changes | Updates `PlayerHealth` score in real-time |
| XP Level Gained | Accumulates XP level score via hook on `Player::addLevels` |

---

## Technical Details

- **Framework**: [Levilamina](https://github.com/LiteLDev/Levilamina)
- **Language**: C++
- **Target Platform**: Minecraft Bedrock Dedicated Server (BDS)
- **Namespace**: `bds_essentials`
- **Hook Targets**:
  - `Player::$addLevels` — intercepts XP level gain
  - `LevelData::achievementsWillBeDisabledOnLoad` — forces `false`
  - `LevelData::disableAchievements` — no-op override

---

## Scoreboard Objectives Created

| Objective Name | Display Name | Display Slot | Sort Order |
|---|---|---|---|
| `PlayerHealth` | `❤` | Below Name | Descending |
| `MostLVL` | `•> Most Level <•` | Sidebar | Descending |

> All previously existing scoreboard objectives are **cleared on plugin enable** to avoid conflicts.

---

## Dependencies

| Dependency | Description |
|---|---|
| [Levilamina](https://github.com/LiteLDev/Levilamina) | Core Bedrock plugin framework |
| [iListenAttentively (ILA)](https://github.com/MiracleForest/iListenAttentively-Release) | Provides additional events: `MobHealthChangeAfterEvent`, `FarmDecayBeforeEvent` |

---

## License

See [LICENSE](LICENSE).
