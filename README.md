# Questionário de Linguagem C — interfaces de terminal

Três programas em C para Windows, com interface de terminal moderna feita com `conio.h` e `windows.h`:
cores de 24 bits, mouse (clique, passagem e rolagem), ícones, layout responsivo e teclado completo.

| Questão | Arquivo | O que faz |
|---|---|---|
| 01 | `questao01_matrizes.c` | Matrizes A e B até 5×5: soma, multiplicação elemento a elemento e produto matricial, com resultado interativo que mostra cada conta |
| 02 | `questao02_calculadora.c` | Calculadora com teclado clicável, operações sucessivas, histórico e tratamento de divisão por zero |
| 03 | `questao03_lanchonete.c` | Sistema de lanchonete: cardápio por categoria, carrinho, pagamento (10% de desconto no dinheiro), cupom e histórico de vendas salvos em arquivo |

## Como compilar e executar (PowerShell)

```powershell
gcc questao01_matrizes.c -o matrizes.exe;      .\matrizes.exe
gcc questao02_calculadora.c -o questao02.exe;  .\questao02.exe
gcc questao03_lanchonete.c -o questao03.exe;   .\questao03.exe
```

Requisitos: Windows 10 ou 11, GCC/MinGW (ex.: w64devkit) e, de preferência, o Windows Terminal
(janela de pelo menos 78×24). Cada arquivo é independente e contém o mesmo módulo de interface.
