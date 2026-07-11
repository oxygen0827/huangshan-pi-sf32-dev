# Upstream Repositories

This project is the working application repository for the LCKFB Huangshan Pi /
立创黄山派 SF32LB52x board. It records both upstream sources used during
bring-up, but it does not vendor every upstream repository.

## Official SiFli SDK

- Repository: https://gitee.com/SiFli/sifli-sdk
- Branch used: `main`; verified local SDK version: 2.5.0 build `cbac8e56`
- Local path on this Mac: `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk`

Clone command:

```bash
git clone --recursive -b main https://gitee.com/SiFli/sifli-sdk.git
```

Role in this project:

- Provides the SF32 toolchain environment through `export.sh`
- Provides board support, RT-Thread, LCD/touch drivers, LVGL integration, and
  SCons build logic
- Remains an external dependency because it is large and has its own submodules

Important local patch:

- `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk/customer/peripherals/co5300/co5300.c`
- Accepts CO5300 IDs `0x331100`, `0x1fff`, and `0x3fff`
- Uses `HAL_LCDC_SYNC_DISABLE` for the verified panel path

## LCKFB Huangshan Pi Examples

- Repository: https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
- Local path on this Mac: `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/lckfb-hspi-ulp_example`

Clone command:

```bash
git clone https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
```

Role in this project:

- Provides the verified `lvgl/watch` example used as the first working
  application shell
- Confirms display, touch, resource generation, launcher registration, and flash
  flow on the LCKFB Huangshan Pi
- Supplies the application structure copied into this repository

## What This Repository Contains

Copied into this repository:

- `project/` SCons application project from the verified LCKFB `lvgl/watch`
  flow
- `src/` GUI app, resource, and launcher source tree
- Local helper scripts under `scripts/`
- Board notes under `docs/`

Not copied into this repository:

- The full SiFli SDK
- The full LCKFB examples repository
- SDK tool downloads under `.sifli`

This keeps the new project focused on board application development while still
recording the exact upstream sources needed to rebuild the environment.

## Related Documentation In This Repository

- `docs/sifli-resources.md`: external SiFli document and tool links
- `docs/sifli-sdk-map.md`: local SDK source map for this board
- `docs/sifli-learning-path.md`: recommended learning order for Huangshan Pi
