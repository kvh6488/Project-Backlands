# The Wilderness — Overworld / Underworld Integration Design

> **Living document** — the design for merging the wilderness survival game into Project Backlands as a single, coherent title. Last updated: **05-10-2026**.
>
> This is the **design reference**: why the game is shaped the way it is. The **plan** — what gets built and in what order — is [roadmap.md](roadmap.md), which has absorbed this document's changes (§17).

---

## 1. Premise

The year is **2100**. You live alone with your wife in the wilderness — hunting, fishing, foraging, working the land. It is a beautiful, quiet, self-sufficient life, and the world above is **pristine**. There is no visible apocalypse, no ruin, no contamination on the surface. Whatever happened to the old world happened a long time ago and left the land clean.

**One day your wife goes missing.** That is the inciting incident, and the game starts there.

You have to keep surviving — the land does not care that she's gone — but now you have a second reason to range further than your camp: the seasons force you to move, resources force you to move, and so does the possibility that she is out there somewhere.

Eventually, you stumble across an entrance to something underneath.

---

## 2. The Two Worlds

The game is **hub and spoke**. The wilderness is the persistent hub; the maze is the dangerous spoke you dive into and retreat from.

| | **The Overworld** (Wilderness) | **The Underworld** (The Maze / Bunker Network) |
|---|---|---|
| **Tone** | Beautiful, peaceful, pristine. Never corrupted. | Liminal, hostile, claustrophobic, psychological horror. |
| **Threat** | The elements, starvation, injury, weather, predators. | Radiation, mobs, shifting layout, darkness, escalating waves. |
| **Time** | **Cyclical** — seasons wheel round, year after year. | **Monotonic** — only ever gets worse, forever. |
| **Tech level** | Primitive/classical bushcraft. Flint, bone, hide, cordage, wood, clay. | Old-world technology. Batteries, wiring, chemicals, electronics, salvage. |
| **Resources** | Renewable but slow. Process-heavy (drying, curing, tanning, smoking). Abundant, low value. | Scarcer and harder-won, but far more powerful. Some renewable — **but only producible down there** (chemical lab, hydroponics). |
| **Food** | The whole point of the surface — hunting, fishing, foraging, farming. | Almost none. Mushrooms only, and they won't sustain you. You eat what you carried in. |
| **Light** | Fire. Renewable, trivial, barely needed. | Batteries, made only in the maze's chemical lab, and **light is life**. |
| **Space** | Static. Terrain never moves. | Shifting. The maze regenerates and rearranges. |
| **Purpose** | Where you live. | Where you go to get what living requires. |

**Hard design rule: the surface never degrades.** No contamination, no spreading stain, no dying forest. The horror stays entirely underground. The wilderness looks safe forever — which is arguably scarier, and it protects the thing that makes the survival half worth playing.

---

## 3. Core Loop & Objective

**The core objective is to survive as many days as possible.** Everything else serves that.

1. **Survive on the surface.** Hunt, fish, forage, build shelter, manage hunger/thirst/temperature, prepare for the season turning.
2. **Range outward.** Seasons and resource depletion push you to keep moving and exploring rather than turtling in one spot.
3. **Prepare.** A dive has to be provisioned from the surface — food, water, light, gear. You cannot live off the maze.
4. **Find an entrance.** (Not before Day 20 — see §7.)
5. **Dive.** Take what you can carry, get what the surface cannot give you, get out before your light, food, or luck runs out.
6. **Return and reinvest.** Bring old-world tech up, and use it to survive longer above.
7. **Repeat, deeper.** The maze gets permanently worse every day. Your gear has to keep pace.

**Why you must eventually go down:** surviving alone in the wilderness is peaceful and far safer than the maze, but it is *notoriously* hard. The intent is that players will realise after a few runs that pure surface survival plateaus — **you almost have to go down the maze to survive past ~90 days**. The maze is not optional content; it is the only path to a long run.

**Your wife is the backstory, not the objective.** Lore drips in: who was down here before, what happened to them, occasional hints that she came this way. It gives the descent a personal weight and gives lore a reason to exist, but the scoreboard is days survived.

---

## 4. Time — One Counter, Two Readings

**There is a single day counter, and it starts at Day 0 when the run starts.** It never resets, never branches, and never runs at a different rate in one world than the other. It is the score, and it is what every unlock gate is measured against — including the Day 20 point at which maze entrances can begin to spawn. To the player it is always the same, simple, honest number: *how many days have I survived?*

What differs between the two worlds is **what each system does with that number**. The surface reads it cyclically. The maze doesn't read it directly at all — maze difficulty is anchored to what the *player* has done, not to the calendar (§4.2).

### 4.1 The Overworld Reading (cyclical)
The surface reads the day count **cyclically** — day within the year, wrapping. Seasons wheel: spring → summer → autumn → winter → spring. Drives temperature, animal populations, plant availability, water (melt/freeze/flood), and which entrances are open. It is predictable and it repeats. You can learn it and plan against it.

### 4.2 The Underworld Reading (monotonic, three stages)

Maze difficulty is **not** read off the run's day count. It is anchored to **player events**, and it escalates in three distinct stages:

| Stage | Begins | Escalation |
|---|---|---|
| **1 — Dormant** | Day 0 | **None.** Entrances cannot spawn before Day 20, and until you find one there is nothing running. |
| **2 — Discovered** | The day you **first find an entrance** | **Slow, linear.** The maze has noticed you. It stirs, gently, whether or not you go in. |
| **3 — Entered** | The day you **first go inside** | **Proper pace, exponential.** You are in it now, and it starts climbing in earnest. |

Escalation drives maze shift aggression, layout confusion, radiation strength and spread rate, and mob spawn rates — as already specified in [roadmap.md](roadmap.md) §1.

**Why this is better than anchoring to Day 20.** The clock starts when the *player* commits, not when the calendar says so, which means:

- **Discovery is a real decision with a cost.** Finding an entrance is not free information — it starts the slow clock. A player who deliberately avoids scouting for entrances is genuinely delaying the whole escalation curve, and that's a legitimate (if ultimately doomed) strategy.
- **Two-stage commitment.** You can find an entrance early and then take your time preparing, paying only the slow linear rate while you get your camp, food and gear in order. The moment you actually step inside, the curve turns over into its steep phase. That's a deliberate, legible, player-controlled escalation trigger.
- **No one is punished for taking the wilderness seriously.** A cautious player who spends 60 days learning to survive above doesn't get ambushed by a 40-day-old maze on their first dive. The maze they meet is always young when they meet it.

