# Quick start

Do this once. Then play.

## 1. Game + Geode

- Geometry Dash **2.2081** (this mod line does **not** work on newer GD yet)
- [Geode](https://geode-sdk.org/) installed for that version

## 2. Install EarlyBuffer

::: warning Not on Geode Index
EarlyBuffer is **banned from the Geode Index**. Do **not** look for it under Geode → Download.
:::

1. Open [GitHub Releases](https://github.com/ToldByNun/earlybuffering-gd/releases)
2. Download the `.geode` for your version
3. Put it in your Geode mods folder
4. Restart GD

If Geode asks for **geode.node-ids**, install that dependency from the Index (that part is fine).

More detail: [Install](/install/).

## 3. Safe starter settings

Open **Geode → EarlyBuffer → Settings** (or pause → **EB**).

| Setting | Starter value | Why |
|---|---|---|
| Enabled | On | Mod is active |
| Safe Mode | **On** | Do not save “legit” progress while assist can fire |
| Assist Mode | **Live** | Plans each click now — best default |
| Window Unit | Ticks | Physics units (~240 per second) |
| Window (Default) | **4** | Small = snappy. Raise later if you want more room |
| Window (Wave) | **4** | Keep low on wave |
| Adaptive Plan | Off at first | Turn on later when Live feels good |

Leave Learning / Auto-Tune / Self-Bake **off** until you know what they do.

## 4. Play one level

1. Jump a little early on purpose.
2. If assist helps, you may see feedback (sound / flash / `+N`) if those are on.
3. If a click is hopeless, EarlyBuffer **passes** — vanilla click, no softlock.

## 5. Only change one knob

If it feels floaty → lower Window.  
If it never helps → raise Window a little, or check [Scope](/configure/scope) (percent / practice rules).  
If progress looks “wrong” online → keep **Safe Mode on**.

## Done?

- Learn the modes → [Features](/features/)
- Full settings map → [Configure](/configure/)
- “Why ticks?” → [Ticks vs frames](/how-it-works/ticks)
- Dig into code → [Dev](/dev/)
