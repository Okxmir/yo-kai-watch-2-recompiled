param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sizes = 16, 24, 32, 48, 64, 128, 256
$sourceImage = [System.Drawing.Bitmap]::FromFile($Source)
$frames = [System.Collections.Generic.List[byte[]]]::new()
try {
    foreach ($size in $sizes) {
        $frame = [System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $draw = [System.Drawing.Graphics]::FromImage($frame)
        $png = [System.IO.MemoryStream]::new()
        try {
            $draw.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $draw.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $draw.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $draw.Clear([System.Drawing.Color]::Transparent)
            $limit = $size - (2 * [Math]::Max(1, [Math]::Round($size * 0.035)))
            $ratio = $limit / [Math]::Max($sourceImage.Width, $sourceImage.Height)
            $width = [int][Math]::Round($sourceImage.Width * $ratio)
            $height = [int][Math]::Round($sourceImage.Height * $ratio)
            $draw.DrawImage($sourceImage, [System.Drawing.Rectangle]::new(($size - $width) -shr 1, ($size - $height) -shr 1, $width, $height))
            $frame.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($png.ToArray())
        } finally {
            $draw.Dispose()
            $frame.Dispose()
            $png.Dispose()
        }
    }
} finally { $sourceImage.Dispose() }

$file = [System.IO.File]::Create($Destination)
$ico = [System.IO.BinaryWriter]::new($file)
try {
    $ico.Write([uint16]0)
    $ico.Write([uint16]1)
    $ico.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($n = 0; $n -lt $sizes.Count; $n++) {
        $dimension = [byte]($sizes[$n] % 256)
        $ico.Write($dimension)
        $ico.Write($dimension)
        $ico.Write([byte]0)
        $ico.Write([byte]0)
        $ico.Write([uint16]1)
        $ico.Write([uint16]32)
        $ico.Write([uint32]$frames[$n].Length)
        $ico.Write([uint32]$offset)
        $offset += $frames[$n].Length
    }
    foreach ($frame in $frames) { $ico.Write([byte[]]$frame) }
} finally { $ico.Dispose() }
