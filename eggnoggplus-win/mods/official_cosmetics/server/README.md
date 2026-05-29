# Official Cosmetics Server Pack

Host the generated contents of this folder at:

- `https://loafiieee.com/eggnogg/cosmetics/v1/manifest.json`
- `https://loafiieee.com/eggnogg/cosmetics/v1/assets/hats.png`

## Add A Hat

On Ubuntu, install the one image dependency:

```bash
sudo apt update
sudo apt install python3 python3-pil
```

Create one `32x32` PNG per hat:

```bash
python3 build_cosmetics.py --new-hat-id mushroom --new-hat-name "Mushroom"
```

Edit `hats/mushroom.png`, then edit `hats/mushroom.json` for placement/motion:

```json
{
  "id": "mushroom",
  "name": "Mushroom",
  "motion": true,
  "scale": 0.65,
  "x": 0.0,
  "y": 10.5,
  "bob": 0.5,
  "tilt": 5.0,
  "drag": 0.3,
  "allowed_online": true
}
```

Build the hosted files:

```bash
python3 build_cosmetics.py
```

Upload `manifest.json` and the `assets` folder to `/eggnogg/cosmetics/v1/`.

Example deploy command from inside this folder:

```bash
rsync -av manifest.json assets/ /var/www/html/eggnogg/cosmetics/v1/
```

## Remove A Hat

Delete its `hats/<id>.png` and `hats/<id>.json`, then run:

```bash
python3 build_cosmetics.py
```

## Import An Existing Sheet

If you have an old `hats.png` sheet and matching `manifest.json`, split it into editable per-hat files:

```bash
python3 build_cosmetics.py --import-sheet --sheet-path path/to/hats.png --manifest-path path/to/manifest.json
```

The generated manifest assigns `sprite_index` automatically based on the sorted hat files.

## Windows

The PowerShell version is still available for local Windows work:

```powershell
.\build_cosmetics.ps1
```
