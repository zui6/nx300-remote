# Build NEON daemon (musl static, cortex-a9) with hardware-accelerated libjpeg
$ErrorActionPreference = 'Stop'

$zig = (Get-ChildItem 'D:\AndroidSdk\zig' -Recurse -Filter zig.exe | Select-Object -First 1).FullName
if (-not $zig) { throw 'zig.exe not found' }

$src = 'D:\workbuddy\2026-10-16-19-57-08\daemon-build'
$out = Join-Path $src 'nx-daemon-neon'

$sources = @(
    'api_server.c', 'command.c', 'fields.c', 'gallery.c', 'input.c',
    'jpeg_nv12.c', 'lcd.c', 'led.c', 'liveview.c', 'main.c',
    'externals/mongoose/mongoose.c',
    'names.c', 'network.c', 'nx_model.c', 'nxtouch.c', 'osd.c', 'params.c',
    'screen_jpeg.c', 'shutter.c', 'status.c', 'ui_overlay.c', 'util.c'
)

$defines = @(
    '-DMG_DISABLE_HTTP_DIGEST_AUTH',
    '-DMG_DISABLE_MQTT',
    '-DMG_DISABLE_MD5',
    '-DMG_DISABLE_JSON_RPC',
    '-DMG_DISABLE_SOCKETPAIR'
)

$objs = @()
$objs += (Get-ChildItem (Join-Path $src 'build\jpegobj_neon') -Filter *.o | ForEach-Object { $_.FullName })
$objs += (Get-ChildItem (Join-Path $src 'build\simdobj') -Filter *.o |
          Where-Object { $_.Name -ne 'jsimd_stub.o' } | ForEach-Object { $_.FullName })

Push-Location $src
$ccArgs = @('cc', '-target', 'arm-linux-musleabi', '-mcpu=cortex_a9', '-mfpu=neon', '-mfloat-abi=softfp',
            '-static', '-O2', '-Wall',
            '-I', 'externals/mongoose', '-I', 'externals/libjpeg', '-I', '.') + `
          $defines + @('-o', $out) + $sources + $objs
& $zig @ccArgs 2>&1 | ForEach-Object { $_ }
$code = $LASTEXITCODE
Pop-Location

if ($code -ne 0) { throw "compile failed (exit $code)" }
$f = Get-Item $out
'OK: {0} ({1:N0} bytes)' -f $f.Name, $f.Length
