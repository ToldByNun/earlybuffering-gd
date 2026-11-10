# How it works (simple)

Imagine a ghost icon that nobody sees.

1. You press Jump.
2. EarlyBuffer freezes a tiny copy of the world (shadow).
3. It tries “what if I fire 0 ticks later? 1? 2? …” inside your window.
4. It scores which delays still look alive / safer.
5. Best honest answer wins:
   - **Delay** → hold your press, fire later
   - **Pass** → fire now (vanilla)

No inventing of presses. No playing the level for you.

## Pages in this section

- [Ticks vs frames](/how-it-works/ticks) — why “4” is not “4 frames” by default
- [Shadow sim](/how-it-works/shadow-sim) — what the ghost can and cannot see
- [Pass vs Delay](/how-it-works/pass-vs-delay) — the two main outcomes

Want the long contract? [Assist reference](/assist_reference).
