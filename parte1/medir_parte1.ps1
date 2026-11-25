# Parte 1 da atividade final: mede FPS (fpsdisplaysink) e CPU de cada pipeline do PDF e grava parte1\resultados.csv.
#
# Uso (da raiz do projeto):  powershell -ExecutionPolicy Bypass -File parte1\medir_parte1.ps1
#   -Medir 10     segundos de medicao (depois de -Aquece segundos de aquecimento)
#   -Filtro L1,U2 roda so os experimentos com esses ids (teste rapido)
#
# Como mede:
#  - FPS: ultima linha "rendered/dropped/current/average" que o fpsdisplaysink imprime com gst-launch -v.
#  - CPU: tempo de processador do(s) processo(s) gst-launch na janela de medicao, dividido pelo tempo real.
#    Aparece em % de UM nucleo (100 = um nucleo inteiro) e em % do total da maquina (como o Gerenciador de Tarefas).
#  - Tempo ate a primeira imagem: de quando o processo que entrou por ultimo iniciou ate o primeiro quadro
#    renderizado (resolucao de ~0,5 s, que e o intervalo de atualizacao do fpsdisplaysink).
# As janelas de video abrem e fecham sozinhas. Nada e estimado: o que nao for medido fica vazio.
param(
  [int]$Medir = 10,
  [int]$Aquece = 2,
  [string[]]$Filtro = @()
)
$ErrorActionPreference = "Stop"
$Filtro = @($Filtro | ForEach-Object { $_ -split ',' } | Where-Object { $_ })   # -File entrega "L1,H1" como um texto so
$tmp = Join-Path $PSScriptRoot "tmp"
New-Item -ItemType Directory -Force $tmp | Out-Null
$env:PATH = "C:\Program Files\gstreamer\1.0\mingw_x86_64\bin;" + $env:PATH
$nucleos = [Environment]::ProcessorCount
$q = '"'
$inv = [Globalization.CultureInfo]::InvariantCulture

function Ler($f) {
  try {
    $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs)
    $t = $sr.ReadToEnd(); $sr.Close(); return $t
  } catch { return "" }
}
function UltimoFps($txt) {
  $m = [regex]::Matches($txt, 'rendered: (\d+), dropped: (\d+), current: ([\d.,]+), average: ([\d.,]+)')
  if ($m.Count -eq 0) { return $null }
  $u = $m[$m.Count - 1]
  return @{
    rend  = [int]$u.Groups[1].Value
    desc  = [int]$u.Groups[2].Value
    atual = [double]::Parse(($u.Groups[3].Value -replace ',', '.'), $inv)
    media = [double]::Parse(($u.Groups[4].Value -replace ',', '.'), $inv)
  }
}
function Cpu($p) { $p.Refresh(); return $p.TotalProcessorTime.TotalSeconds }
function Gst($argumentos, $saida) {
  Remove-Item $saida -ErrorAction SilentlyContinue
  return Start-Process gst-launch-1.0.exe -ArgumentList "-v $argumentos" -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $saida -RedirectStandardError ($saida + ".err")
}
function Fim($ps) { foreach ($p in $ps) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } } }

