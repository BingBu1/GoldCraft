param([string]$OutputDirectory = 'build/infection-sprite')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$output = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $OutputDirectory)
[void][IO.Directory]::CreateDirectory($output)
Add-Type -AssemblyName System.Drawing
# Rasterize the standard Unicode biohazard glyph; no font is redistributed.
$large = [Drawing.Bitmap]::new(512, 512)
$graphics = [Drawing.Graphics]::FromImage($large)
$font = [Drawing.Font]::new('Segoe UI Symbol', 320, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$graphics.Clear([Drawing.Color]::Black)
$graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.DrawString([string][char]0x2623, $font, [Drawing.Brushes]::White, 0, 0, [Drawing.StringFormat]::GenericTypographic)
$graphics.Dispose(); $font.Dispose()
$left=512; $top=512; $right=0; $bottom=0
for($y=0;$y -lt 512;$y++){for($x=0;$x -lt 512;$x++){
    if($large.GetPixel($x,$y).R -gt 16){
        $left=[Math]::Min($left,$x); $right=[Math]::Max($right,$x)
        $top=[Math]::Min($top,$y); $bottom=[Math]::Max($bottom,$y)
    }
}}
if($right -le $left -or $bottom -le $top){throw 'Biohazard glyph was not rendered'}
$sprite=[Drawing.Bitmap]::new(20,20)
$graphics=[Drawing.Graphics]::FromImage($sprite)
$graphics.Clear([Drawing.Color]::Black)
$graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.DrawImage($large, [Drawing.Rectangle]::new(1,1,18,18), $left,$top,($right-$left+1),($bottom-$top+1),[Drawing.GraphicsUnit]::Pixel)
$graphics.Dispose(); $large.Dispose()
$sprite.Save((Join-Path $output 'zp_infection.png'),[Drawing.Imaging.ImageFormat]::Png)
$preview=[Drawing.Bitmap]::new(240,240)
$graphics=[Drawing.Graphics]::FromImage($preview)
$graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::Half
$graphics.DrawImage($sprite,[Drawing.Rectangle]::new(0,0,240,240))
$graphics.Dispose()
$preview.Save((Join-Path $output 'preview.png'),[Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
$path=Join-Path $output 'zp_infection.spr'
$writer=[IO.BinaryWriter]::new([IO.File]::Create($path))
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('IDSP'))
    $writer.Write([int]2); $writer.Write([int]2); $writer.Write([int]1) # v2, VP_PARALLEL, additive
    $writer.Write([single][Math]::Sqrt(200)); $writer.Write([int]20); $writer.Write([int]20)
    $writer.Write([int]1); $writer.Write([single]0); $writer.Write([int]0)
    $writer.Write([uint16]256)
    for($i=0;$i -lt 256;$i++){ $writer.Write([byte]$i); $writer.Write([byte]$i); $writer.Write([byte]$i) }
    $writer.Write([int]0); $writer.Write([int]-10); $writer.Write([int]10)
    $writer.Write([int]20); $writer.Write([int]20)
    for($y=0;$y -lt 20;$y++){for($x=0;$x -lt 20;$x++){ $writer.Write([byte]$sprite.GetPixel($x,$y).R) }}
} finally { $writer.Dispose(); $sprite.Dispose() }
if((Get-Item -LiteralPath $path).Length -ne 1230){throw 'Unexpected GoldSrc SPR size'}
Get-FileHash -LiteralPath $path -Algorithm SHA256 | Select-Object Path,Hash
