param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'src\common\wsap.svg'
$target = Join-Path $root 'src\common\wsap.ico'
[xml]$svg = [IO.File]::ReadAllText($source)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$brushes = @{}
foreach ($gradient in $svg.svg.defs.ChildNodes) {
    if ($gradient.LocalName -ne 'linearGradient') { throw "Unsupported gradient: $($gradient.LocalName)" }
    $brush = [Windows.Media.LinearGradientBrush]::new()
    $brush.StartPoint = [Windows.Point]::new([double]::Parse($gradient.x1, $invariant), [double]::Parse($gradient.y1, $invariant))
    $brush.EndPoint = [Windows.Point]::new([double]::Parse($gradient.x2, $invariant), [double]::Parse($gradient.y2, $invariant))
    $brush.MappingMode = [Windows.Media.BrushMappingMode]::Absolute
    foreach ($stop in $gradient.stop) {
        $color = [Windows.Media.ColorConverter]::ConvertFromString($stop.GetAttribute('stop-color'))
        if ($stop.HasAttribute('stop-opacity')) { $color.A = [byte][Math]::Round(255 * [double]::Parse($stop.GetAttribute('stop-opacity'), $invariant)) }
        $brush.GradientStops.Add([Windows.Media.GradientStop]::new($color, [double]::Parse($stop.offset, $invariant)))
    }
    $brush.Freeze()
    $brushes[$gradient.id] = $brush
}
function Get-Brush([string]$Fill) {
    if ($Fill -eq 'none' -or -not $Fill) { return $null }
    if ($Fill -match '^url\(#([a-z]+)\)$') {
        if (-not $brushes.ContainsKey($Matches[1])) { throw "Unknown gradient: $Fill" }
        return $brushes[$Matches[1]]
    }
    return [Windows.Media.SolidColorBrush]::new([Windows.Media.ColorConverter]::ConvertFromString($Fill))
}
function Render-Icon([int]$Size) {
    $visual = [Windows.Media.DrawingVisual]::new()
    $drawing = $visual.RenderOpen()
    $drawing.PushTransform([Windows.Media.ScaleTransform]::new($Size / 256.0, $Size / 256.0))
    foreach ($path in $svg.svg.path) {
        $pen = $null
        if ($path.HasAttribute('stroke')) {
            $pen = [Windows.Media.Pen]::new((Get-Brush $path.stroke), [double]::Parse($path.GetAttribute('stroke-width'), $invariant))
        }
        $drawing.DrawGeometry((Get-Brush $path.fill), $pen, [Windows.Media.Geometry]::Parse($path.d))
    }
    $drawing.Close()
    $bitmap = [Windows.Media.Imaging.RenderTargetBitmap]::new($Size, $Size, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $bitmap.Freeze()
    return $bitmap
}
function Encode-Png($Bitmap) {
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($Bitmap))
    $stream = [IO.MemoryStream]::new()
    try { $encoder.Save($stream); return ,$stream.ToArray() }
    finally { $stream.Dispose() }
}
$frames = foreach ($size in @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)) {
    $large = Render-Icon ($size * 4)
    $visual = [Windows.Media.DrawingVisual]::new()
    [Windows.Media.RenderOptions]::SetBitmapScalingMode($visual, [Windows.Media.BitmapScalingMode]::HighQuality)
    $drawing = $visual.RenderOpen()
    $drawing.DrawImage($large, [Windows.Rect]::new(0, 0, $size, $size))
    $drawing.Close()
    $small = [Windows.Media.Imaging.RenderTargetBitmap]::new($size, $size, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
    $small.Render($visual)
    [pscustomobject]@{ Size=$size; Bytes=(Encode-Png $small) }
}
$stream = [IO.File]::Create($target)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frame.Bytes.Length); $writer.Write([uint32]$offset)
        $offset += $frame.Bytes.Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
}
finally { $writer.Dispose() }
Copy-Item -LiteralPath $target -Destination (Join-Path $root 'src\common\wsapSmall.ico') -Force
$artifacts = Join-Path $root 'build\icons'
[IO.Directory]::CreateDirectory($artifacts) | Out-Null
[IO.File]::WriteAllBytes((Join-Path $artifacts 'winsap-icon-256.png'), $frames[-1].Bytes)
Write-Output "Generated WinSap application and small icons from the editable SVG (16-256 pixels)."
