Mod for the Game Satisfactory

FactorySpawner takes the hassle out of manually placing and wiring every machine. Simply tell it what you want to produce and how many machines you need, and it will:

- Spawn the requested machines and set their recipes

- Automatically connect everything with belts

- Wire up power poles and connect them to the grid

- Handle splitters, mergers, and constructors for smooth production chains

Usage:

For a demo factory, just place the "Factory Spawner" - buildable.

Open the chat and type:

/FactorySpawner {number} {machine type} {recipe} (without curly braces)

- number: Number of machines in a row
- machine type: Smelter, Constructor, Assembler, Foundry or Manufacturer (others currently not supported)
- recipe: e. g. IngotIron, IronPlate, Motor, Computer

You can chain multiple commands, separated by commas. This results in multiple rows of machines:

/FactorySpawner 2 Smelter IronIngot, 3 Constructor IronPlate

This spawns 2 Smelters making Iron Ingots and 3 Constructors making Iron Plates, all connected with belts and power.

You can also control which belts are used:

/FactorySpawner BeltTier 3

This forces the mod to use Mk3 belts (default Mk1).
