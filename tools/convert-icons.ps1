# Konwersja arkuszy symboli z https://github.com/hessu/aprs-symbols (png/aprs-symbols-32-0@2x.png
# i -32-1@2x.png, zapisane jako sheet0.png / sheet1.png) na 1-bitowe ikony 32x32 do icons_aprs.h.
# Użycie: powershell -File convert-icons.ps1 -InDir <folder z PNG> -OutHeader icons_aprs.h -OutPreview podglad.png
param(
  [string]$InDir,
  [string]$OutHeader,
  [string]$OutPreview,
  [double]$LumThreshold = 0.80,
  [double]$AlphaThreshold = 0.45,
  [double]$DarkThreshold = 0.30
)
Add-Type -AssemblyName System.Drawing

# Symbole-logo firm - zastępujemy ogólną ikoną z literą (rysowaną w programie)
$skip = @{ 0 = @('M', 'Z'); 1 = @('K') }

function Convert-Sheet($path, $tableIdx) {
  $bmp = New-Object System.Drawing.Bitmap $path
  $cell = [int]($bmp.Width / 16)
  $icons = @()
  for ($idx = 0; $idx -lt 94; $idx++) {
    $code = [char](33 + $idx)
    $col = $idx % 16; $row = [math]::Floor($idx / 16)
    $bits = New-Object byte[] 128
    $empty = $true
    if (-not ($skip[$tableIdx] -contains [string]$code)) {
      for ($y = 0; $y -lt 32; $y++) {
        for ($x = 0; $x -lt 32; $x++) {
          # Średnia z bloku (cell/32) x (cell/32) pikseli źródła
          $step = $cell / 32
          $a = 0.0; $l = 0.0; $n = 0
          for ($sy = 0; $sy -lt $step; $sy++) {
            for ($sx = 0; $sx -lt $step; $sx++) {
              $c = $bmp.GetPixel($col * $cell + $x * $step + $sx, $row * $cell + $y * $step + $sy)
              $al = $c.A / 255.0
              $a += $al
              $l += $al * (0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B) / 255.0
              $n++
            }
          }
          $a /= $n
          $lum = if ($a -gt 0) { $l / $n / $a } else { 1 }
          # Trzy poziomy: ciemne kontury = czarne, kolorowe wypełnienia = szachownica 50%
          # (na e-papierze wygląda jak szarość i zostawia widoczne kontury), jasne = białe
          $ink = $false
          if ($a -ge $AlphaThreshold) {
            if ($lum -lt $DarkThreshold) { $ink = $true }
            elseif ($lum -lt $LumThreshold) { $ink = ((($x + $y) % 2) -eq 0) }
          }
          if ($ink) {
            $bits[$y * 4 + [math]::Floor($x / 8)] = $bits[$y * 4 + [math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8))
            $empty = $false
          }
        }
      }
    }
    $icons += , @{ code = $code; bits = $bits; empty = $empty }
  }
  $bmp.Dispose()
  return , $icons
}

$tables = @((Convert-Sheet (Join-Path $InDir 'sheet0.png') 0), (Convert-Sheet (Join-Path $InDir 'sheet1.png') 1))

# --- Nagłówek C ---
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// Symbole APRS 32x32 (1 bit), wygenerowane z zestawu aprs.fi:')
[void]$sb.AppendLine('// https://github.com/hessu/aprs-symbols (Heikki Hannikainen OH7LZB i inni autorzy,')
[void]$sb.AppendLine('// licencje poszczególnych symboli: COPYRIGHT.md w tym repozytorium).')
[void]$sb.AppendLine('// Logo firm (/M, /Z, \K) pominięte - program rysuje w ich miejscu ikonę z literą.')
[void]$sb.AppendLine('// Wygenerowano skryptem tools/convert-icons.ps1.')
[void]$sb.AppendLine('// Indeks = kod symbolu - 33 (od "!" do "~"). Puste ikony mają same zera.')
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('#include <Arduino.h>')
[void]$sb.AppendLine('')
$names = @('aprs_symbols_primary', 'aprs_symbols_alternate')
for ($t = 0; $t -lt 2; $t++) {
  [void]$sb.AppendLine("const uint8_t $($names[$t])[94][128] PROGMEM = {")
  foreach ($ic in $tables[$t]) {
    $label = if ($t -eq 0) { "/$($ic.code)" } else { "\$($ic.code)" }
    $hex = ($ic.bits | ForEach-Object { '0x{0:X2}' -f $_ }) -join ','
    # Kod szesnastkowo na końcu: komentarz "// /\" kończący się backslashem skleiłby
    # następną linię z komentarzem i przesunął wszystkie dalsze ikony o jedną pozycję
    $codeHex = '0x{0:X2}' -f [int][char]$ic.code
    [void]$sb.AppendLine("  {$hex}, // $label ($codeHex)")
  }
  [void]$sb.AppendLine('};')
  [void]$sb.AppendLine('')
}
$text = $sb.ToString()
# Kontrola: żadna linia nie może kończyć się backslashem (kontynuacja linii w C++)
$bad = $text -split "`r?`n" | Where-Object { $_.EndsWith('\') }
if ($bad) { throw "Linia konczy sie backslashem: $($bad[0])" }
$entries = ([regex]::Matches($text, '(?m)^  \{0x')).Count
if ($entries -ne 188) { throw "Oczekiwano 188 ikon, jest $entries" }
[System.IO.File]::WriteAllText($OutHeader, $text, (New-Object System.Text.UTF8Encoding $false))

# --- Podgląd: 2 tabele, 16 kolumn, skala x2 ---
$scale = 2; $pad = 4; $cellOut = 32 * $scale + $pad
$prev = New-Object System.Drawing.Bitmap (16 * $cellOut), (12 * $cellOut + $pad)
$g = [System.Drawing.Graphics]::FromImage($prev)
$g.Clear([System.Drawing.Color]::White)
for ($t = 0; $t -lt 2; $t++) {
  for ($idx = 0; $idx -lt 94; $idx++) {
    $ic = $tables[$t][$idx]
    $ox = ($idx % 16) * $cellOut; $oy = ($t * 6 + [math]::Floor($idx / 16)) * $cellOut + $t * $pad
    $g.DrawRectangle([System.Drawing.Pens]::LightGray, $ox, $oy, 32 * $scale + 1, 32 * $scale + 1)
    for ($y = 0; $y -lt 32; $y++) { for ($x = 0; $x -lt 32; $x++) {
      if ($ic.bits[$y * 4 + [math]::Floor($x / 8)] -band (0x80 -shr ($x % 8))) {
        $g.FillRectangle([System.Drawing.Brushes]::Black, $ox + 1 + $x * $scale, $oy + 1 + $y * $scale, $scale, $scale)
      }
    } }
  }
}
$g.Dispose()
$prev.Save($OutPreview, [System.Drawing.Imaging.ImageFormat]::Png)
$prev.Dispose()
"OK: " + (($tables[0] + $tables[1]) | Where-Object { $_.empty }).Count + " pustych"
