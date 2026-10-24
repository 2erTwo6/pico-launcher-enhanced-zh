# File Associations
Pico Launcher supports custom file associations for file extensions other than `.nds`.
By setting up a file association, files with the specified extension will be opened using the specified DS application.
The file path is passed via argv.

## How to setup file associations
1. Open `/_pico/settings.json`.
2. If not present yet, add a `fileAssociations` key.
3. For each file association add a key inside the `fileAssociations` key. For example:
    ```json
    "fileAssociations": {
      "xyz": {
        "appPath": "/path/to/xyz/app.nds"
      },
      "gba": {
        "appPath": "/myGbaApp.nds"
      }
    }
    ```
    With this setup `.xyz` files will be opened by `/path/to/xyz/app.nds` and `.gba` files will be opened by `/myGbaApp.nds`.

## Files opened through a file association are games

A file opened through a file association counts as a game, like a DS game:
- **X** marks it as a favorite, and holding **X** marks it as completed.
- **Delete game** in the menu deletes it. Its save stays: an emulator may name its save its own way, and a `.sav` with the same name may belong to a DS game in the same folder. The confirmation names that save as kept when there is one. `.gba` and `.agb` files are the exception: deleting one also deletes its `.sav`, as for a DS game.
- **SELECT + A** can pick it as a random game.
- It counts in the **in folder** figure of the statistics panel.

These extensions are not games, in any case: `txt`, `bmp`, `png`, `jpg`, `jpeg`, `gif`, `dpg`, `avi`, `mp3`, `ogg`, `wav`, `mod`, `xm`, `mid` and `nsf`. They open as before, but can't be marked, deleted or picked, and the **in folder** figure leaves them out.
