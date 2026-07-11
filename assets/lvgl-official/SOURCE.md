# LVGL Official Assets

This directory contains a curated subset of official LVGL assets for Huangshan Pi UI prototyping.

## Source

Downloaded on 2026-07-05 from the Gitee mirror of the official LVGL repository:

- Mirror archive: `https://gitee.com/mirrors/lvgl/repository/archive/master.zip`
- Upstream project: `https://github.com/lvgl/lvgl`
- Archive root: `lvgl-master`
- Archive commit shown by zip metadata: `b6ed933feaa45839afabbe3de15a1d882a9e00dc`

GitHub direct archive download was attempted first, but this machine currently has `http_proxy` / `https_proxy` pointing at `127.0.0.1:7897`; direct DNS without that proxy failed, while the Gitee mirror download succeeded.

## Included Paths

Only asset and license/reference paths were extracted. The full LVGL source tree was not vendored into this project.

- `upstream-master/examples-assets` from `lvgl-master/examples/assets`
- `upstream-master/demos-music-assets` from `lvgl-master/demos/music/assets`
- `upstream-master/demos-widgets-assets` from `lvgl-master/demos/widgets/assets`
- `upstream-master/LICENCE.txt`
- `upstream-master/README.md`

## License

LVGL is MIT licensed. Keep `upstream-master/LICENCE.txt` with these assets if files are moved into firmware resources or Runtime App packages.

## Intended Use

This folder is a local design and implementation reference for future LVGL UI work. It is not automatically included in the current firmware build. When promoting an asset into firmware or an app package, copy only the specific file needed into the module that owns it and confirm memory/flash impact.
