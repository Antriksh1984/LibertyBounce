# Build the local music importer

Tested with Python 3.12 x64 on Windows. Install the dependencies from requirements-build.txt in your own build environment, then run from this folder:

```text
python -m pip install -r requirements-build.txt
python -m PyInstaller --noconfirm --onefile --windowed --noupx --name LibertyBounceMusic --collect-data _soundfile_data --collect-binaries _soundfile_data --hidden-import _cffi_backend --exclude-module tkinter import_music.py
```

Copy dist/LibertyBounceMusic.exe into the game's scripts folder. You can also run the source directly with Python. No network access is used during music import or beat analysis. Third-party components remain under their own licenses; NumPy and SoundFile are not part of LibertyBounce's own license. The supplied Python source permits rebuilding the executable with modified/replacement libsndfile and other dependencies; do not bundle users' imported tracks/caches in releases.
