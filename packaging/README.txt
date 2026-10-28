Ninkasi {version}
Brewery management: the brewery floor, recipes, batches, inventory,
packaging, TTB reports and backups, all on this computer.

Starting it
  Run ninkasi.exe (or Ninkasi in the Start menu, if you used the
  installer). The first time, Ninkasi creates the brewery's data in
  %APPDATA%\Ninkasi\ninkasi.db, and from then on makes a backup in
  %APPDATA%\Ninkasi\backups each day it's used.

  Windows may say it protected your PC from an unrecognized app, because
  this version isn't signed with a code-signing certificate yet. Choose
  "More info", then "Run anyway".

Your data
  Uninstalling Ninkasi, or deleting this folder, leaves the brewery's data
  in %APPDATA%\Ninkasi. File > Backups and export saves copies anywhere
  you like and exports the records as CSV files.

  To use a different database, such as a copy for trying things out:
    ninkasi.exe --db C:\path\to\brewery.db

More
  https://github.com/McDuffStuff/Ninkasi
  The licenses of the open-source software Ninkasi is built with are in
  THIRD-PARTY-NOTICES.txt.
