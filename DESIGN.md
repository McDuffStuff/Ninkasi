# **Ninkasi**
---

### Overview

Ninkasi is a lightweight data driven brewery software that helps brewers track inventory, calculate recipes, manage tasks, organize production timelines, track gravities, and many other things. Ninkasi will have a modern but simple GUI that enables snappy responsiveness and intuitive usage. Ninkasi will have all brewery related calculations done in a common-sense approach where brewers have full control over how each system works. The software should be modular so that each brewery can setup Ninkasi to their liking.

---
### Technical Specs

* Ninkasi should be self contained and run locally on Windows and Linux.
* SQLite will be the local database structure.
* Clean GUI

---
### Aesthetics
* Clean, modern design
* Simple, intuitive navigation
* Dark and light modes
* Highly customizable to fit any brewers needs
---
### Main Elements

#### - Brewery Floor
This is where brewers should spend most of their time. This will function like a graphical physical representation of their brewery. Most of the day to day data will be viewable from this screen. They will be able to add "elements" such as dry storage, fermentation tanks, brewhouses, bright tanks, keg washing, cold storage, recipe crafting station, etc. Brewers will use this menu to set tasks for specific tanks, view logged activity, brew beers, keg beers and move them to cold storage, etc. Important calculations will be automated at the appropriate stations/tanks. Beers can be transferred between all stages of production as long as the target is available and empty.

#### - Specific Elements View
Each active element in the brewery will have an individual screen that can be accessed through an option when clicking on an element in the Brewery Floor. This Screen will contained detailed data and logs about that specific element. Dry Storage will have inbound and outbound goods logged. The brewhouse will have batch id's and details.

#### - Data Export
There should be easy ways to export data in csv files or to connect a google account for automated Google Sheets backups of all important data. This should be configurable in a settings menu.