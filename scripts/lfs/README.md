# LittleFS image builder

`make_littlefs_image.py` creates a mountable LittleFS image with the host
`mklittlefs` tool.

Add an individual file at the LittleFS root under its basename:

```sh
python3 scripts/lfs/make_littlefs_image.py \
  --file build/zephyr/zephyr.bin.ota \
  --size 2M --output littlefs.bin
```

Add every file below one or more directories. Each directory's *contents* are
copied recursively to the LittleFS root; relative paths below the directory
are retained:

```sh
python3 scripts/lfs/make_littlefs_image.py \
  --path assets --path configuration \
  --size 512K --output littlefs.bin
```

`--file` and `--path` can be combined. A duplicate target path is rejected
rather than overwritten. Geometry defaults are a 4096-byte erase block and
16-byte read/program sizes; use `--block-size`, `--read-size`, and
`--prog-size` when the target filesystem requires different values.
