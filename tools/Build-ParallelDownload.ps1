$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$output=Join-Path $script:GoldCraftRoot 'build/parallel-download'
$inputPaths=@(Get-ChildItem "$script:GoldCraftRoot/native/parallel_download" -File)+@(Get-ChildItem "$script:GoldCraftRoot/tests/parallel-download" -File)
$inputs=@{}
foreach($item in $inputPaths){$inputs[$item.FullName]=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
$revision=git -C $script:GoldCraftRoot rev-parse HEAD
$arguments=Get-GoldCraftClangCMakeArguments $compiler
& cmake -S "$script:GoldCraftRoot/native/parallel_download" -B $output -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" -DCMAKE_BUILD_TYPE=Release @arguments
if($LASTEXITCODE){throw 'ParallelDownload configuration failed'}
& cmake --build $output --parallel 2
if($LASTEXITCODE){throw 'ParallelDownload compilation failed'}
foreach($path in $inputs.Keys){if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $inputs[$path]){throw "Build input changed: $path"}}
$relativeInputs=@{}
foreach($path in $inputs.Keys){$relativeInputs[[IO.Path]::GetRelativePath($script:GoldCraftRoot,$path)]=$inputs[$path]}
@{revision=$revision;inputs=$relativeInputs;dllSha256=(Get-FileHash "$output/ParallelDownload.dll").Hash;pdbSha256=(Get-FileHash "$output/ParallelDownload.pdb").Hash;verifiedUtc=[DateTime]::UtcNow.ToString('O')} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$output/build-receipt.json" -Encoding utf8
