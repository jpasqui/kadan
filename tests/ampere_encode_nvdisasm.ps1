param([string]$bin, [string]$out)
$nv = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin\nvdisasm.exe'
$bytes = [IO.File]::ReadAllBytes($bin)
$tmp = Join-Path $env:TEMP 's2one.bin'
$lines = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
  [IO.File]::WriteAllBytes($tmp, $bytes[$i..($i+15)])
  $hex = ($bytes[$i..($i+15)] | ForEach-Object { $_.ToString('x2') }) -join ''
  $text = & $nv -b SM86 $tmp 2>&1 | Out-String
  $m = [regex]::Match($text, '/\*0000\*/\s+(.*?);')
  if ($m.Success) { $lines.Add("$hex`t$($m.Groups[1].Value.Trim())") } else { $lines.Add("$hex`tILLEGAL " + ($text -replace '\s+',' ').Trim()) }
}
[IO.File]::WriteAllLines($out, $lines)
