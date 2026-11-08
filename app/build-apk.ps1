# Build + sign the NX300 Remote APK (no Gradle, manual aapt2/d8/zipalign/apksigner)
$ErrorActionPreference = 'Stop'

$sdk = 'D:\AndroidSdk'
$app = 'D:\workbuddy\2026-10-16-19-57-08\nx300-app'
$bt  = Join-Path $sdk 'build-tools\34.0.0'
$aj  = Join-Path $sdk 'platforms\android-34\android.jar'
$jdk = Join-Path $sdk 'jdk17\jdk-17.0.2'
$env:JAVA_HOME = $jdk
$env:PATH = "$($jdk)\bin;" + $env:PATH

$build = Join-Path $app 'build'
Remove-Item (Join-Path $build 'classes'), (Join-Path $build 'dex') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path (Join-Path $build 'classes'), (Join-Path $build 'dex') -Force | Out-Null

Push-Location $app

# 1. compile
& "$jdk\bin\javac.exe" --release 8 -encoding UTF-8 -classpath $aj -d (Join-Path $build 'classes') 'src\com\nx300\remote\MainActivity.java'
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
Write-Host 'javac OK'

# 2. resources + manifest
& (Join-Path $bt 'aapt2.exe') link -o (Join-Path $build 'base.apk') -I $aj --manifest AndroidManifest.xml --min-sdk-version 21 --target-sdk-version 34
if ($LASTEXITCODE -ne 0) { throw 'aapt2 link failed' }

# 3. dex
$classes = Get-ChildItem (Join-Path $build 'classes') -Recurse -Filter *.class | ForEach-Object { $_.FullName }
& (Join-Path $bt 'd8.bat') --release --min-api 21 --lib $aj --output (Join-Path $build 'dex') @classes 2>$null
if (-not (Test-Path (Join-Path $build 'dex\classes.dex'))) { throw 'd8 failed' }

# 4. pack dex into apk
Add-Type -AssemblyName System.IO.Compression.FileSystem
$unsigned = Join-Path $build 'unsigned.apk'
Copy-Item (Join-Path $build 'base.apk') $unsigned -Force
$zip = [System.IO.Compression.ZipFile]::Open($unsigned, 'Update')
$e = $zip.GetEntry('classes.dex'); if ($e) { $e.Delete() }
[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $build 'dex\classes.dex'), 'classes.dex') | Out-Null
$zip.Dispose()

# 5. align
& (Join-Path $bt 'zipalign.exe') -f 4 $unsigned (Join-Path $build 'aligned.apk')
if ($LASTEXITCODE -ne 0) { throw 'zipalign failed' }

# 6. sign (same debug key so it upgrades in place)
$ks = Join-Path $app 'debug.keystore'
& (Join-Path $bt 'apksigner.bat') sign --ks $ks --ks-pass pass:android --key-pass pass:android `
    --out (Join-Path $app 'nx300-remote.apk') (Join-Path $build 'aligned.apk')
if ($LASTEXITCODE -ne 0) { throw 'apksigner failed' }

Pop-Location

$apk = Join-Path $app 'nx300-remote.apk'
& (Join-Path $bt 'apksigner.bat') verify $apk
$fi = Get-Item $apk
'APK OK: {0:N0} bytes  {1}' -f $fi.Length, (Get-FileHash $apk -Algorithm SHA256).Hash.Substring(0,16)
