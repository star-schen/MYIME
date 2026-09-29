# Asset authoring only; not part of build/test. Renders this project's simple
# rect/polygon SVG subset without external tools or installed fonts.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$assetDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'windows/host/assets'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number($text) { [single]::Parse($text, $culture) }
function Color($name, $opacity) {
    $alpha = if ($opacity) { [int](255 * (Number $opacity)) } else { 255 }
    [Drawing.Color]::FromArgb($alpha, [Drawing.Color]::FromName($name))
}
foreach ($name in @('brand','mode-zh','mode-en')) {
    [xml]$svg = Get-Content -LiteralPath (Join-Path $assetDirectory "$name.svg") -Raw
    $images = [Collections.Generic.List[byte[]]]::new()
    $sizes = @(16,20,24,32,40,48)
    foreach ($size in $sizes) {
        $large = [Drawing.Bitmap]::new($size*4,$size*4,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($large)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.ScaleTransform(($size*4/32),($size*4/32))
            foreach ($node in $svg.DocumentElement.ChildNodes) {
                if ($node.LocalName -eq 'title') { continue }
                $path = [Drawing.Drawing2D.GraphicsPath]::new()
                try {
                    if ($node.LocalName -eq 'rect') {
                        $path.AddRectangle([Drawing.RectangleF]::new((Number $node.x),(Number $node.y),(Number $node.width),(Number $node.height)))
                    } elseif ($node.LocalName -eq 'polygon') {
                        [Drawing.PointF[]]$points = foreach ($pair in ($node.points -split '\s+')) {
                            $xy = $pair -split ','
                            [Drawing.PointF]::new((Number $xy[0]),(Number $xy[1]))
                        }
                        $path.AddPolygon($points)
                    } else { throw "Unsupported SVG element: $($node.LocalName)" }
                    $fill = $node.GetAttribute('fill')
                    if ($fill -and $fill -ne 'none') {
                        $brush = [Drawing.SolidBrush]::new((Color $fill ($node.GetAttribute('fill-opacity'))))
                        try { $graphics.FillPath($brush,$path) } finally { $brush.Dispose() }
                    }
                    $stroke = $node.GetAttribute('stroke')
                    if ($stroke -and $stroke -ne 'none') {
                        $pen = [Drawing.Pen]::new((Color $stroke ($node.GetAttribute('stroke-opacity'))),(Number ($node.GetAttribute('stroke-width'))))
                        $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
                        try { $graphics.DrawPath($pen,$path) } finally { $pen.Dispose() }
                    }
                } finally { $path.Dispose() }
            }
            $small = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $scaled = [Drawing.Graphics]::FromImage($small)
            $stream = [IO.MemoryStream]::new()
            try {
                $scaled.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $scaled.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $scaled.DrawImage($large,[Drawing.Rectangle]::new(0,0,$size,$size),0,0,$large.Width,$large.Height,[Drawing.GraphicsUnit]::Pixel)
                $small.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
                $images.Add($stream.ToArray())
            } finally { $stream.Dispose(); $scaled.Dispose(); $small.Dispose() }
        } finally { $graphics.Dispose(); $large.Dispose() }
    }
    $file = [IO.File]::Create((Join-Path $assetDirectory "$name.ico"))
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        [uint32]$offset = 6 + 16*$sizes.Count
        for ($index=0; $index -lt $sizes.Count; $index++) {
            $writer.Write([byte]$sizes[$index]); $writer.Write([byte]$sizes[$index])
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$index].Length); $writer.Write($offset)
            $offset += $images[$index].Length
        }
        foreach ($image in $images) { $writer.Write([byte[]]$image) }
    } finally { $writer.Dispose(); $file.Dispose() }
}
