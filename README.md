# Shadows of Eternity

A **turn-based, terminal-based role-playing game (RPG) written in C**. Create a hero, select a class, battle increasingly difficult enemies, collect randomized items, and overcome final bosses through a combination of strategy and dice-roll luck.

**Created by:** Braulio Millanés Loza and Diego Armando Estévez Martínez.

## Features

- **Four playable classes:** Assassin, Knight, Mage, and Warrior, each with distinct starting attributes and spell availability.
- **Turn-based combat:** physical attacks, spell casting, evasion, optional special events, and consumable items.
- **d20 mechanics:** twenty-sided die rolls modified by the character's Luck attribute; enemy selection and special effects also use random rolls.
- **Elemental combat:** physical, fire, frost, light, and shadow damage, with physical/magical defenses and weaknesses that amplify damage.
- **Magic system:** spells implemented through C function pointers, including fire, frost, light, shadow, and the special Fate Die spell.
- **Item rewards:** randomly generated attribute boosts, a five-slot inventory, and the ability to replace items when full.
- **Progressive encounters:** three ordinary chapters of increasing difficulty, a final boss, and a secret boss triggered by a rapid final-boss victory.
- **Enemy behavior:** computer-controlled opponents select physical or magical attacks, with temporary buffs and freeze effects influencing combat.

The **interface and gameplay messages are in Spanish**; source documentation and comments are in English.

## Project files

| File | Description |
| --- | --- |
| `Shadows_of_Eternity_Final.c` | Main/final version of the game, using pointer arithmetic in several gameplay and inventory routines. |
| `Shadows_of_Eternity_Final.exe` | Precompiled Windows executable included in the original project. |

**Important:** The `.exe` files came with the original archive and do not automatically reflect changes to the source comments. Recompile from the provided `.c` files to build your own current executable. This project does not require third-party C libraries.

## Requirements

- A **C compiler** that supports C99 or later, such as GCC or Clang.
- A terminal (Windows PowerShell, macOS Terminal, or Linux shell).
- No databases, accounts, or external packages are necessary.

## Build and run

Open a terminal in the project directory (the folder containing the `.c` files).

### macOS or Linux — GCC/Clang

Compile the final version:

```bash
gcc -std=c11 -O2 Shadows_of_Eternity_Final.c -o shadows_of_eternity
```

Run:

```bash
./shadows_of_eternity
```

### Windows — GCC (MinGW-w64 or MSYS2)

Compile the final version in PowerShell or a GCC-equipped terminal:

```powershell
gcc -std=c11 -O2 Shadows_of_Eternity_Final.c -o shadows_of_eternity.exe
```

Run from PowerShell:

```powershell
.\shadows_of_eternity.exe
```

Alternatively, on Windows you can run the precompiled `.exe` files included in the archive, but compiling locally is recommended to use the provided source code.

> **Troubleshooting:** If `gcc` is not recognized, install a GCC toolchain and ensure it is available on your PATH. On macOS, Apple's Command Line Tools provide Clang; substitute `clang` for `gcc` if desired.

## How to play

1. Run either version and **enter your hero's name**.
2. **Select a class** from the on-screen menu.
3. During each turn, choose an action: physical attack, cast a spell, try to evade, use an available special event, use an inventory item, or inspect your character's statistics.
4. Watch the d20 rolls: luck, class attributes, enemy defenses, and elemental weaknesses affect outcomes.
5. Defeat enemies through the three chapters, then **select rewards** that strengthen your character. The inventory holds up to five items.
6. Defeat the **final boss** to complete the normal progression. Winning the final-boss battle in fewer than three turns unlocks a **secret encounter**.

Most gameplay prompts and results are printed in Spanish. Use the displayed option numbers to make choices.

## Technical implementation

- **Language:** C (C99-compatible code; commands above compile as C11).
- **Data modeling:** `struct` types represent characters, items, and enemy templates; `enum` types represent character classes and damage categories.
- **Memory management:** `malloc`, `calloc`, and `free` allocate/release characters, generated rewards, and inventory storage.
- **Pointers and pointer arithmetic:** used to manage inventory entries, enemy templates, and spell lists.
- **Function pointers:** each spell uses a common callback signature, `int (*)(void *, void *, int)`, allowing different spell implementations to share a dispatch mechanism.
- **Randomized gameplay:** `rand()` and `srand()` support die rolls, opponent selection, rewards, and events.
- **Modular gameplay routines:** independent functions handle combat turns, damage, spells, inventory, rewards, character creation, and enemies.

## Design notes

- This is a **single-player, offline terminal application**, not a web application or network game.
- The original logic and Spanish user-facing messages are retained; comments have been updated to English.
- The evasion implementation temporarily boosts defense, so it is tied to the game's existing defense-reset logic.
- Game inputs are interactive; running the executable requires entering choices at the terminal.

## Authors

- **Braulio Millanés Loza**
- **Diego Armando Estévez Martínez**
