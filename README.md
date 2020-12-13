# vip-pull
A bash script + C program that downloads "Chosen" music from the [Vidya Intarweb Playlist](https://www.vipvgm.net/), avoiding those already downloaded.

The script uses `curl` to download the JSON file from https://www.vipvgm.net/roster.min.json. This JSON, as far as I am aware, is the only location where you can find the IDs for the music. The IDs are not in the XML files referenced by the [credited source code](https://github.com/fpgaminer/vip-html5-player). The "Export" buttons on VIP give you a comma-separated list of these IDs, so it's necessary to know them.

The JSON is piped into the C program called `vip-pull`. Three command-line arguments are also passed to this program:
- `target-dir`: The directory where the music will be downloaded to and where all the previously-downloaded VIP music is kept.
- `"VIP-export-chosen-string"`: The string of comma-separated IDs given when you click "Export Chosen" on the VIP HTML player.
- `"VIP-export-sourced-string"`: Same as above but for "Export Sourced."

`vip-pull` will find all "Chosen" music which is not already present in `target-dir` and print the download URL to `stdout`. Control returns to the script which passes each download URL back to `curl` to download those files to `target-dir`.

The "Sourced" string is used to download the source version of songs that were both chosen and sourced.

# Usage
Run `make` to compile the C program to an executable called `vip-pull`.

Then, execute the script: `pull.sh target-dir "VIP-export-chosen-string" "VIP-export-sourced-string"`