**Curve shape (continuous by construction):**

```
if not discovered:        escalation = 0
else if not entered:      escalation = A * (day - discoveryDay)                    // slow, linear
else:                     escalation = A * (entryDay - discoveryDay)               // banked linear
                                     + B * (exp(C * (day - entryDay)) - 1)         // exponential, starts at 0
```

The `- 1` matters: it makes the exponential term zero on the day you first enter, so the curve bends upward smoothly instead of jumping. The player feels the maze getting worse, never a sudden unexplained spike.

Once stage 2 begins, **the curve climbs whether or not you go back down.** A winter spent hunkered on the surface is a winter the maze spent getting worse. There is no way to pause it, and no way to farm the surface risk-free forever.

> **Balance note on "exponential".** A true exponential runs away hard and will turn a run from survivable to hopeless within a few days of the knee. That may be exactly what you want, given the score is days survived and death is meant to be inevitable — but it's worth checking in playtesting whether the ending feels like *pressure* or like *a wall*. If it's a wall, the usual fixes are a lower `C`, a soft cap, or a logistic curve that flattens at a brutal-but-playable ceiling. Flagged in §19.

**Implementation note.** Three stored numbers plus the day counter, only one of which the player ever sees:

```
day            // authoritative. Ticks at the normal rate everywhere. Score + unlock gates.
seasonalDrift  // extra season-time accrued while in the maze. Only ever increases.
discoveryDay   // unset until the first entrance is found
entryDay       // unset until the first time the player goes inside

season     = seasonOf(day + seasonalDrift)   // wraps modulo year length
escalation = escalationOf(day, discoveryDay, entryDay)   // per the curve above; ignores drift
```

The day counter stays clean and exploit-proof, the drift makes the maze feel temporally wrong, and the two event anchors put the difficulty curve's start in the player's hands.

### 4.3 Time Dilation — the seasons run fast, the day counter does not

**Resolved approach.** The maze should *feel* like it plays by different temporal rules without becoming a mathematically optimal way to farm the scoreboard.

- **The day counter ticks at the normal rate, everywhere.** Time below buys you no extra days survived, and no faster escalation. This closes the exploit outright.
- **The seasons accelerate while you are below.** Time in the maze accrues *seasonal drift* (§4.2) on top of the normal day tick, so spend too long down there and you climb out into a season you did not plan for — descend in late summer, surface into deep winter. The drift is permanent for the run; you never get that season back. **Drift pauses while an extreme weather event is pending or active** (§10.2), so a dive can never be used to fast-forward past one.
- **Consequences land at your base, not on the counter.** Outdoor crops dead and frosted over. Unmaintained structures damaged. Stores spoiled or raided. The window you were saving for has closed. The punishment for greed is *logistical*, which is exactly right for a survival game.
- **Hunger, thirst and stamina drain faster below.** Diegetically this is **stress and constant movement** — you are running, hiding, and terrified, not strolling. Mechanically it is a flat elevated drain rate while in the maze; it is **not** coupled to actual player movement input, so there's no incentive to stand still and no fiddly movement-accounting system to build.

Net effect: the maze is temporally wrong, the player feels it, the scoreboard stays honest.

---

## 5. The Entrances

Entrances are **discovered, plural, and varied**. Finding a new one is real progress — it's a shortcut past known territory and a different drop point into the network.

**Each maze spawn has on average 2–4 entrances.**

**Entrance types:**

| Type | Behaviour |
|---|---|
| **The sinkhole / hole in the ground** | **One-way. You fall in and you cannot climb out.** Easy to enter — often by accident — and the entire problem becomes finding another way up. The most Backrooms-appropriate entrance in the game. |
| **Seasonal entrances** | Only accessible or even *visible* in certain seasons. A service door swamped by spring melt and frozen shut in winter, passable only in late summer. A hatch you can only reach when the ground is frozen hard enough to walk on. This ties surface season directly to maze access and forces the year to have a rhythm: prepare in the warm months, dive in the window, survive winter on what you carried back. |
| **Ventilation stacks** | Sealed with bolts. Require the screwdriver (roadmap Phase 12 item, needed earlier for this). |
| **Freight lift** | Needs power. A mid-game unlock that converts a one-way fall into a repeatable, survivable route. |

---

## 6. Death, Saves & Pausing

**Full permadeath, both worlds.** Die in the maze or die on the surface and the run is over — back to Day 0, everything gone.

**Rationale (design intent):** the item set is deliberately *not* huge. Fewer items, each one genuinely fun to use. So reaching the point in a run where you're properly kitted out and then losing it should make you want to go straight back in and get there again. The chase is the point.

**Making permadeath fair — accepted design:** the two worlds kill you differently.
- **The surface mostly cripples rather than killing outright.** Injury, illness, frostbite, infection, a broken tool at the wrong time, an empty smokehouse in November. Surface death should almost always be the end of a *chain of the player's own compounding mistakes*, visible in hindsight, never a sudden ambush. **Extreme weather is the exception that can kill outright** — but only where the player had a buildable counterplay and did not build it, which keeps it inside the same rule. See §10.1.
- **The maze is where sudden death lives.** That's the trade you accept when you go down the hole.

**Saves & pausing (open, likely to change).** The current roadmap specifies no saves and no pausing while exploring the maze. Both may need to be relaxed for a game of this length — a multi-year survival run is a lot to ask of a single sitting. If saving is added, the intended form is:

- **One save slot at a time.** One run, saved in place, wiped on death. No branching, no backups, no rolling back a bad decision.
- **You can only save at a dedicated camp in the overworld.** Saving is a *place*, not a menu option. You cannot save in the maze, and you cannot save standing in open wilderness in the middle of a blizzard.

That second rule does a lot of work for free:

- **No save-scumming a dive.** Once you go down the hole you are committed until you come back up. The maze keeps its teeth entirely.
- **It makes the surface camp mean something mechanically**, not just as a shelter — it's the only place the run can be put down and picked up again.
- **Quitting has a cost.** Wanting to stop playing means physically getting yourself somewhere safe first, which is itself a survival decision, and a very thematic one.
- **It reinforces the nomadic loop** (§9) — your seasonal camps become your save points, so where you site them is a real strategic choice rather than pure resource logistics.

