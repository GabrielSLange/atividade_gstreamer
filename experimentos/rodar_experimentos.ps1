# Roda a matriz de experimentos (latencia do jitter buffer x condicao de rede simulada)
# e grava experimentos\resultados.csv.
#
# Uso (a partir da raiz do projeto, depois de rodar build.bat):
#   powershell -ExecutionPolicy Bypass -File experimentos\rodar_experimentos.ps1
#   (teste rapido: ... -Duracao 4 -Latencias 200)
#
# Como funciona: gera uma COPIA do receptor.c em experimentos\tmp (o src\receptor.c nao e alterado)
# em que a saida de audio vira um arquivo .raw e o programa termina sozinho. Para cada combinacao,
# sobe essa copia + o transmissor real, le a ultima linha [STATS], conta os eventos [PERDA] e
# analisa o PCM gravado: como o sinal e um seno de 440 Hz, cada amostra deve seguir a recorrencia
# x[n] = 2*cos(w)*x[n-1] - x[n-2]; um desvio grande indica uma descontinuidade (um "clique").
param(
  [int]$Duracao = 10,
  [int[]]$Latencias = @(20, 50, 200, 1000),
  [string]$GstDir = "C:\Program Files\gstreamer\1.0\mingw_x86_64",
  [string]$MingwDir = "C:\mingw64\mingw64\bin"
)
$ErrorActionPreference = "Stop"

$raiz = Split-Path $PSScriptRoot -Parent
$tmp = Join-Path $PSScriptRoot "tmp"
New-Item -ItemType Directory -Force $tmp | Out-Null
$env:PATH = "$GstDir\bin;$MingwDir;" + $env:PATH

$tx = Join-Path $raiz "bin\transmissor.exe"
if (-not (Test-Path $tx)) { throw "bin\transmissor.exe nao existe. Rode build.bat antes." }

# ---- 1. Gera e compila a copia do receptor usada nos experimentos ----------------------------
function Troca($texto, $de, $para) {
  if (-not $texto.Contains($de)) { throw "Trecho nao encontrado em src\receptor.c: $de" }
  return $texto.Replace($de, $para)
}
$cod = (Get-Content (Join-Path $raiz "src\receptor.c") -Raw -Encoding UTF8) -replace "`r`n", "`n"

