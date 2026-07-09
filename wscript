import os

top = '.'
out = 'build'

def options(ctx):
  ctx.load('pebble_sdk')

def configure(ctx):
  ctx.load('pebble_sdk')

def build(ctx):
  ctx.load('pebble_sdk')
  binaries = []
  cached_env = ctx.env
  # Allow $LOP_CFLAGS in the environment to inject extra defines
  # (used by scripts/capture_playthrough.sh to warp to a given level).
  extra_cflags = os.environ.get('LOP_CFLAGS', '').split()
  for platform in ctx.env.TARGET_PLATFORMS:
    ctx.env = ctx.all_envs[platform]
    ctx.set_group(ctx.env.PLATFORM_NAME)
    if extra_cflags:
      ctx.env.append_value('CFLAGS', extra_cflags)
    app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
    ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'),
                  target=app_elf, bin_type='app')
    binaries.append({'platform': platform, 'app_elf': app_elf})
  ctx.env = cached_env
  ctx.set_group('bundle')
  ctx.pbl_bundle(binaries=binaries, js=[])
