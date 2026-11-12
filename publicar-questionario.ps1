# =====================================================================
#  publicar-questionario.ps1 · Envia a pasta "Questionario C" para o GitHub
#  Rode nesta pasta:
#     powershell -ExecutionPolicy Bypass -File .\publicar-questionario.ps1
# =====================================================================
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Set-Location -LiteralPath $PSScriptRoot
$User = 'AndreFecury'
$Repo = 'questionario-c'
$Url  = "https://github.com/$User/$Repo.git"

Write-Host "`nAutor dos commits: $(git config --global user.name) <$(git config --global user.email)>" -ForegroundColor Cyan

if (-not (Test-Path .git)) { git init -b main | Out-Null; Write-Host '  ✔ repositório criado' -ForegroundColor Green }

git add .gitignore README.md publicar-questionario.ps1 questao01_matrizes.c questao02_calculadora.c questao03_lanchonete.c
$Msg = @"
feat: questionário de C com interface de terminal moderna

- Questão 01: matrizes até 5x5 (soma, elemento a elemento, produto)
- Questão 02: calculadora com teclado clicável e histórico
- Questão 03: lanchonete com cardápio, carrinho, pagamento e cupom
- Interface com cores 24 bits, mouse, ícones e layout responsivo

"@
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) { git commit -m $Msg | Out-Null; Write-Host '  ✔ commit criado' -ForegroundColor Green }
else { Write-Host '  ! nada novo para commitar' -ForegroundColor Yellow }

if (-not (git remote)) {
  if (Get-Command gh -ErrorAction SilentlyContinue) {
    gh repo create "$User/$Repo" --public --source . --remote origin
  } else {
    Write-Host "`nCrie um repositório VAZIO em https://github.com/new" -ForegroundColor Yellow
    Write-Host "  dono: $User   nome: $Repo   (sem README, sem .gitignore)" -ForegroundColor Yellow
    Read-Host '  Depois de criar, tecle Enter para continuar'
    git remote add origin $Url
  }
}

git push -u origin main
if ($LASTEXITCODE -eq 0) { Write-Host "`n  ✔ Publicado em https://github.com/$User/$Repo" -ForegroundColor Green }
else { Write-Host "`n  ! O envio falhou. Veja a mensagem acima." -ForegroundColor Red }