$resultados = @()
function Registra($o) {
  $script:resultados += [pscustomobject]$o
  Write-Host ("{0,-4} {1,-58} fps={2,-6} desc={3,-4} cpu(nucleo)={4,-6} 1a imagem={5}" -f `
    $o.id, $o.experimento, $o.fps_medio, $o.descartados, $o.cpu_a_nucleo_pct, $o.primeira_imagem_s)
}
function Quer($id) { return ($Filtro.Count -eq 0) -or ($Filtro -contains $id) }

# ---------------- 1.1 e 1.2: execucao local (com e sem H.264) ----------------
function Local($id, $secao, $nome, $w, $h, $fps, $enc) {
  if (-not (Quer $id)) { return }
  $caps = "${q}video/x-raw,width=$w,height=$h,framerate=$fps/1${q}"
  $cad = "videotestsrc is-live=true pattern=ball ! $caps ! videoconvert"
  if ($enc) { $cad += " ! x264enc tune=zerolatency speed-preset=$($enc.preset) bitrate=$($enc.bitrate) ! h264parse ! avdec_h264 ! videoconvert" }
  $cad += " ! fpsdisplaysink text-overlay=true"
  $saida = Join-Path $tmp "local.out"
  $p = Gst $cad $saida
  Start-Sleep -Seconds $Aquece
  $r = [ordered]@{ id = $id; secao = $secao; experimento = $nome; largura = $w; altura = $h; fps_config = $fps
                   bitrate = $(if ($enc) { $enc.bitrate } else { "" }); preset = $(if ($enc) { $enc.preset } else { "" })
                   cenario = "local"; fps_medio = ""; renderizados = ""; descartados = ""; primeira_imagem_s = ""
                   cpu_a_nucleo_pct = ""; cpu_a_total_pct = ""; cpu_b_nucleo_pct = ""; cpu_b_total_pct = ""; obs = "" }
  if ($p.HasExited) { $r.obs = "pipeline encerrou sozinha: " + (Ler ($saida + ".err")).Trim(); Registra $r; return }
  $c0 = Cpu $p; $sw = [Diagnostics.Stopwatch]::StartNew()
  Start-Sleep -Seconds $Medir
  $c1 = Cpu $p; $t = $sw.Elapsed.TotalSeconds
  $f = UltimoFps (Ler $saida)
  Fim @($p)
  $nuc = ($c1 - $c0) / $t * 100
  $r.cpu_a_nucleo_pct = [Math]::Round($nuc, 1); $r.cpu_a_total_pct = [Math]::Round($nuc / $nucleos, 1)
  if ($f) { $r.fps_medio = $f.media; $r.renderizados = $f.rend; $r.descartados = $f.desc } else { $r.obs = "fpsdisplaysink nao imprimiu medidas" }
  Registra $r
}

# ---------------- 1.3: MPEG-TS sobre UDP em 127.0.0.1 ----------------
# $m: tune, key, conf, align, sync (false = udpsink sync=false). $tardio: receptor entra 3 s depois do transmissor.
function Udp($id, $nome, $porta, $m, $tardio) {
  if (-not (Quer $id)) { return }
  $caps = "${q}video/x-raw,width=640,height=360,framerate=25/1${q}"
  $enc = "x264enc"; if ($m.tune) { $enc += " tune=zerolatency" }; $enc += " bitrate=500 speed-preset=ultrafast"
  if ($m.key) { $enc += " key-int-max=25" }
  $par = "h264parse"; if ($m.conf) { $par += " config-interval=-1" }
  $mux = "mpegtsmux"; if ($m.align) { $mux += " alignment=7" }
  $snk = "udpsink host=127.0.0.1 port=$porta"; if ($m.sync -eq $false) { $snk += " sync=false" }
  $txArgs = "videotestsrc is-live=true pattern=ball ! $caps ! videoconvert ! $enc ! $par ! $mux ! $snk"
  $rxArgs = "udpsrc port=$porta caps=${q}video/mpegts,systemstream=true${q} ! tsdemux ! h264parse ! avdec_h264 ! videoconvert ! fpsdisplaysink text-overlay=true"
  $saidaRx = Join-Path $tmp "rx.out"; $saidaTx = Join-Path $tmp "tx.out"
  $r = [ordered]@{ id = $id; secao = "1.3"; experimento = $nome; largura = 640; altura = 360; fps_config = 25
                   bitrate = 500; preset = "ultrafast"; cenario = $(if ($tardio) { "udp, receptor entra 3 s depois" } else { "udp, receptor antes" })
                   fps_medio = ""; renderizados = ""; descartados = ""; primeira_imagem_s = ""
                   cpu_a_nucleo_pct = ""; cpu_a_total_pct = ""; cpu_b_nucleo_pct = ""; cpu_b_total_pct = ""; obs = "" }
  if ($tardio) { $tx = Gst $txArgs $saidaTx; Start-Sleep -Seconds 3; $rx = Gst $rxArgs $saidaRx }
  else         { $rx = Gst $rxArgs $saidaRx; Start-Sleep -Seconds 1; $tx = Gst $txArgs $saidaTx }
  $sw = [Diagnostics.Stopwatch]::StartNew()                     # zero = quando o ultimo processo iniciou
  $primeira = $null
  while ($sw.Elapsed.TotalSeconds -lt ($Aquece + $Medir)) {
    Start-Sleep -Milliseconds 100
    if ($null -eq $primeira -and ((Ler $saidaRx) -match 'rendered: [1-9]\d*,')) { $primeira = [Math]::Round($sw.Elapsed.TotalSeconds, 1) }
    if ($sw.Elapsed.TotalSeconds -ge $Aquece -and $null -eq $c0tx) { $c0tx = Cpu $tx; $c0rx = Cpu $rx; $t0 = $sw.Elapsed.TotalSeconds }
  }
  $c1tx = Cpu $tx; $c1rx = Cpu $rx; $t1 = $sw.Elapsed.TotalSeconds
  $f = UltimoFps (Ler $saidaRx)
  $txVivo = -not $tx.HasExited; $rxVivo = -not $rx.HasExited
  Fim @($rx, $tx)
  if (-not $txVivo -or -not $rxVivo) { $r.obs = "um dos processos encerrou sozinho. tx: " + (Ler ($saidaTx + ".err")).Trim() + " rx: " + (Ler ($saidaRx + ".err")).Trim() }
  $dt = $t1 - $t0
  $a = ($c1tx - $c0tx) / $dt * 100; $b = ($c1rx - $c0rx) / $dt * 100
  $r.cpu_a_nucleo_pct = [Math]::Round($a, 1); $r.cpu_a_total_pct = [Math]::Round($a / $nucleos, 1)
  $r.cpu_b_nucleo_pct = [Math]::Round($b, 1); $r.cpu_b_total_pct = [Math]::Round($b / $nucleos, 1)
  if ($null -ne $primeira) { $r.primeira_imagem_s = $primeira } else { $r.primeira_imagem_s = "sem imagem"; if (-not $r.obs) { $r.obs = "nenhum quadro renderizado em $($Aquece + $Medir) s" } }
  if ($f) { $r.fps_medio = $f.media; $r.renderizados = $f.rend; $r.descartados = $f.desc }
  Registra $r
}

# ---- 1.1: sem compressao ----
Local "L1" "1.1" "sem compressao 640x360 a 25 fps (base)" 640 360 25 $null
Local "L2" "1.1" "sem compressao 320x240 a 25 fps"        320 240 25 $null
Local "L3" "1.1" "sem compressao 1280x720 a 25 fps"       1280 720 25 $null
Local "L4" "1.1" "sem compressao 640x360 a 15 fps"        640 360 15 $null
Local "L5" "1.1" "sem compressao 640x360 a 30 fps"        640 360 30 $null
# ---- 1.2: com H.264 (uma variavel por vez, a partir da base) ----
$base = @{ preset = "ultrafast"; bitrate = 500 }
Local "H0" "1.2" "H.264 base: 640x360, 25 fps, 500 kbps, ultrafast" 640 360 25 $base
Local "H1" "1.2" "H.264 bitrate 200 kbps"   640 360 25 @{ preset = "ultrafast"; bitrate = 200 }
Local "H2" "1.2" "H.264 bitrate 1500 kbps"  640 360 25 @{ preset = "ultrafast"; bitrate = 1500 }
Local "H3" "1.2" "H.264 speed-preset fast"   640 360 25 @{ preset = "fast"; bitrate = 500 }
Local "H4" "1.2" "H.264 speed-preset medium" 640 360 25 @{ preset = "medium"; bitrate = 500 }
Local "H5" "1.2" "H.264 15 fps"   640 360 15 $base
Local "H6" "1.2" "H.264 30 fps"   640 360 30 $base
Local "H7" "1.2" "H.264 320x240"  320 240 25 $base
Local "H8" "1.2" "H.264 1280x720" 1280 720 25 $base
# ---- 1.3: UDP (MPEG-TS) em localhost ----
$orig = @{ tune = $true; key = $false; conf = $false; align = $false; sync = $true }
Udp "U1" "original, receptor antes"                      5301 $orig $false
Udp "U2" "original, receptor entra depois"               5302 $orig $true
Udp "U3" "so key-int-max=25"                             5303 @{ tune = $true; key = $true; conf = $false; align = $false; sync = $true } $true
Udp "U4" "so h264parse config-interval=-1"               5304 @{ tune = $true; key = $false; conf = $true; align = $false; sync = $true } $true
Udp "U5" "so mpegtsmux alignment=7"                      5305 @{ tune = $true; key = $false; conf = $false; align = $true; sync = $true } $true
Udp "U6" "so udpsink sync=false"                         5306 @{ tune = $true; key = $false; conf = $false; align = $false; sync = $false } $true
Udp "U7" "sem tune=zerolatency"                          5307 @{ tune = $false; key = $false; conf = $false; align = $false; sync = $true } $true
$mod = @{ tune = $true; key = $true; conf = $true; align = $true; sync = $false }
Udp "U8" "modificada (as 4 alteracoes), receptor antes"  5308 $mod $false
Udp "U9" "modificada (as 4 alteracoes), receptor depois" 5309 $mod $true

$csv = Join-Path $PSScriptRoot "resultados.csv"
if ($Filtro.Count -eq 0) { $resultados | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8; Write-Host "`nResultados gravados em $csv (nucleos logicos: $nucleos)" }
else { Write-Host "`n(teste rapido: CSV nao gravado)" }