*(Open: what counts as a "dedicated camp" — probably basic shelter tier and above, so a bare fire and bedroll isn't enough. Also open: what happens if the player quits without saving. Standard approach is to leave the run where it was at the last save, which is consistent with everything above.)*

The **no-pause rule may survive even if saving is added**, since it's doing real work for the horror and survial realism.

---

## 7. Progression & Day Gates

Systems and items unlock at **day thresholds**, so the late game gets *more* interesting rather than just harder.

- **Maze entrances cannot spawn before Day 20.** Twenty days of pure wilderness survival first. This teaches the survival systems properly, makes the discovery of the first entrance a genuine event, and means the horror half is earned rather than handed over. Day 20 gates *availability* only — the difficulty curve doesn't start until you actually find an entrance, and doesn't reach its real pace until you go in (§4.2). The maze is always at its gentlest when you first meet it, however long you took to get there.
- **Other systems and items unlock at later day counts.** (Specific gates TBD.) The dog is mid-game. Deeper maze systems, the chemical lab, hydroponics, and the advanced tech tiers sit further out still.

This also gives the run a *shape*: early game is bushcraft, mid game is the first dives, late game is a fully-equipped operation running supply lines between two worlds.

---

## 8. Survival Systems

**Hunger, thirst, temperature** are core, and they **carry into the maze** rather than switching off — at an elevated drain rate (§4.3).

**Food below is nearly nonexistent.** There is no game to hunt, no clean water, and no fire. The only edible thing down there is **mushrooms**, and they will not sustain you for long — enough to stretch a dive, never enough to live on. *(To be expanded: nutritional value, the split between normal and magic mushrooms, whether spoilage applies, whether water can be found at all.)*

**This is what makes surface preparation load-bearing.** A dive is provisioned before it starts: smoked and dried food, full waterskins, charged batteries, working gear. The homestead isn't decoration — it's the staging ground, and a badly-prepared dive is lost before you enter.

**Sleep.** Sleeping advances the day and triggers maze regeneration in the shifting zone (roadmap Phase 7). Because your bed is now on the surface, **sleeping safely at home still shifts the maze** — reinforcing that the underworld clock cannot be paused. Sleeping *in* the maze needs separate, much riskier treatment: a bedroll in a cleared, barricaded room, high risk, but you don't lose your hard-won position. Failing to sleep still causes sluggishness, hallucinations, and passing out.

**Fire.** Renewable and central above; **nothing burns below** (gas pockets, or simply nothing to burn). This forcibly separates the two toolkits and stops surface solutions from trivialising maze problems.

---

## 9. Surface Shelter — Nomadic, Not Monolithic

**There is no mega-base.** Building one permanent fortress and turtling in it is explicitly not the game. You move with the seasons the way nomadic humans actually did — following game, water, and warmth — so the natural pattern is **a camp for each season** rather than one home for all of them.

This also quietly solves the permadeath problem: the player never sinks hundreds of hours into a single structure that one bad dive erases. Investment is spread thin and always partly disposable by design.

**Shelter tiers:**

| Tier | Build cost | Behaviour |
|---|---|---|
| **Basic camp** | Minutes | No shelter. A fire, a bedroll, somewhere to process a kill. Disposable — make one anywhere, abandon it without a second thought. |
| **Basic shelter** | Hours | Lean-to, debris hut, tarp. Real protection from weather, still cheap enough to leave behind when the season turns. |
| **Log cabin** | Days of in-game work | Permanent, immovable, genuinely good — the only realistic way to survive a hard winter. Takes serious time to build and **must be maintained across the seasons**. |

**The cabin maintenance rule matters.** You cannot build a cabin, wander off for three seasons, and return to a warm house in November. Left unattended it degrades — roof damage, rot, weather, animals — and you come back to a repair job, not a home. Winter shelter has to be *kept*, which means your seasonal circuit has to actually route back through it, which is what turns nomadic movement into a planning problem rather than random wandering.

**Extreme weather is what makes the tiers mean anything** (§10). Without it the ladder is a slow temperature bonus; with it, a basic shelter is the difference between a blizzard costing you a night and costing you the run, and *where* you sited each camp — high ground, out of the trees, away from the river — becomes a decision the weather comes back to audit.

---

## 10. Extreme Weather Events

**Roughly one extreme weather event per in-game month.** That is far more often than the real world, and deliberately so: it is **2100**, and climate change has run to its extreme. This is the one place the "realistic" brief bends, and it bends for a stated in-fiction reason rather than for game feel. It does **not** break the §2 rule — the surface is still pristine and never degrades. The weather is violent; the land is not poisoned.

Weather is the surface's answer to the maze's escalation curve. The maze gets worse monotonically; the surface gets worse *periodically*, and unlike the maze you cannot choose not to enter it.

### 10.1 The design rule that constrains all of this

**Extreme weather can kill you outright.** The overworld is not a safe half of the game — it is the *less* deadly half. The maze is where death is sudden and often unavoidable; the surface is where death is survivable-in-principle and therefore always, in hindsight, your own fault.

That is the refinement §6 needs. It said the surface cripples rather than ambush-kills, and that surface death should be the end of a visible chain of the player's own mistakes. Both still hold — but the chain now runs through **preparation**, not just moment-to-moment caution. A flood that drowns you was survivable if you had a boat. A blizzard that freezes you was survivable inside a real shelter. A bushfire was survivable with cleared ground or water between you and it. The mistake was made days earlier, in what you did or didn't build.

So the rule for every event is: **every event has counterplay, and the counterplay is a thing you built.**

| Event | The prepared answer |
|---|---|
| Flood | A boat, or a camp on high ground away from water |
| Blizzard | A real shelter (basic tier or above) — a camp is not enough |
| Bushfire | Distance, water, cleared ground / firebreaks |
| Storm | Not camping under trees |
| Drought | Stored water, and the mobility to leave |

This keeps the death honest without making it toothless. The player who dies to a blizzard did not get ambushed by the weather; they went into winter without a shelter, which is a decision, and one the game gave them a season to make.

It also means the counterplay list is a **build list** — it is what the surface tech tree is *for*, and it gives the boat, the cabin, the firebreak and the water store a reason to exist beyond flavour.

**Information is the other half of the counterplay**, which is what makes §10.3 the real mechanic rather than a convenience: knowing an event is coming is what converts a built answer into a used one. The event is the setting; **reading it in time and having already built the answer is the game.**

### 10.2 The events

Two shapes, and they want opposite counterplay:

- **Acute** — minutes to hours, destroys things. Answer: be inside, or be somewhere else.
- **Chronic** — days to weeks, starves you slowly. Answer: move camp.

| Event | Shape | Season bias | What it does |
|---|---|---|---|
| **Flood** | Acute | Spring (melt), any heavy-rain period | Low ground and anything near river or lake goes under. Floods camps and basic shelters outright; ruins food stores, kills fires, sweeps away un-anchored gear. A cabin sited on high ground survives — **which retroactively makes where you built it a decision you made months ago.** Caught in rising water without a boat, you drown. |
| **Blizzard** | Acute | Winter | **Camping without shelter in a blizzard is not survivable** — being in the open is the failure state, not bad luck. Shelters get snowed in (a dig-out cost, and a real one if your tools are outside). Cabins hold. Travel is effectively suspended. |
| **Heatwave → bushfire** | Chronic spawning acute | Summer | The heatwave itself is chronic — water burns down, work is slower, the risk meter climbs. It then *spawns* the acute event: fire, which spreads across terrain and burns forest, camps, shelters, and cabins alike. It kills what it catches. The only answers are distance, water, and cleared ground — and it is the one event that can destroy a cabin outright. |
| **Storm / high wind** | Acute | Autumn-biased, any season | **Blows over trees.** Falling timber kills, crushes structures and blocks routes; a forest camp is the worst place to be and an open ridge is the second worst. Leaves felled wood behind — the one event with a windfall attached. |
| **Drought** | Chronic | Summer | Water sources shrink or vanish, game migrates away, foraging thins. Destroys nothing. Forces you to move, which is exactly the pressure §9's nomadic loop wants. |

**Weather and the maze.** An event that fires while you are underground lands entirely on your base — you climb out into the aftermath, the same logistical punishment as seasonal drift (§4.3), and for the same reason. Flooding can also seal or reveal an entrance, which folds straight into §5's seasonal entrances.

> **Note — floods read the height field (resolved).** Flooding needs a notion of low ground. The world now has one: height is the island's land score (§16), so a flood **fills the lowest ground near water first** and spreads uphill. Cabin siting on high ground is a real height check, not a distance-to-water proxy. Rendering is unchanged — the world still draws flat.

> **Note — the maze is shelter, and that has to not be an exploit.** If you have no shelter, **going underground is a legitimate way to sit out an extreme event** — a real, welcome extra reason to descend, and one that lands right where the design wants pressure: the players with the worst surface position have the strongest reason to go down. The catch is that you are blind down there. You cannot see the current conditions, cannot tell how long the event has left, and cannot judge what you will climb out into.
>
> **This forces a change to §4.3's seasonal drift.** As written, a quick dive is a way to fast-forward time — which would let a player skip an event by ducking underground for a few minutes. So: **while an extreme weather event is pending or active, seasonal acceleration pauses.** The event runs for its full duration at normal speed whether you are above or below it, and drift resumes once it clears. Waiting one out costs you the real time it takes, never less.

### 10.3 Reading the weather — three tiers of information

This is the actual system. The events are content; the **information ladder** is the mechanic, and the whole thing is a study in *how much you know and where you have to stand to know it.*

| Tier | Requires | What you get |
|---|---|---|
| **0 — Signs** | Nothing | Ambient cues only: rate of snowfall, wind picking up, rain intensity, heat haze, the river rising. No names, no timings, no certainty. |
| **1 — Weather station** | Computer + satellite dish, hauled up from the maze and installed in a shelter | Current conditions read properly, plus **prediction**: this system is deepening, this front is coming. Still no exact timing. |
| **2 — Upgraded station** | Level 2 upgrade to the same installation | An explicit **10 in-game minutes' warning** of a specific approaching event — **but only while you are physically at the terminal.** |

**The fixed location is the entire point.** The Tier 2 warning does not travel with you. It makes your shelter an *information anchor*, not a safety blanket, and every check costs the walk home. That is a second reason — alongside saving (§6) and winter survival (§9) — for a nomadic game to still care where its fixed points are.

**What each tier does to how far you dare walk:**

- **Tier 0.** Every minor sign has to be treated as potentially run-ending, because it might be. Your exploration radius is bounded by *can I get back before this turns lethal*, and every darkening sky truncates the trip. This is the default state and it should feel genuinely constricting.
- **Tier 1.** You can finally tell weather from *Weather*. The question becomes a judgement about **pace of change** — is this rain getting worse fast enough that I should stop what I'm doing and walk back to check the station? That judgement, not the event, is the interesting decision.
- **Tier 2.** You can commit to a specific window. Ten minutes is not enough to get home from anywhere — it is enough to decide, from the terminal, whether this is a day to range out at all.

**The build.** The computer and the satellite dish are **late-game maze salvage** (§12), carried up and installed. It needs power, so it sits inside the battery economy — another standing draw competing with your light for the same batteries, and a station that goes dark when you overspend on a dive.

**Why this is the right shape for the reward.** It is old-world technology, found in the dark, that makes the *surface* half more survivable — the same direction of payoff as the archive's survey maps (§14). The underworld does not make you stronger down there; it makes you able to live up here. That is the load-bearing asymmetry of the whole design, and the weather station is probably its clearest single expression.

---

## 11. Maze Camps

**Maze bases are forward camps, not homes.** A stash chest, a barricaded room, a light left burning, a bedroll. Establishing a camp deep in the network is a mid-game achievement rather than the opening move. Rope, doors and barricades stop being about building a home and become about **securing a route** — which is a better use for all three.

Existing base rules carry over where they still apply: camps must be placed in rooms, entrance/exit count is a real trade-off (too few and you get cornered, too many and you can't defend), no camps in radiation zones, and the "something is off with this room" warning when placing inside a regeneration zone.

---

## 12. Crafting — Two Tech Tiers

Two trees that should feel completely different to use.

**Surface (classical / bushcraft):** cordage, flint, hide, bone, wood, tannin, clay, sinew. Slow. Process-driven — things must be dried, cured, tanned, smoked. Nothing is instant. Materials are abundant; the cost is time and labour.

**Underworld (old-world tech):** batteries, wire, scrap metal, circuit boards, chemicals, components. Fast to assemble once you have the parts. The cost is the risk of getting them.

### 12.1 What "limited" actually means down there

The maze is **limited, not permanently finite** — the distinction matters:

- **Renewable, but only in the maze.** The **chemical lab** (batteries, chemicals) and **hydroponics** (better and more reliable food than the surface can consistently produce) are ongoing production systems, and they can *only* be run underground. Once established they keep producing — which is precisely why the maze stays relevant for the whole run instead of being strip-mined and abandoned.
- **Limited-chance salvage.** Most maze items are scarcer than anything on the surface, and some are rare enough that you may get **only one realistic chance at them in a given run**. Miss the chance, or lose the item beyond repair, and that capability is gone **for the rest of that save** — not gone from the game. Permadeath rerolls the dice; a new run is a new set of chances.

### 12.2 Durability & repair
Every item runs on **Minecraft-style durability** — use it enough and it breaks. Some tools can be **repaired at a dedicated repair/work table** rather than being lost forever, so gear management is an ongoing cost rather than a cliff.

Salvaged tech is **tied to the maze**, not doomed by it: batteries need the chemical lab, and the lab is underground. That circularity — the maze funds the equipment required to survive the maze — is the load-bearing structure of the late game.

**The interesting space is where the tiers cross.** A flint spear is cheap, renewable, and weak. A machete found below is superb, wears down, and can only be maintained by going back for what maintains it.

---

## 13. The Dog

Found in the **overworld, mid-game**. On the surface it hunts and tracks. **It can be taken down into the maze**, where it becomes an early-warning system against flanking mobs and can track scent through corridors.

Because it can die permanently down there, the decision to bring it is a genuinely hard one every single dive. Cheapest high-impact feature in the design.

---

## 14. Maps

The same crafting recipe produces two completely different relationships with the player:

- **Surface maps are permanently true.** Terrain never moves. A map you draw of the wilderness is good forever, and mapping the land is real, durable progress.
- **Maze maps rot.** The layout shifts every time you sleep. Your map starts decaying the moment you draw it, and the older it is the more it lies.

**Survey maps of the wild are found below.** The bunker was a research and survey station (§15), so its archive contains **old-world maps of the surface** — cached resource locations, water sources, old trails, terrain that would otherwise take many in-game weeks to scout. This is a clean, contamination-free reason for the underworld to reward the overworld: *you go down into the dark to learn about the light.*

Existing map items (Classic Map, Magic Map, Infected Map, Computer Map) continue to apply to the maze; the survey maps are a new surface-facing category.

---

## 15. Lore & The Archive

**Backstory:** the bunker network was built and inhabited by the **last survivors of the "old world"**, before an apocalypse that **nobody actually knows the details of**. It ended a long time ago. The surface healed. The network did not.

**Nothing is confirmed.** Lore arrives as rumours and fragments in found text documents, competing with each other:
- **The Great Storm**
- **The Floods**
- **The Fires**
- **Radiation** — the most plausible account, and the one that fits the radiation still present in the network

Let the documents disagree. An unresolved apocalypse is more unsettling than a stated one, and it costs nothing to maintain.

**The bunker as records archive.** It was a research and survey station, which justifies both the technology and the paperwork. Found down there: survey maps of the wilderness (§14), research logs, personnel records, incident reports, and — occasionally, rarely — traces of people who came down more recently than the old world. **Including hints about your wife.**

Keep the wife thread as a slow, sparse drip. It should never become a fetch quest with a marker; it's the reason the player reads the documents at all.

---

## 16. Terrain, Water & Visual Polish for the Overworld

> **Settled for roadmap Phase 4 (05-10-2026).** The overworld is **an island in a wrapping ocean**, and it has **height** — but height as a *generation and gameplay value*, not rendered relief.
>
> Earlier drafts explored height as a rendered field — hydraulic/thermal erosion, quantised cliff bands, connectivity ramps, a posterised hillshade shader. **That rendering track stays cut**: its cost was large for a purely visual payoff. What came back is the cheap part. The island's **land score** — noise minus a falloff from the centre — *is* the height: mountains rise in the middle, the coast is where the score crosses the land threshold. It costs nothing extra to compute, and it does real work: rivers flow downhill (§16.1), temperature falls with altitude (§16.2), floods fill low ground first (§10.2), and siting a camp on high ground means something.
>
> **The world still draws flat**, the same way the maze is flat. Height shows only through biome tiles (sand → grass → rock → snow) — no shading, no cliffs, and for now no effect on movement. Depth comes from mixed-projection sprites (§16.4) and lighting/animation polish (§16.6), not from geometry.

### 16.1 Water bodies: rivers & lakes

Rivers and lakes are staying in the game — they're too load-bearing to cut (drinking water, fishing sites, crossings as route decisions, frozen rivers as winter roads, spring melt as a hazard, and §14's "follow the water downstream" as free orientation in a world where maps are a scarce crafted good).

**Settled: rivers flow downhill, because the world has height.** The earlier question — authored directional paths vs non-directional noise — dissolved once the land score became the height. Flow is not authored; it falls out of the terrain:

- **Rivers** start on high ground and walk to their lowest neighbour, step by step, until they reach the ocean or a lake. Where two meet, they merge. Every river therefore ends at a lake or the ocean *by construction* — no rule has to enforce it.
- **Lakes** are pits in the height field — local minima where a downhill walk gets stuck. A pit fills into a lake until it overflows, and the river continues from its lowest rim (*priority-flood* depression filling → search `priority flood depression filling`, Red Blob Games `mapgen2`).
- **Lone lakes** are pits no river ever reaches — still water with nothing feeding it.

The direction comes for free, so the payoffs an authored directional river would have bought are all back: "follow the water downstream" for orientation, animated current, frozen rivers as winter roads. River and lake placement runs once at world creation on the coarse grid (§16.5) — a whole-island pass, which the finite island affords.

### 16.2 Biomes & moisture

Keep the **Whittaker diagram** approach — a 2D lookup of temperature × moisture → biome type → search `Whittaker biome diagram`. It is a table, not an equation: round both inputs into buckets and read `table[t][m]`.

- **Temperature** = base + noise − height. An island ~2k tiles across is too small for latitude to mean anything; altitude does the work instead, so snowy peaks come straight out of the table with no special case. Seasons later shift the whole field up and down, moving the snow line.
- **Moisture** = a coarse noise field plus a bonus near rivers, lakes and coast.
- **Height bands** override the table at the extremes: beach along the coast, mountain and snow peak at the top.

Starter set: **ocean, beach, grassland, forest, wetland, mountain, snow peak**; scrub and tundra can follow with seasons. The payoff is a world where biome placement feels governed rather than random — a veteran learns that wetlands cluster round water and the cold is uphill, and reads an unfamiliar island faster for it.

### 16.3 Distribution: resources & set pieces

- **Poisson-disc sampling (Bridson's algorithm)** for resources and landmarks — random placement with a guaranteed minimum spacing, so the world reads as naturally uneven (blue noise) rather than clumped-with-dead-zones (white noise). → search `Poisson disc sampling Bridson`. This is the exploration-pacing lever: it guarantees "something worth finding within N minutes of walking" without a visible grid.
- **Hand-authored prefabs** (ruins, standing stones, abandoned cabins, **maze entrances**) placed at sites passing constraint checks: correct biome, minimum distance from siblings, near or far from water. Noise produces variety; authored set pieces produce *memory* — nobody remembers a hill, everybody remembers the lightning-split tree by the ford.

### 16.4 The current maze rendering approach, and why it carries over

The game already uses **mixed projection**, and the reasoning behind it applies to the overworld unchanged.

Current state:

| Element | Projection | Where |
|---|---|---|
| **Corridors** | **Flat top-down.** Bitmasked tile drawn at `(x, y)`, no projected face. | `maze_renderer.cpp` ~L126-150 |
| **Rooms with visible floor below** | **3/4 oblique.** Roof edge at `y-2`, upper wallpaper at `y-1`, base wallpaper at `y`. | `maze_renderer.cpp` ~L180-220 |
| **Props** (doors, lockers, benches) | **3/4.** Door drawn 16×29 anchored at its base; furniture shows front faces. | `item_renderer.cpp`, `maze_renderer.cpp` |
| **Player** | **3/4.** Front-facing sprite — you see the body, not the top of the head. | `player_renderer.cpp` |

**Mixed projection is the standard convention, not a compromise.** Stardew Valley, Zelda: A Link to the Past, Pokémon and Chrono Trigger all draw the ground plane top-down and give a projected face only to things that need to read as *vertical*. No 2D game does consistent optical geometry. The rule is "project only what needs to read as vertical," because the goal is legibility, not correctness. The overworld's trees, rocks, cabins and set pieces should follow the same rule: flat ground, projected props — no heightmap required to make that convention work.

### 16.5 World scale, seed & the edge

**Settled: a new world seed per run.** Permadeath rerolls the world. This keeps discovery fresh and stops run #20 from being a memorised optimal route, which matters when the score is days survived.

**What carries across runs is not the map, it is literacy.** Water collects in basins, biomes cluster round it, resources thin out with distance from camp. A veteran reads an unfamiliar map quickly because they understand the generator's logic, not because they've memorised a layout.

**Settled: a finite island in a wrapping ocean.** The landmass is finite — one large island, plus the islets and outlying islands the falloff mask naturally leaves (kept: coastal gameplay will be expanded later). Around it is **open ocean, at least 500 tiles in every direction**, and the world wraps toroidally like the maze: sail far enough and you come back round to the far shore. The world *seems* infinite, but it is a fixed `W × H`.

- **The ocean hides the seam.** The wrap edges always fall in open ocean, which is uniform water, so no generator — noise, biomes, rivers, scatter — has to be seamless across it. Only player position, camera culling and the chunk cache need to know the world wraps.
- **Size.** Start at **~2k tiles across** while the generation mechanisms are built, then grow toward ~8k. The design argument for a large-but-finite world stands: nomadic seasonal camps (§9), a maze that spawns near you (§5), a wife to find, and ~90 days of surviving in one place all depend on the world being *knowable*, the way Valheim, Don't Starve and Unreal World's worlds are.
- **Spawn inland** — in the mid-height band between coast and mountains, on grassland nearest the centre; never a lake, river or mountain. The peak is the hardest place to start a survival game.

**Crossing the open ocean is a survival challenge, and the wrap is a discoverable secret.** Nothing stops you, but a crossing means provisioning a boat for a long voyage: scurvy from a poor diet, and deep-water fishing as the only food out there. Reaching the far shore and realising where you are is the reward — and an achievement (roadmap §6).

**Resolution decoupling — two layers.** The maze's toroidal 1D-array grid is a different representation entirely and shares no world-storage code with this.

| | Coarse grid | Chunks |
|---|---|---|
| Resolution | 1 cell per 8×8 tiles (~256×256 at 2k) | Every tile, in 32×32-tile chunks |
| Holds | Biomes, lakes, the river network — anything needing a whole-island pass | Trees, rocks, bushes, resources |
| Built | Once, at world creation | On demand from `hash(seed, chunkX, chunkY)` when near the camera |
| Stored | Yes — it's small | No — discarded when far away |

Height itself is stored in neither: it is a pure function of position and seed, computed wherever it's needed. Two consequences of regenerating chunks:

- **Player changes are a sparse diff.** A felled tree would grow back on regeneration, so anything the player alters is kept in a map keyed by tile and re-applied after the chunk is rebuilt (the same pattern as the maze's `m_itemStates`). Only what the player touched costs memory.
- **Poisson spacing across borders.** Two chunks generated independently can each put a tree at their shared edge. A chunk also regenerates its neighbours' points — cheap, because they're deterministic — so minimum spacing holds across borders.

Generation constants are compile-time: the island is built once before the player loads in, so there is nothing to tune live.

### 16.6 Visual polish & effects — where the payoff actually is

With rendered height off the table, this is the section carrying the overworld's visual ambition, and it's worth treating as the real plan rather than a leftover list. Roughly in order of payoff per line of code:

- **Palette unification** — ✅ done: the 56-colour master palette (`docs/palette.md`). It was sampled mostly from underworld art, so the overworld pack sheets need a test quantize before they enter `assets/`, and possibly a palette extension.
- **Y-sorted render queue.** The current draw order is three fixed layers (maze → player → items in `playing_state.cpp` ~L432-435), fine for a tile maze but not for a forest where trees, rocks, the dog, animals and the player need to interleave with each other. Collect drawables into one queue, sort by base Y in world pixels (not tile row, or entities pop between tiles) — `O(k log k)`, or `O(k)` with a bucket sort since baseY is bounded by screen height.
- **8-bit autotiling.** The existing `tileMap[16]` is a 4-bit cardinal-only mask, fine for solid walls; biome borders (grass/sand/water) need the corner-aware 47-tile "blob" set or diagonal transitions look wrong.
- **Banded/posterised lighting.** The old hillshade-specific version of this trick is gone with the heightmap, but the underlying principle still applies everywhere a continuous light term meets pixel art: quantise into 3-5 discrete steps and map each to a chosen palette colour rather than a continuous multiply, so day/night transitions and torchlight shade in clean bands instead of muddy in-betweens. → search `posterized lighting pixel art`, `pixel art palette ramp shading`.
- **Day/night colour ramp**, using the banding above.
- **Weather** — rain, fog, snow, a fog-density uniform. Turns the same terrain into a visibly different place and plugs straight into seasons and temperature. This is also the **only channel the Tier 0 player has for reading an incoming extreme event** (§10.3), so intensity has to be legible and its *rate of change* readable — these effects are gameplay, not decoration.
- **Wind sway** — offset the upper pixels of grass and tree sprites by `sin(time + worldPos)`. Roughly twenty lines, and the world stops feeling like a diorama.
- **Animated water with edge foam** — the direct payoff of §16.1's rivers/lakes; river current animates downhill, the way the water actually flows.
- **Ambient particles** (pollen, leaves, snow, fireflies) and per-biome colour grading.

### 16.7 A staged path

| Stage | Work | Payoff |
|---|---|---|
| **0** ✅ | Palette unification. | Unblocks everything else; fixes existing asset incoherence. |
| **1** | Island height field, lakes and downhill rivers, biomes (coarse grid), per-chunk detail scatter, Y-sort, autotiling — **roadmap Phase 4**. | The world exists and reads as governed rather than random. |
| **2** | Render polish pass — banded lighting/day-night, weather, wind sway, animated water, particles, per-biome grading — **roadmap Phase 9**. | This *is* the visual identity of the overworld. No rendered-relief pass is planned behind it. |

### 16.8 Reference games

| Game | What to look at |
|---|---|
| **Unreal World** | The direct genre inspiration, now also the closest structural match: fixed finite world, rivers and lakes that structure travel, fishing and settlement, drawn in very plain top-down tiles. No elevation-driven relief. |
| **Necesse** | Closest genre and aesthetic sibling (2D top-down pixel survival, procgen biomes) — its terrain is flat, which used to be a limitation to note and is now simply the model. |
| **Stardew Valley** | Reference for *considered* flat pixel art — palette discipline, readable tile borders, mixed-projection props — with the cliff-specific lesson dropped since there's no heightmap here. |
| **Minecraft** | The counter-example for rivers: they wander, dead-end, never consistently reach an ocean, and nobody navigates by them. Downhill drainage (§16.1) is what avoids that. |
| **Red Blob Games — mapgen2 / "making maps with noise"** | Not a game, but the closest technical reference: falloff island masks, elevation-driven biomes, and drainage. |

---

## 17. What This Changes in the Existing Roadmap

**Applied (05-10-2026).** The deltas this section used to list have been folded into [roadmap.md](roadmap.md): the vision is two worlds, the phases from 4 onward are re-planned around the overworld, the items table carries a *World* column, and the ~105-day timeline is gone. The section number is kept so references elsewhere stay valid.

---

## 18. Gameplay Risks to Watch

Distinct from the open questions below: these are not decisions waiting to be made, they are **known weak points in the design that only playtesting can resolve**. Each one is a way this could be structurally sound on paper and still not fun.

### 18.1 The wilderness half must be fun second-to-second — no interlock fixes this
**The biggest remaining risk in the whole design.** Hunting, fishing, foraging and processing degrade into menu-driven chores very easily. Unreal World is deeply admired and also famously dry. Every interlock in this document assumes the surface loop is worth playing on its own terms; if tracking an animal, setting a trap, landing a fish or building a shelter doesn't feel good *in the hand*, the surface becomes homework between dives and the prep loop turns into friction rather than tension.

This is an execution problem, not a structural one, which means no amount of further design work solves it. **Prototype the moment-to-moment feel of one surface activity early** — before building breadth — and be willing to conclude the answer is "hunting needs to be a real mechanic, not a menu."

### 18.2 Carry capacity is load-bearing
Triage is what makes preparation matter: food *or* batteries, not both. Several interlocks — prep-before-descent (§8), the battery economy (§12), dive-length pressure (§4.3) — go slack simultaneously if inventory ends up generous. Treat carry capacity as a core balance lever, not a convenience setting, and resist the very strong late-development pull toward expanding it.

### 18.3 The maze has to stay frightening after mastery
Horror decays with exposure. Shifting layouts help, and the tonal boundary (§2) does real work by resetting the player's baseline every time they surface — but familiarity still erodes dread. Escalating numbers alone will not sustain it: a Day 200 maze that is merely *more* of the Day 40 maze is a chore with higher stats. Plan for **novelty injection at depth** — new room types, new rules, things that behave differently rather than harder.

### 18.4 Twenty days before the hook is a retention risk
The Day 20 gate is excellent for immersion and earns the reveal properly. It is also a long time to ask a new player to wait, and a serious problem for a Steam demo where you have roughly fifteen minutes to convince someone.

Mitigation worth designing early: let players *know* there is something underground long before they can reach it — a rumour in the wife's belongings, a distant concrete structure visible but sealed, a document, a sound. Curiosity can be seeded on Day 1 even if access is gated to Day 20. **The gate should withhold the maze, not the knowledge that a maze exists.**

### 18.5 The battery death spiral needs to be legible, not sudden
Spending batteries to go get batteries (§12.1) is one of the strongest tensions in the design — but only if the player can *see it coming*. If they discover they're below the threshold only when they're already stranded, it reads as an unfair loss rather than a bad decision. The player needs enough information to recognise "I have two more dives in me" several days out.

### 18.6 The two halves must not become sequential
The intended shape is an oscillation — prepare, dive, recover, repeat, with rising amplitude. The failure mode is that the game becomes *front-loaded wilderness followed by all-maze*, with the surface reduced to a restock stop once the player is equipped. Watch for this in long playtests specifically; if late-run players stop caring about seasons, the surface has stopped being a game and become a shop.

### 18.7 Lethal weather has to read as a preparation failure, not a dice roll
§10.1 lets weather kill outright, on the condition that every event had a counterplay the player could have built. The risk is that the player cannot *tell the difference* between "I was unprepared" and "I was unlucky" in the moment of dying — and a death that reads as a dice roll is a death that reads as unfair, however defensible it is on paper.

Two things have to hold, and both are tuning problems rather than design ones:

- **Onset time relative to travel time.** An acute event needs a ramp long enough that a Tier 0 player who reacts at the first sign can reach their shelter from a normal day's foraging range. If a blizzard can open faster than that walk, "read the signs" was never a real option and the death is an ambush wearing a hint's clothing.
- **The counterplay must be visibly missing beforehand.** The player should already know they have no boat, no cleared ground, no winter shelter — ideally the game should have been nagging them about it for a season. Dying to the gap is fine; discovering the gap existed only as it kills you is not.

The mirror risk: over-correct and the surface becomes "wait indoors until the bar clears," which is not a game. The chronic events (drought, heatwave) exist partly to push *outward* while the acute ones pull *in* — if that balance tips, the surface stops moving.

And the framing risk: the surface being genuinely lethal must not flatten the tonal gap in §2. The maze has to stay the obviously deadlier place, or the descent loses its weight.

---

## 19. Open Questions

- **Exact seasonal acceleration ratio while below.** How many surface days should a long dive cost? Needs playtesting against how long a satisfying dive actually is.
- **Escalation curve constants** (`A` linear rate, `B`/`C` exponential rate — §4.2). Specifically: does a true exponential end runs with *pressure* or with *a wall*? If it's a wall, drop `C`, add a soft cap, or switch to a logistic curve that flattens at a brutal-but-playable ceiling.
- **Does discovering further entrances after the first do anything to escalation?** Recommend no — only the first discovery and first entry anchor the curve, or scouting becomes self-punishing.
- **Mushroom sustenance numbers.** How much does a mushroom actually buy you, is there any water source below at all, and does carried food spoil at the accelerated rate?
- **Does *any* lore persist across runs?** A persistent lore/document codex (knowledge carries over, items never do) would let the wife thread build across runs instead of resetting every death. Worth deciding early — it changes how documents are written.
- **Does the no-pause rule survive** if single-slot saving is added? (Recommend yes — it's doing real work for the horror, and camp-only saving already makes the maze unsaveable.)
- **What shelter tier qualifies as a save point?** Recommend basic shelter and above, so a bare campfire doesn't count and siting a proper camp is a real decision.
- **What forces movement across the surface map?** Seasons and resource depletion are the stated drivers. Needs concrete numbers — how fast a valley depletes, how far you must range, how many seasonal camps a run realistically sustains.
- **Cabin degradation rate.** Fast enough that abandoning one has teeth, slow enough that maintenance isn't a chore. This number decides whether the nomadic loop feels like planning or like babysitting.
- **Level 2 descent** (roadmap Phase 12) — still in, and now reads as a third layer below the maze. Needs re-siting within the new structure.
- **Do maze entrance locations persist across the shifting zones,** or can a regeneration event bury the way you came in? (Very good horror, potentially very unfair.)
- **Tile spec — settled** (2026-09-11). It lives in code as `src/core/grid.hpp` and reads: every sheet is authored at **16 art px per world cell** and drawn at exactly **2×**, so a cell is 32 canvas px and one art pixel is a 2×2 block; sprites address sheets in whole tiles (`grid::srcTile`) and their destination rectangles are derived from the source (`grid::destFor`), so no draw site carries a scale of its own. Tall props stand on their floor cell and grow upward (`grid::standingOn`). The canvas is blitted to the window at 1 / 1.5 / 2 / 3 so an art pixel stays a whole number of screen pixels. Pixel Crawler is natively 16px and needs nothing; a 32-based pack (`Mobs/`) must be halved with `tools/downsample_sheet.py` before it enters `assets/`. The master palette is settled too (`docs/palette.md`). Still open from the original question: the 3/4-view convention.
- **Rivers, world shape, size — settled** (05-10-2026, §16.1/§16.5): downhill rivers with priority-flood lakes; a finite island in a ≥500-tile wrapping ocean; ~2k tiles across to start, growing toward ~8k.
- **Does height ever affect movement?** Flat and free for now (§16). Revisit with stamina — slower uphill, impassable peaks, or neither.
- **Engine decision.** At multi-year, two-genre scope, the from-scratch C++/raylib build costs significantly more than it did at the original scope. Not urgent, but should be a conscious choice rather than an inherited default.
- **Extreme weather frequency and variance.** "About one a month" is the stated target — but is it a fixed cadence, a Poisson process, or season-weighted draws? A fixed cadence is learnable and defuses the tension; pure randomness produces the back-to-back blizzards that end runs unfairly. Probably season-weighted with a minimum gap.
- **Which counterplay builds actually exist, and what do they cost?** The boat, the firebreak, the water store and the winter-shelter deadline are load-bearing the moment weather can kill (§10.1) — each needs to be cheap enough to be a reasonable ask and expensive enough that skipping it is a real gamble.
- **How long is an acute event's onset ramp?** (§18.7) The single number that decides whether Tier 0 play is tense or unfair. Must be tuned against how far a normal day's foraging takes you from shelter.
- **Does the weather station draw power continuously, or only when read?** Continuous makes it a real competitor with torchlight for batteries and can go dark at the worst moment; on-demand is friendlier and much less interesting. Leaning continuous, with a low draw.
- **Can an extreme event fire while the player is in the maze, and do they get any hint of it?** Recommend yes, and no — surfacing into an aftermath you could not have known about is the same lesson seasonal drift teaches (§4.3).
- **Is drought an event or a season modifier?** It has no acute moment and no shelter interaction, so it may belong with the seasons rather than the event system.
- **Do weather events interact with each other?** Drought into heatwave into bushfire is the obvious chain and is worth building deliberately; the rest probably should not compound.

---

## 20. Copyright & Licensing

**Moved to roadmap Phase 13** (GUI, Graphical Polish & Licensing): a proprietary all-rights-reserved `LICENSE`, `THIRD_PARTY_NOTICES.md` for Raylib / ImGui / rlImGui / GoogleTest, and a copyright line in the README — done before the project is shown around or released.
