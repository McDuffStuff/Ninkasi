# Ninkasi

Lightweight, local brewery management software. See [DESIGN.md](DESIGN.md) for the product vision.

Built with C++20, [Dear ImGui](https://github.com/ocornut/imgui) (docking branch), GLFW/OpenGL and SQLite. All dependencies are downloaded automatically by CMake on the first configure.

## Building

### Windows
Requirements: Visual Studio 2022+ or Build Tools with the **Desktop development with C++** workload (it includes CMake and Ninja).

```powershell
.\scripts\build.ps1                  # debug build + tests
.\scripts\build.ps1 -Config release  # release build + tests
.\scripts\build.ps1 -Run             # build, test, launch
```

### Linux
Requirements: GCC 11+ or Clang 14+, CMake 3.24+, Ninja, and the GLFW build dependencies:

```sh
# Debian/Ubuntu
sudo apt install build-essential cmake ninja-build libgl1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols

cmake --preset debug && cmake --build --preset debug && ctest --preset debug
./build/debug/ninkasi
```

## Running
```
ninkasi [--db <path>]
```
By default, data lives in `%APPDATA%\Ninkasi` (Windows) or `~/.local/share/ninkasi` (Linux): the `ninkasi.db` database and `imgui.ini` window layout. Pass `--db` to use a different database, e.g. a scratch brewery for testing.

### The Brewery Floor
- **Viewing:** hover over equipment for a summary, click it for its details window, right-click it for actions, drag empty space (or middle-drag) to pan, and scroll to zoom. **Fit** frames everything. Tanks show the beer in them with its latest gravity, temperature and volume; a numbered badge counts open tasks (red when one is overdue).
- **Editing:** tick **Edit layout**, then drag equipment to move it, drag its corner handle to resize, double-click for details, or press Delete to remove it. Add equipment from the toolbar or by right-clicking the floor.
- **Details windows** can be docked, left floating, popped out into their own OS window (e.g. on a second monitor), or maximized. Windows left open reopen next time.
- **Units:** the **Units** menu sets the brewery-wide units (with US and metric presets). Each piece of equipment can override them in its **Units** tab.

### Recipes
- **Recipes > Recipe library** (or a recipe station's **Open recipe library**): create, open, duplicate and delete recipes, and import BeerJSON files exported from other brewing software, either with **Import BeerJSON...** or by dropping the files onto Ninkasi.
- **The recipe editor** recalculates OG, FG, ABV, IBU, color, brew-day volumes, yeast needs and mash pH as you type, and saves automatically.
- **Recipes > Ingredient library:** the fermentables, hops and yeasts recipes pick from. It starts with generic ingredients at typical values; update them to your suppliers' analyses. Recipes keep their own copy of each ingredient, so library edits never change existing recipes.
- **Recipes > Brewing settings:** the formulas used (IBU, color, ABV) and the defaults new and imported recipes start from (batch size, equipment, mash steps, source water).

### Brewing and the cellar
- **Log a brew** (on a brewhouse) is data entry after the fact: pick the recipe, check the batch code (the next number is suggested) and the brew date, then the knock-out: the tank, volume, OG and the ingredients actually used, which start at the recipe's amounts. Press Enter to save. To fill a big fermenter with several brews, choose **Another brew for a batch**.
- **Knock-out later:** untick *Log the knock-out now* to save just the brew. The brewhouse then shows *Knock-out to log*; log it from the brewhouse (or the batch) whenever you get to it. The batch keeps its own copy of the recipe as brewed.
- **In the cellar**, a tank's window or right-click menu has **Log reading** (hydrometer with temperature correction, refractometer with Terrill's correction, or a digital meter), **Transfer**, **Package**, **Correct volume** and **Dump**. Beer only goes into tanks that are empty, big enough and in service; moving it into a brite tank starts conditioning. A batch is finished when the last of it is packaged or dumped.
- **Tanks** that have been emptied are marked *Needs cleaning*; mark them clean, or out of service, from their window.
- **View > Batches** lists every batch. A batch's window has its fermentation chart, readings, brews, the ingredients used, its volume history, its recipe and its log.
- **View > Tasks** lists open tasks by due date; each piece of equipment also has its own **Tasks** tab, and a **Log** of everything that happened to it.
- Every change in the volume of beer in a tank is recorded in a ledger that TTB reports will be built from. History is never edited: mistakes are fixed with corrections.

## Project layout
```
src/calc/     brewing math and unit conversions (pure, no DB/UI)
src/domain/   plain data types and business rules
src/data/     SQLite wrapper, schema migrations, repositories
src/app/      services, session, file paths
src/ui/       ImGui screens
tests/        doctest unit tests (mirror src/)
```
Dependencies point downward only: `ui → app → domain/data → calc`. `calc` is its own library (`ninkasi_calc`) with no dependencies, so the linker enforces that it stays pure.

## Conventions
- Values are stored and calculated in canonical units (liters, kilograms, °C, specific gravity, minutes) and converted only for display or input; see `src/calc/units.h`. Parameter names carry the unit: `volume_l`, `mass_kg`, `temp_c`.
- Ratios such as alpha acids, yields, efficiency and ABV are fractions: `0.055` means 5.5 %.
- Calculations never throw. Physically impossible inputs return NaN, which the UI shows as `--`.
- Each formula's source is cited in its header, and the tests check it against published reference values.
- Source files are UTF-8 without a byte-order mark.

## Third-party
Dear ImGui, ImPlot and GLFW (MIT/zlib-style licenses), SQLite (public domain), nlohmann/json and doctest (MIT), and the Roboto font (Apache License 2.0), which is compiled into the executable.