# a) em vez de tocar, grava o PCM no arquivo dado em argv[5]
$cod = Troca $cod '"autoaudiosink", "saida"' '"filesink", "saida"'
$cod = Troca $cod '  /* 3. Adiciona' "  g_object_set(saida, `"location`", argv[5], NULL);`n  /* 3. Adiciona"

# b) termina sozinho depois de argv[6] segundos (fechando o arquivo corretamente)
$laco = @'
  while (!msg) {
    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (!msg)
      imprime_estatisticas(jitter);
  }
'@ -replace "`r`n", "`n"
$laco_novo = @'
  for (int s = 0; !msg && s < atoi(argv[6]); s++) {
    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (!msg)
      imprime_estatisticas(jitter);
  }
  if (!msg)
    msg = gst_message_new_eos(GST_OBJECT(pipeline));
'@ -replace "`r`n", "`n"
$cod = Troca $cod $laco $laco_novo

$fonte = Join-Path $tmp "receptor_experimento.c"
[System.IO.File]::WriteAllText($fonte, $cod, (New-Object System.Text.UTF8Encoding $false))
$exe = Join-Path $tmp "receptor_experimento.exe"
gcc $fonte '-Wall' "-I$GstDir\include\gstreamer-1.0" "-I$GstDir\include\glib-2.0" "-I$GstDir\lib\glib-2.0\include" "-L$GstDir\lib" '-lgstreamer-1.0' '-lgobject-2.0' '-lglib-2.0' -o $exe
if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar o receptor de experimento." }

# ---- 2. Analisador do PCM (S16BE estereo, canal esquerdo) ---------------------------------------
if (-not ("AnalisaPcm" -as [type])) {
  Add-Type -TypeDefinition @"
public static class AnalisaPcm {
  public static double[] Rodar(string caminho) {
    byte[] b = System.IO.File.ReadAllBytes(caminho);
    int n = b.Length / 4;
    double c = 2 * System.Math.Cos(2 * System.Math.PI * 440.0 / 44100.0);
    int descontinuidades = 0; bool anterior = false; int x1 = 0, x2 = 0;
    for (int i = 0; i < n; i++) {
      int x = (short)((b[4 * i] << 8) | b[4 * i + 1]);
      if (i >= 2) {
        bool salto = System.Math.Abs(x - (c * x1 - x2)) > 20;
        if (salto && !anterior) descontinuidades++;
        anterior = salto;
      }
      x2 = x1; x1 = x;
    }
    return new double[] { n / 44100.0, descontinuidades };
  }
}
"@
}

# ---- 3. Matriz de experimentos ------------------------------------------------------------------
$cenarios = @(
  @{ nome = "rede limpa";            perda = 0; atraso = 0  },
  @{ nome = "perda 5%";              perda = 5; atraso = 0  },
  @{ nome = "jitter ate 80 ms";      perda = 0; atraso = 80 },
  @{ nome = "perda 5% + jitter 80 ms"; perda = 5; atraso = 80 }
)
$saida = Join-Path $tmp "saida.raw"
$out = Join-Path $tmp "rx.out"
$err = Join-Path $tmp "rx.err"
$linhas = @()

foreach ($c in $cenarios) {
  foreach ($lat in $Latencias) {
    Remove-Item $saida -ErrorAction SilentlyContinue
    $argsRx = @("5000", "$lat", "$($c.perda)", "$($c.atraso)", "`"$saida`"", "$($Duracao + 3)")
    $rx = Start-Process $exe -ArgumentList $argsRx -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 700
    $t = Start-Process $tx -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $tmp "tx.out")
    Start-Sleep -Seconds $Duracao
    Stop-Process -Id $t.Id -Force
    if (-not $rx.WaitForExit(($Duracao + 15) * 1000)) {
      Stop-Process -Id $rx.Id -Force
      throw "O receptor nao terminou no tempo esperado."
    }

    $txt = Get-Content $out
    $ultima = $txt | Where-Object { $_ -like "[[]STATS]*" } | Select-Object -Last 1
    if ($ultima -match "entregues=(\d+) perdidos=(\d+) atrasados=(\d+) duplicados=(\d+)") {
      $m = $Matches
    } else {
      throw "Nenhuma linha [STATS] na saida do receptor. Veja $out e $err"
    }
    $eventos = @($txt | Where-Object { $_ -like "[[]PERDA]*" }).Count
    $pcm = [AnalisaPcm]::Rodar($saida)

    $linha = [pscustomobject]@{
      cenario          = $c.nome
      latencia_ms      = $lat
      perda_pct        = $c.perda
      atraso_max_ms    = $c.atraso
      entregues        = [int]$m[1]
      perdidos         = [int]$m[2]
      atrasados        = [int]$m[3]
      duplicados       = [int]$m[4]
      eventos_perda    = $eventos
      audio_s          = [math]::Round($pcm[0], 2)
      descontinuidades = [int]$pcm[1]
    }
    $linhas += $linha
    Write-Host ("{0,-24} lat={1,-5} entregues={2,-5} perdidos={3,-4} atrasados={4,-4} eventos={5,-4} audio={6}s cliques={7}" -f `
      $c.nome, $lat, $linha.entregues, $linha.perdidos, $linha.atrasados, $eventos, $linha.audio_s, $linha.descontinuidades)
  }
}

Remove-Item $saida -ErrorAction SilentlyContinue
$csv = Join-Path $PSScriptRoot "resultados.csv"
$linhas | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
Write-Host "`nResultados gravados em $csv"
