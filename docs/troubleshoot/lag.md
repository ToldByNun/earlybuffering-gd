# Lag & crashes

## Hitch on enter / leave / FPS drop

1. Leave **Interaction Journal** on (default) → play → attach `earlybuffer-interactions.log`  
2. Look for `tw.skipHuge` / `tw.moversOnly` / long `defer.triggerWorld` lines  
3. Turn **Lag Probe** on → check `earlybuffer-lagprobe.log`  
4. Bisect: Adaptive / CPU Multithread off, Trigger World off, Overlay off  

## Hitch on click / death / restart

1. Turn **Lag Probe** on (Main) → play → check `earlybuffer-lagprobe.log`  
2. Turn **Perf Trace** only while hunting (noisy)  
3. Bisect: Adaptive off, Trigger World off, Overlay off  
4. Hybrid with heavy bake verify can hitch — try Live-only

## Crash

1. Note GD version (**2.2091**), Geode version (**6.0.0+**), platform, EarlyBuffer version  
2. Grab Geode crashlog if any  
3. Open a [GitHub Issue](https://github.com/ToldByNun/earlybuffering-gd/issues) with steps  

## Dynamic / section lag (devs)

Some levels with heavy section walks / Adaptive fallthrough used to double-spend budget. Newer builds keep remaining budget for classic fallthrough. Update if you are on an old package.

## Still stuck?

Discord: [community link](https://discord.gg/QJuTDkmPcf) — include version + platform first.
