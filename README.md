# Factory Spawner
https://ficsit.app/mod/FactorySpawner

**Factory Spawner** takes the hassle out of manually placing and wiring every machine.  
Simply tell it what you want to produce and how many machines you need, and it will:

- ✅ Spawn the requested machines and set their recipes  
- 🔄 Automatically connect everything with belts  
- ⚡ Wire up power poles and connect them to the grid  
- 🛠️ Handle splitters, mergers, and constructors for smooth production chains  

---

## Usage

### Quick Start
- Unlock the Schematic "Factory Spawner" (Tier 2).
- Select the Recipe: Production → Manufacturers → Factory Spawner
- Just place the demo factory.

![Demo Screenshot](https://raw.githubusercontent.com/uniqueSimon/FactorySpawner/refs/heads/master/Images/screenshot.PNG)


### Customize Factory
Open the chat and type:
```
/FactorySpawner {number} {machine type} {recipe} {underclock}
```
(without curly braces)

- **number** → Number of machines in a row
- **machine type** → Smelter, Constructor, Assembler, Foundry, or Manufacturer
- **recipe** (optional) → e.g. IngotIron, IronPlate, Motor, Computer
- **underclock** (optional) → The clock speed of each machine in the row (Value in %: 0 - 100)

Afterwards, place the **Factory Spawner** Recipe again. The factory will have changed!

You can chain multiple commands, separated by commas.
```
/FactorySpawner {number 1} {machine type 1} {recipe 1}, {number 2} {machine type 2} {recipe 2}
```

---

## Examples

This spawns **2 Smelters** making Iron Ingots and **3 Constructors** making Iron Plates, all connected with belts and power:

```
/FactorySpawner 2 Smelter IngotIron, 3 Constructor IronPlate
```

Here is the config from the screenshot (without recipes).
```
/FactorySpawner 12 Smelter, 10 Constructor, 5 Foundry, 10 Constructor, 4 Assembler, 3 Manufacturer
```
You can also control which belts are used:
```
/FactorySpawner BeltTier 3
```

This forces the mod to use **Mk3 belts** (default: Mk1).

---

## ⚡ Automatically Generate Commands

👉 [**Open Satisfactory Planner**](https://uniquesimon.github.io/satisfactory-planner/)

With this tool you can **plan your factories online** and instantly generate a **config command string**.  
No manual typing required — just:

1. Design your factory setup in the planner.  
2. Copy the generated config string to your clipboard.  
3. Paste it directly into the in-game chat (`Ctrl + V`).  

Your factory will be ready in seconds! 🚀

## Safe to Uninstall

All placed buildables are just **vanilla ones**, so you can safely uninstall the mod at any time.

## Known issues
- After switching save games, the game crashes. You have to restart the game, then it works again.
- The schematic can be unlocked before splitter and merger. In that case those are not added to the cost calculation. I should add that dependency.
- You can spawn machines that have not yet been unlocked, like manufacturers.

## Future plans
- Fix save game bug
- Add all remaining machines
- Add conveyor lifts (Have to find out how to spawn them properly)
- Improve preview hologram

## Known issues
- The schematic can be unlocked before splitter and merger. In that case those are not added to the cost calculation. I should add that dependency.
- You can spawn machines that have not yet been unlocked, like manufacturers.

## Feedback on Discord

https://discord.gg/Yzgw9yQK