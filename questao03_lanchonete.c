/*
 * ==========================================================================
 *  QUESTÃO 03 — SISTEMA DE LANCHONETE
 * ==========================================================================
 *  Pequeno sistema de vendas com:
 *    • cardápio por categoria (sanduíches, sucos e sobremesas), com
 *      cadastro, edição e exclusão de produtos;
 *    • tela de pedido com catálogo e carrinho lado a lado, botões − e +
 *      em cada produto e subtotal em tempo real;
 *    • pagamento em dinheiro, cartão ou Pix — dinheiro tem 10% de desconto,
 *      com cálculo de troco;
 *    • confirmação do pedido e emissão de cupom;
 *    • histórico com faturamento, ticket médio e gráfico dos últimos 7 dias.
 *
 *  Uso com mouse (clique, passagem e roda) ou teclado; layout responsivo.
 *
 *  Persistência (arquivos de texto na pasta do programa):
 *    produtos.txt  ->  id;categoria;nome;preço
 *    pedidos.txt   ->  P;número;data;pagamento;subtotal;desconto;total;recebido
 *                      I;id;quantidade;preço unitário;nome   (itens do pedido)
 *  O cardápio é gravado num arquivo temporário e depois substituído, para
 *  não perder dados se algo falhar no meio. Valores monetários ficam em
 *  centavos (inteiros), sem erros de arredondamento.
 *
 *  Compilar (MinGW / w64devkit):  gcc questao03_lanchonete.c -o questao03.exe
 *  Executar:                      .\questao03.exe
 * ==========================================================================
 */
/* ==========================================================================
 *  MÓDULO DE INTERFACE (TUI) — idêntico nos três programas
 *  ------------------------------------------------------------------------
 *  • Renderização com "double buffering": tudo é desenhado numa tela em
 *    memória e só as células que mudaram são enviadas ao console — sem
 *    piscar. No Windows Terminal (e consoles com sequências VT) usa cores
 *    de 24 bits; em consoles antigos cai para as 16 cores clássicas.
 *  • Mouse: clique, passagem do cursor (hover), roda de rolagem e botão
 *    direito (voltar), lidos com ReadConsoleInputW.
 *  • Layout responsivo: a área útil acompanha o tamanho da janela
 *    (mínimo 78 × 24) e é redesenhada quando a janela muda de tamanho.
 *  • Ícones coloridos (emoji) quando executado no Windows Terminal.
 *  • Teclado via conio.h (_getwch/_kbhit) como alternativa.
 * ========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <conio.h>
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef MOUSE_MOVED
#define MOUSE_MOVED 0x0001
#endif
#ifndef DOUBLE_CLICK
#define DOUBLE_CLICK 0x0002
#endif
#ifndef MOUSE_WHEELED
#define MOUSE_WHEELED 0x0004
#endif

#ifdef __GNUC__
#  define TUI static __attribute__((unused))
#else
#  define TUI static
#endif

/* ----------------------------- Dimensões ----------------------------- */
#define MIN_L 78                  /* menor janela suportada */
#define MIN_A 24
#define MAX_L 160                 /* acima disso a interface é centralizada */
#define MAX_A 50
static int g_telaL = MIN_L, g_telaA = MIN_A;  /* tamanho atual da área útil */
#define TELA_L g_telaL
#define TELA_A g_telaA
#define LIN_STATUS (g_telaA - 2)
#define LIN_RODAPE (g_telaA - 1)

/* ------------------------------ Estilos ------------------------------
 * Estilo = cor do texto (4 bits) | cor de fundo (4 bits) << 4 | NEGRITO.
 * Fundo 0 = "transparente": usa o fundo da região (painel ou tela). */
typedef unsigned Estilo;
#define EST(fg, bg) ((Estilo)((fg) | ((bg) << 4)))
#define NEGRITO 0x100u

enum { P_BASE, P_PAINEL, P_CAMPO, P_BORDA, P_FRACO, P_TEXTO, P_FORTE, P_ACENTO,
       P_ROXO, P_OK, P_ERRO, P_AVISO, P_AZUL, P_LARANJA, P_FUNDO, P_ESCURO };

/* Paleta moderna (tema escuro) — RGB. */
static const unsigned PALETA[16] = {
    0x000000, 0x1D2133, 0x2B3250, 0x464F78, 0x7C85A8, 0xC9D1EA, 0xFFFFFF, 0x7DCFFF,
    0xBB9AF7, 0x9ECE6A, 0xF7768E, 0xE0AF68, 0x7AA2F7, 0xFF9E64, 0x14161F, 0x11131C
};
/* Equivalentes para consoles antigos (16 cores). */
static const unsigned char LEGADO[16] = { 0, 0, 8, 8, 8, 7, 15, 11, 13, 10, 12, 14, 9, 6, 0, 0 };

#define COR_TEXTO   EST(P_TEXTO, 0)
#define COR_FRACO   EST(P_FRACO, 0)
#define COR_FORTE   (EST(P_FORTE, 0) | NEGRITO)
#define COR_ACENTO  EST(P_ACENTO, 0)
#define COR_ROXO    EST(P_ROXO, 0)
#define COR_AZUL    EST(P_AZUL, 0)
#define COR_LARANJA EST(P_LARANJA, 0)
#define COR_BORDA   EST(P_BORDA, 0)
#define COR_OK      EST(P_OK, 0)
#define COR_ERRO    EST(P_ERRO, 0)
#define COR_AVISO   EST(P_AVISO, 0)
#define COR_SEL     (EST(P_ESCURO, P_AZUL) | NEGRITO)
#define COR_CAMPO   EST(P_FORTE, P_CAMPO)
#define COR_TECLA   (EST(P_TEXTO, P_BORDA) | NEGRITO)
#define COR_HOVER   (EST(P_ESCURO, P_ACENTO) | NEGRITO)

typedef enum { MSG_LIMPA, MSG_INFO, MSG_OK, MSG_AVISO, MSG_ERRO } TipoMsg;
typedef enum { AL_ESQ, AL_CENTRO, AL_DIR } Alinhamento;
typedef enum { FILTRO_TEXTO, FILTRO_INTEIRO, FILTRO_REAL } FiltroCampo;

/* Teclas e eventos especiais (acima do maior caractere Unicode). */
enum {
    TK_BS = 8, TK_TAB = 9, TK_ENTER = 13, TK_ESC = 27,
    TK_CIMA = 0x200000, TK_BAIXO, TK_ESQ, TK_DIR, TK_INICIO, TK_FIM,
    TK_PGCIMA, TK_PGBAIXO, TK_DEL,
    TK_PASSA,      /* o mouse passou para outra zona (ver g_passa)          */
    TK_REDIM,      /* a janela mudou de tamanho: redesenhe a tela           */
    TK_ZONA = 0x300000   /* códigos de zonas clicáveis começam aqui         */
};
#define MENU_REDIM (-2)   /* tMenu: a janela mudou de tamanho */

typedef struct {
    const char *icone;      /* emoji (Windows Terminal)          */
    const char *alt;        /* alternativa de 2 colunas          */
    const char *rotulo;
    const char *descricao;
    int atalho;             /* tecla extra (0 = nenhuma)         */
} ItemMenu;

/* ------------------------------ Células ------------------------------ */
typedef struct {
    unsigned cp;            /* caractere (0 = 2ª metade de um caractere largo) */
    unsigned fg, bg, base;  /* cores RGB; base = fundo da região               */
    unsigned char larg;     /* 1 normal, 2 largo (emoji), 0 continuação        */
    unsigned char negrito, lfg, lbg, lbase;   /* cores legadas                 */
} Cel;

typedef struct { int x, y, w, h, codigo; Estilo hover; } Zona;

typedef struct {                         /* região guardada sob um diálogo */
    int x, y, w, h, nZonas, zonaBase;
    Cel *buf;
} Recorte;

static Cel  g_tras[MAX_A][MAX_L], g_frente[MAX_A][MAX_L];
static HANDLE g_saida, g_entrada;
static DWORD g_modoSaida, g_modoEntrada;
static int  g_vt, g_emoji, g_mouse, g_sujo = 1;
static int  g_ox, g_oy;                  /* origem no buffer do console      */
static int  g_vx, g_vy;                  /* origem relativa à janela (VT)    */
static int  g_cursorX = -1, g_cursorY;   /* cursor visível (campos de texto) */
static int  g_redim, g_modal;
static Zona g_zonas[256];
static int  g_nZonas, g_zonaBase, g_hover = -1, g_passa, g_querPassa;
static Cel  g_hoverSalvo[MAX_L * 3];
static int  g_mx, g_my, g_duplo;
static const char *g_app = "", *g_rotulo = "", *g_icone = "", *g_iconeAlt = "";
static int  g_temStatus;
static TipoMsg g_pendTipo = MSG_LIMPA;
static char g_pendTexto[200];
static char g_rodape[256];

/* ------------------------------ UTF-8 ------------------------------ */

TUI unsigned u8Prox(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned c = s[0];
    int n, i;
    if (c < 0x80)                { n = 0; }
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { *ps += 1; return '?'; }
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *ps += i; return '?'; }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *ps += n + 1;
    return c;
}

TUI int u8Codifica(unsigned c, char *o) {
    if (c < 0x80)    { o[0] = (char)c; return 1; }
    if (c < 0x800)   { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                       o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* Colunas ocupadas por um caractere: emoji = 2, seletores invisíveis = 0. */
static int larguraCP(unsigned c) {
    if (c == 0xFE0F || c == 0x200D) return 0;
    if (c >= 0x1F300 && c <= 0x1FAFF) return 2;
    return 1;
}

TUI int u8Larg(const char *s) {
    int n = 0;
    while (*s) n += larguraCP(u8Prox(&s));
    return n;
}

TUI int u8Len(const char *s) {
    int n = 0;
    while (*s) { u8Prox(&s); n++; }
    return n;
}

/* Escolhe o emoji ou a alternativa conforme o terminal. */
TUI const char *ic(const char *emoji, const char *alt) { return g_emoji ? emoji : alt; }

/* ------------------------- Desenho em memória ------------------------- */

static unsigned misturar(unsigned a, unsigned b, double t) {
    int ra = (int)(a >> 16), ga = (int)((a >> 8) & 255), ba = (int)(a & 255);
    int rb = (int)(b >> 16), gb = (int)((b >> 8) & 255), bb = (int)(b & 255);
    return ((unsigned)(ra + (rb - ra) * t) << 16) | ((unsigned)(ga + (gb - ga) * t) << 8)
         | (unsigned)(ba + (bb - ba) * t);
}

/* Grava uma célula com cores RGB explícitas (bg = 0xFFFFFFFF usa a base). */
static void tCelRGB(int x, int y, unsigned cp, int larg, unsigned fg, unsigned bg,
                    int negrito, unsigned char lfg, int lbg) {
    Cel *c;
    if (y < 0 || y >= g_telaA || x < 0 || x >= g_telaL) return;
    if (larg == 2 && x + 1 >= g_telaL) { cp = ' '; larg = 1; }
    c = &g_tras[y][x];
    if (c->larg == 0 && x > 0) { g_tras[y][x - 1].cp = ' '; g_tras[y][x - 1].larg = 1; }
    if (c->larg == 2 && x + 1 < g_telaL) { g_tras[y][x + 1].cp = ' '; g_tras[y][x + 1].larg = 1; }
    c->cp = cp;
    c->larg = (unsigned char)larg;
    c->fg = fg;
    c->bg = bg == 0xFFFFFFFFu ? c->base : bg;
    c->negrito = (unsigned char)negrito;
    c->lfg = lfg;
    c->lbg = (unsigned char)(lbg < 0 ? c->lbase : lbg);
    if (larg == 2) {
        Cel *d = &g_tras[y][x + 1];
        unsigned base2 = d->base;
        unsigned char lbase2 = d->lbase;
        if (d->larg == 2 && x + 2 < g_telaL) { g_tras[y][x + 2].cp = ' '; g_tras[y][x + 2].larg = 1; }
        *d = *c;
        d->cp = 0;
        d->larg = 0;
        d->base = base2;
        d->lbase = lbase2;
    }
}

static void tCel(int x, int y, unsigned cp, int larg, Estilo e) {
    unsigned b = (e >> 4) & 15;
    tCelRGB(x, y, cp, larg, PALETA[e & 15], b ? PALETA[b] : 0xFFFFFFFFu,
            (e & NEGRITO) != 0, LEGADO[e & 15], b ? LEGADO[b] : -1);
}

/* Escreve texto UTF-8 até a coluna limite; devolve a coluna seguinte. */
static int tEscreveEm(int x, int y, Estilo e, const char *s, int limite) {
    while (*s) {
        unsigned c = u8Prox(&s);
        int w = larguraCP(c);
        if (!w) continue;
        if (!g_emoji && w == 2) { c = ' '; w = 1; }   /* sem emoji: não deveria ocorrer */
        if (x + w > limite) break;
        tCel(x, y, c, w, e);
        x += w;
    }
    return x;
}

TUI int tTexto(int x, int y, Estilo e, const char *fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return tEscreveEm(x, y, e, buf, g_telaL);
}

/* Texto em exatamente 'larg' colunas: corta com "…" e completa com espaços. */
TUI void tCampo(int x, int y, int larg, Estilo e, Alinhamento al, const char *fmt, ...) {
    char txt[600];
    int w, esq, i;
    va_list ap;
    if (larg <= 0) return;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof txt, fmt, ap);
    va_end(ap);
    w = u8Larg(txt);
    if (!g_emoji) { /* sem emoji cada caractere ocupa 1 coluna */
        const char *p = txt; w = 0;
        while (*p) { if (larguraCP(u8Prox(&p))) w++; }
    }
    for (i = 0; i < larg; i++) tCel(x + i, y, ' ', 1, e);
    if (w > larg) {
        int fim = tEscreveEm(x, y, e, txt, x + larg - 1);
        tCel(fim, y, 0x2026, 1, e);                       /* … */
        return;
    }
    esq = al == AL_ESQ ? 0 : al == AL_DIR ? larg - w : (larg - w) / 2;
    tEscreveEm(x + esq, y, e, txt, x + larg);
}

TUI void tRepete(int x, int y, int n, Estilo e, const char *simbolo) {
    int i;
    for (i = 0; i < n; i++) tEscreveEm(x + i, y, e, simbolo, x + i + 1);
}

TUI void tPreenche(int x, int y, int w, int h, Estilo e) {
    int i, j;
    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++) tCel(x + j, y + i, ' ', 1, e);
}

/* Define o fundo "base" de uma região (painéis). */
static void tBase(int x, int y, int w, int h, unsigned rgb, unsigned char leg) {
    int i, j;
    for (i = y; i < y + h; i++)
        for (j = x; j < x + w; j++)
            if (i >= 0 && i < g_telaA && j >= 0 && j < g_telaL) { g_tras[i][j].base = rgb; g_tras[i][j].lbase = leg; }
}

/* Texto com degradê de cor letra a letra. */
TUI void tTextoDegrade(int x, int y, const char *s, unsigned c1, unsigned c2, int negrito) {
    int n = u8Len(s), i = 0;
    while (*s) {
        unsigned c = u8Prox(&s);
        int w = larguraCP(c);
        if (!w) continue;
        tCelRGB(x, y, c, w, misturar(c1, c2, n > 1 ? (double)i / (n - 1) : 0), 0xFFFFFFFFu,
                negrito, 11, -1);
        x += w;
        i++;
    }
}

/* ------------------------------ Zonas ------------------------------ */

static void hoverDesfazer(void) {
    int i, j, k = 0;
    Zona *z;
    if (g_hover < 0) return;
    z = &g_zonas[g_hover];
    if (z->hover && z->w * z->h <= MAX_L * 3)
        for (i = 0; i < z->h; i++)
            for (j = 0; j < z->w; j++)
                if (z->y + i < g_telaA && z->x + j < g_telaL) g_tras[z->y + i][z->x + j] = g_hoverSalvo[k++];
    g_hover = -1;
}

static void hoverAplicar(int idx) {
    int i, j, k = 0;
    Zona *z;
    hoverDesfazer();
    if (idx < 0) return;
    g_hover = idx;
    z = &g_zonas[idx];
    if (!z->hover || z->w * z->h > MAX_L * 3) return;
    for (i = 0; i < z->h; i++)
        for (j = 0; j < z->w; j++) {
            Cel *c;
            if (z->y + i >= g_telaA || z->x + j >= g_telaL) continue;
            c = &g_tras[z->y + i][z->x + j];
            g_hoverSalvo[k++] = *c;
            c->fg = PALETA[z->hover & 15];
            c->bg = PALETA[(z->hover >> 4) & 15];
            c->negrito = (z->hover & NEGRITO) != 0;
            c->lfg = LEGADO[z->hover & 15];
            c->lbg = LEGADO[(z->hover >> 4) & 15];
        }
}

/* Remove as zonas contidas num retângulo (ex.: ao redesenhar um painel). */
TUI void tZonasRemover(int x, int y, int w, int h) {
    int i, k = g_zonaBase;
    hoverDesfazer();
    for (i = g_zonaBase; i < g_nZonas; i++) {
        Zona *z = &g_zonas[i];
        if (z->x >= x && z->y >= y && z->x + z->w <= x + w && z->y + z->h <= y + h) continue;
        g_zonas[k++] = *z;
    }
    g_nZonas = k;
}

/* Registra uma área clicável. Clicar nela devolve 'codigo' em tTecla();
 * 'hover' (opcional) é o estilo aplicado quando o mouse passa por cima. */
TUI void tZona(int x, int y, int w, int h, int codigo, Estilo hover) {
    int i;
    for (i = g_zonaBase; i < g_nZonas; i++) {
        Zona *z = &g_zonas[i];
        if (z->x == x && z->y == y && z->w == w && z->h == h) {
            int estava = g_hover == i;
            if (estava) { g_hover = -1; }          /* conteúdo foi redesenhado */
            z->codigo = codigo;
            z->hover = hover;
            if (estava) hoverAplicar(i);
            return;
        }
        if (g_hover == i && x < z->x + z->w && z->x < x + w && y < z->y + z->h && z->y < y + h)
            g_hover = -1;                          /* sobreposição: descarta realce */
    }
    if (g_nZonas >= (int)(sizeof g_zonas / sizeof g_zonas[0])) return;
    g_zonas[g_nZonas].x = x; g_zonas[g_nZonas].y = y;
    g_zonas[g_nZonas].w = w; g_zonas[g_nZonas].h = h;
    g_zonas[g_nZonas].codigo = codigo;
    g_zonas[g_nZonas].hover = hover;
    g_nZonas++;
}

static int zonaEm(int x, int y) {
    int i;
    for (i = g_nZonas - 1; i >= g_zonaBase; i--) {
        Zona *z = &g_zonas[i];
        if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h) return i;
    }
    return -1;
}

/* ------------------------- Envio ao console ------------------------- */

static wchar_t g_out[160000];
static int g_nOut;

static void saiFlush(void) {
    DWORD k;
    if (g_nOut) WriteConsoleW(g_saida, g_out, (DWORD)g_nOut, &k, NULL);
    g_nOut = 0;
}
static void saiA(const char *s) {               /* ASCII */
    while (*s) { if (g_nOut > 159990) saiFlush(); g_out[g_nOut++] = (wchar_t)(unsigned char)*s++; }
}
static void saiCP(unsigned c) {
    if (g_nOut > 159990) saiFlush();
    if (c >= 0x10000) {
        c -= 0x10000;
        g_out[g_nOut++] = (wchar_t)(0xD800 + (c >> 10));
        g_out[g_nOut++] = (wchar_t)(0xDC00 + (c & 0x3FF));
    } else {
        g_out[g_nOut++] = (wchar_t)c;
    }
}

static int celIgual(const Cel *a, const Cel *b) {
    return a->cp == b->cp && a->fg == b->fg && a->bg == b->bg && a->negrito == b->negrito && a->larg == b->larg;
}

/* Envia ao console apenas o que mudou desde a última vez. */
TUI void tPresent(void) {
    char tmp[96];
    int x, y, cx = -1, cy = -1;
    unsigned ufg = 0xFFFFFFFFu, ubg = 0xFFFFFFFFu;
    int uneg = -1;

    if (!g_vt) {                                  /* console antigo: 16 cores */
        static CHAR_INFO ci[MAX_A * MAX_L];
        COORD tam, org = { 0, 0 };
        SMALL_RECT r;
        for (y = 0; y < g_telaA; y++)
            for (x = 0; x < g_telaL; x++) {
                Cel *c = &g_tras[y][x];
                ci[y * g_telaL + x].Char.UnicodeChar = (wchar_t)(c->cp && c->cp < 0x10000 ? c->cp : ' ');
                ci[y * g_telaL + x].Attributes = (WORD)(c->lfg | (c->lbg << 4));
            }
        if (g_sujo) {
            CONSOLE_SCREEN_BUFFER_INFO i;
            if (GetConsoleScreenBufferInfo(g_saida, &i)) {
                DWORD k; COORD o; int lin;
                for (lin = i.srWindow.Top; lin <= i.srWindow.Bottom; lin++) {
                    o.X = i.srWindow.Left; o.Y = (SHORT)lin;
                    FillConsoleOutputCharacterW(g_saida, L' ', (DWORD)(i.srWindow.Right - i.srWindow.Left + 1), o, &k);
                    FillConsoleOutputAttribute(g_saida, 0, (DWORD)(i.srWindow.Right - i.srWindow.Left + 1), o, &k);
                }
            }
        }
        tam.X = (SHORT)g_telaL; tam.Y = (SHORT)g_telaA;
        r.Left = (SHORT)g_ox; r.Top = (SHORT)g_oy;
        r.Right = (SHORT)(g_ox + g_telaL - 1); r.Bottom = (SHORT)(g_oy + g_telaA - 1);
        WriteConsoleOutputW(g_saida, ci, tam, org, &r);
        if (g_cursorX >= 0) {
            CONSOLE_CURSOR_INFO cc = { 20, TRUE };
            COORD p; p.X = (SHORT)(g_ox + g_cursorX); p.Y = (SHORT)(g_oy + g_cursorY);
            SetConsoleCursorPosition(g_saida, p);
            SetConsoleCursorInfo(g_saida, &cc);
        } else {
            CONSOLE_CURSOR_INFO cc = { 20, FALSE };
            SetConsoleCursorInfo(g_saida, &cc);
        }
        g_sujo = 0;
        return;
    }

    g_nOut = 0;
    saiA("\x1b[?25l");
    if (g_sujo) {
        unsigned f = PALETA[P_FUNDO];
        snprintf(tmp, sizeof tmp, "\x1b[0m\x1b[48;2;%u;%u;%um\x1b[2J", f >> 16, (f >> 8) & 255, f & 255);
        saiA(tmp);
    }
    for (y = 0; y < g_telaA; y++)
        for (x = 0; x < g_telaL; x++) {
            Cel *b = &g_tras[y][x];
            if (b->larg == 0) continue;
            if (!g_sujo && celIgual(b, &g_frente[y][x])
                && (b->larg != 2 || celIgual(&g_tras[y][x + 1], &g_frente[y][x + 1]))) continue;
            if (cx != x || cy != y) {
                snprintf(tmp, sizeof tmp, "\x1b[%d;%dH", g_vy + y + 1, g_vx + x + 1);
                saiA(tmp);
            }
            if (b->fg != ufg || b->bg != ubg || b->negrito != uneg) {
                snprintf(tmp, sizeof tmp, "\x1b[%d;38;2;%u;%u;%u;48;2;%u;%u;%um", b->negrito ? 1 : 22,
                         b->fg >> 16, (b->fg >> 8) & 255, b->fg & 255, b->bg >> 16, (b->bg >> 8) & 255, b->bg & 255);
                saiA(tmp);
                ufg = b->fg; ubg = b->bg; uneg = b->negrito;
            }
            saiCP(b->cp);
            g_frente[y][x] = *b;
            if (b->larg == 2) { g_frente[y][x + 1] = g_tras[y][x + 1]; cx = -1; }  /* reposiciona após emoji */
            else { cx = x + 1; cy = y; }
        }
    if (g_cursorX >= 0) {
        snprintf(tmp, sizeof tmp, "\x1b[%d;%dH\x1b[?25h", g_vy + g_cursorY + 1, g_vx + g_cursorX + 1);
        saiA(tmp);
    }
    saiFlush();
    g_sujo = 0;
}

/* ----------------------------- Geometria ----------------------------- */

/* Calcula a área útil e a posição dela na janela. Retorna 0 se pequena demais. */
static int tMedir(void) {
    CONSOLE_SCREEN_BUFFER_INFO i;
    int cols, lins;
    if (!GetConsoleScreenBufferInfo(g_saida, &i)) return 1;
    cols = i.srWindow.Right - i.srWindow.Left + 1;
    lins = i.srWindow.Bottom - i.srWindow.Top + 1;
    g_telaL = cols < MAX_L ? cols : MAX_L;
    g_telaA = lins < MAX_A ? lins : MAX_A;
    g_vx = (cols - g_telaL) / 2;
    g_vy = (lins - g_telaA) / 2;
    g_ox = i.srWindow.Left + g_vx;
    g_oy = i.srWindow.Top + g_vy;
    return cols >= MIN_L && lins >= MIN_A;
}

/* ------------------------------ Teclado e mouse ------------------------------ */

static void tReposicionar(void) {           /* janela redimensionada */
    int L = g_telaL, A = g_telaA;
    tMedir();
    if (g_modal) { g_telaL = L; g_telaA = A; }       /* durante diálogos mantém o desenho */
    g_redim = 1;
    g_sujo = 1;
}

static int teclaVirtual(WORD vk) {
    switch (vk) {
        case VK_UP: return TK_CIMA;     case VK_DOWN: return TK_BAIXO;
        case VK_LEFT: return TK_ESQ;    case VK_RIGHT: return TK_DIR;
        case VK_HOME: return TK_INICIO; case VK_END: return TK_FIM;
        case VK_PRIOR: return TK_PGCIMA; case VK_NEXT: return TK_PGBAIXO;
        case VK_DELETE: return TK_DEL;  case VK_RETURN: return TK_ENTER;
        case VK_ESCAPE: return TK_ESC;  case VK_BACK: return TK_BS;
        case VK_TAB: return TK_TAB;
        default: return 0;
    }
}

/* Aguarda o próximo evento: tecla, clique numa zona, rolagem ou redimensionamento. */
TUI int tTecla(void) {
    static unsigned alto;
    for (;;) {
        INPUT_RECORD r;
        DWORD n;
        if (g_redim && !g_modal) { g_redim = 0; return TK_REDIM; }
        tPresent();
        if (!g_mouse) {                          /* sem eventos de console: conio */
            int c = _getwch();
            if (c == 0 || c == 0xE0) {
                switch (_getwch()) {
                    case 72: return TK_CIMA;   case 80: return TK_BAIXO;
                    case 75: return TK_ESQ;    case 77: return TK_DIR;
                    case 71: return TK_INICIO; case 79: return TK_FIM;
                    case 73: return TK_PGCIMA; case 81: return TK_PGBAIXO;
                    case 83: return TK_DEL;    default: continue;
                }
            }
            return c;
        }
        if (!ReadConsoleInputW(g_entrada, &r, 1, &n) || n == 0) continue;
        if (r.EventType == KEY_EVENT) {
            KEY_EVENT_RECORD *k = &r.Event.KeyEvent;
            unsigned ch = k->uChar.UnicodeChar;
            int t;
            if (!k->bKeyDown) continue;
            if ((t = teclaVirtual(k->wVirtualKeyCode)) != 0) return t;
            if (ch >= 0xD800 && ch <= 0xDBFF) { alto = ch; continue; }
            if (ch >= 0xDC00 && ch <= 0xDFFF) {
                if (!alto) continue;
                ch = 0x10000 + ((alto - 0xD800) << 10) + (ch - 0xDC00);
                alto = 0;
            }
            if (ch) return (int)ch;
        } else if (r.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD *m = &r.Event.MouseEvent;
            int x = m->dwMousePosition.X - g_ox, y = m->dwMousePosition.Y - g_oy, idx;
            if (m->dwEventFlags & MOUSE_WHEELED)
                return (short)HIWORD(m->dwButtonState) > 0 ? TK_CIMA : TK_BAIXO;
            if (m->dwEventFlags & MOUSE_MOVED) {
                idx = zonaEm(x, y);
                if (idx != g_hover) {
                    int cod = idx >= 0 ? g_zonas[idx].codigo : 0;
                    hoverAplicar(idx);
                    if (cod != g_passa) { g_passa = cod; if (g_querPassa) return TK_PASSA; }
                }
                continue;
            }
            if (m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
                idx = zonaEm(x, y);
                g_mx = x; g_my = y;
                g_duplo = (m->dwEventFlags & DOUBLE_CLICK) != 0;
                if (idx >= 0) return g_zonas[idx].codigo;
            } else if (m->dwButtonState & RIGHTMOST_BUTTON_PRESSED) {
                return TK_ESC;                      /* botão direito = voltar */
            }
        } else if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            tReposicionar();
        }
    }
}

/* ------------------------- Componentes visuais ------------------------- */

/* Painel arredondado com fundo próprio e título opcional. */
TUI void tCaixa(int x, int y, int w, int h, Estilo borda, const char *titulo) {
    int i;
    tZonasRemover(x, y, w, h);
    tBase(x, y, w, h, PALETA[P_PAINEL], LEGADO[P_PAINEL]);
    tPreenche(x, y, w, h, COR_TEXTO);
    tTexto(x, y, borda, "╭");
    tRepete(x + 1, y, w - 2, borda, "─");
    tTexto(x + w - 1, y, borda, "╮");
    for (i = 1; i < h - 1; i++) { tTexto(x, y + i, borda, "│"); tTexto(x + w - 1, y + i, borda, "│"); }
    tTexto(x, y + h - 1, borda, "╰");
    tRepete(x + 1, y + h - 1, w - 2, borda, "─");
    tTexto(x + w - 1, y + h - 1, borda, "╯");
    if (titulo && *titulo) tTexto(x + 2, y, COR_FORTE, " %s ", titulo);
}

TUI void tDivisoria(int x, int y, int w, Estilo e) {
    tTexto(x, y, e, "├");
    tRepete(x + 1, y, w - 2, e, "─");
    tTexto(x + w - 1, y, e, "┤");
}

/* Sombra suave à direita e abaixo de um retângulo. */
TUI void tSombra(int x, int y, int w, int h) {
    int i;
    for (i = 1; i <= h; i++)
        if (y + i < g_telaA && x + w < g_telaL) {
            Cel *c = &g_tras[y + i][x + w];
            c->fg = misturar(c->fg, 0, 0.55); c->bg = misturar(c->bg, 0, 0.55);
        }
    for (i = 1; i <= w; i++)
        if (y + h < g_telaA && x + i < g_telaL) {
            Cel *c = &g_tras[y + h][x + i];
            c->fg = misturar(c->fg, 0, 0.55); c->bg = misturar(c->bg, 0, 0.55);
        }
}

/* Botão em forma de pílula: ▐ rótulo ▌. Retorna a largura usada. */
TUI int tBotao(int x, int y, const char *rotulo, int codigo, int corFundo, int ativo) {
    int w = u8Larg(rotulo) + 2, cf = ativo ? corFundo : P_BORDA;
    int claro = cf == P_CAMPO || cf == P_BORDA || cf == P_PAINEL;     /* fundo escuro: texto claro */
    Estilo meio = EST(!ativo ? P_FRACO : claro ? P_FORTE : P_ESCURO, cf) | NEGRITO;
    tCel(x, y, 0x2590, 1, EST(cf, 0));                 /* ▐ */
    tCampo(x + 1, y, w, meio, AL_CENTRO, "%s", rotulo);
    tCel(x + w + 1, y, 0x258C, 1, EST(cf, 0));         /* ▌ */
    if (ativo && codigo) tZona(x, y, w + 2, 1, codigo, COR_HOVER);
    return w + 2;
}

/* Texto com quebra automática por palavras. Retorna linhas usadas. */
TUI int tParagrafo(int x, int y, int w, int maxLin, Estilo e, const char *txt) {
    char linha[400];
    int lin = 0;
    while (*txt && lin < maxLin) {
        const char *p = txt, *corte = NULL, *q;
        int n = 0;
        while (*p && *p != '\n' && n < w) {
            if (*p == ' ') corte = p;
            n += larguraCP(u8Prox(&p));
        }
        if (!*p || *p == '\n' || !corte) corte = p;
        q = corte;
        if ((size_t)(q - txt) >= sizeof linha) q = txt + sizeof linha - 1;
        memcpy(linha, txt, (size_t)(q - txt));
        linha[q - txt] = '\0';
        tCampo(x, y + lin, w, e, AL_ESQ, "%s", linha);
        lin++;
        txt = corte;
        while (*txt == ' ' || *txt == '\n') txt++;
    }
    for (; lin < maxLin; lin++) tCampo(x, y + lin, w, e, AL_ESQ, "");
    return lin;
}

/* Barra de progresso com degradê azul → roxo. */
TUI void tBarra(int x, int y, int w, double frac, Estilo e) {
    int i, cheio;
    (void)e;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    cheio = (int)(frac * w + 0.5);
    for (i = 0; i < w; i++) {
        if (i < cheio)
            tCelRGB(x + i, y, 0x2588, 1, misturar(PALETA[P_AZUL], PALETA[P_ROXO], w > 1 ? (double)i / (w - 1) : 0),
                    0xFFFFFFFFu, 0, LEGADO[P_AZUL], -1);
        else
            tCelRGB(x + i, y, 0x2588, 1, PALETA[P_CAMPO], 0xFFFFFFFFu, 0, LEGADO[P_CAMPO], -1);
    }
}

/* Mantém 'sel' visível numa janela de 'alt' linhas. */
TUI void tRolagem(int sel, int *topo, int alt, int n) {
    if (sel < *topo) *topo = sel;
    if (sel >= *topo + alt) *topo = sel - alt + 1;
    if (*topo > n - alt) *topo = n - alt;
    if (*topo < 0) *topo = 0;
}

/* Barra de rolagem vertical (trilho + indicador). */
TUI void tBarraRolagem(int x, int y, int alt, int topo, int n) {
    int i, ini, tam;
    if (n <= alt) { for (i = 0; i < alt; i++) tCel(x, y + i, ' ', 1, COR_TEXTO); return; }
    tam = alt * alt / n; if (tam < 1) tam = 1;
    ini = (alt - tam) * topo / (n - alt);
    for (i = 0; i < alt; i++)
        tCel(x, y + i, i >= ini && i < ini + tam ? 0x2588 : 0x2502, 1, i >= ini && i < ini + tam ? COR_ACENTO : COR_BORDA);
}

/* ------------------------- Moldura da tela ------------------------- */

/* Prepara uma tela nova (ajustada ao tamanho atual da janela). */
TUI void tLimpar(void) {
    int i, j;
    g_redim = 0;
    while (!tMedir()) {                          /* janela pequena demais */
        CONSOLE_SCREEN_BUFFER_INFO inf;
        char m[160];
        memset(&inf, 0, sizeof inf);
        int cols = MIN_L, lins = MIN_A, t;
        if (GetConsoleScreenBufferInfo(g_saida, &inf)) {
            cols = inf.srWindow.Right - inf.srWindow.Left + 1;
            lins = inf.srWindow.Bottom - inf.srWindow.Top + 1;
        }
        g_telaL = cols > MAX_L ? MAX_L : cols; g_telaA = lins > MAX_A ? MAX_A : lins;
        g_vx = g_vy = 0; g_ox = inf.srWindow.Left; g_oy = inf.srWindow.Top;
        for (i = 0; i < g_telaA; i++) for (j = 0; j < g_telaL; j++) { g_tras[i][j].base = PALETA[P_FUNDO]; g_tras[i][j].lbase = 0; }
        tPreenche(0, 0, g_telaL, g_telaA, COR_TEXTO);
        snprintf(m, sizeof m, "Janela pequena: %d × %d", cols, lins);
        tCampo(0, g_telaA / 2 - 1, g_telaL, COR_AVISO, AL_CENTRO, "%s", m);
        snprintf(m, sizeof m, "Aumente para pelo menos %d × %d", MIN_L, MIN_A);
        tCampo(0, g_telaA / 2, g_telaL, COR_TEXTO, AL_CENTRO, "%s", m);
        g_sujo = 1;
        g_nZonas = g_zonaBase = 0;
        g_hover = -1;
        t = tTecla();
        (void)t;
        g_redim = 0;
    }
    for (i = 0; i < g_telaA; i++)
        for (j = 0; j < g_telaL; j++) {
            g_tras[i][j].base = PALETA[P_FUNDO];
            g_tras[i][j].lbase = 0;
        }
    tPreenche(0, 0, g_telaL, g_telaA, COR_TEXTO);
    g_sujo = 1;
    g_nZonas = g_zonaBase = 0;
    g_hover = -1;
    g_passa = 0;
    g_cursorX = -1;
}

/* Linha de mensagens: selo colorido + texto. */
TUI void tStatus(TipoMsg tipo, const char *fmt, ...) {
    static const char *ICONE[] = { "", "›", "✓", "!", "✕" };
    static const int   COR[]   = { P_TEXTO, P_AZUL, P_OK, P_AVISO, P_ERRO };
    char buf[400];
    va_list ap;
    tPreenche(0, LIN_STATUS, g_telaL, 1, COR_TEXTO);
    g_temStatus = tipo != MSG_LIMPA;
    if (!g_temStatus) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    tCel(1, LIN_STATUS, 0x2590, 1, EST(COR[tipo], 0));
    tTexto(2, LIN_STATUS, EST(P_ESCURO, COR[tipo]) | NEGRITO, "%s", ICONE[tipo]);
    tCel(3, LIN_STATUS, 0x258C, 1, EST(COR[tipo], 0));
    tCampo(5, LIN_STATUS, g_telaL - 6, tipo == MSG_INFO ? COR_TEXTO : EST(COR[tipo], 0), AL_ESQ, "%s", buf);
}

TUI void tAvisoPendente(TipoMsg tipo, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_pendTexto, sizeof g_pendTexto, fmt, ap);
    va_end(ap);
    g_pendTipo = tipo;
}

/* Converte o nome de uma tecla do rodapé no código devolvido ao clicar. */
static int codigoTecla(const char *k) {
    if (!strcmp(k, "Esc")) return TK_ESC;
    if (!strcmp(k, "Enter")) return TK_ENTER;
    if (!strcmp(k, "Tab")) return TK_TAB;
    if (!strcmp(k, "Del")) return TK_DEL;
    if (u8Len(k) == 1) { const char *p = k; return (int)u8Prox(&p); }
    return 0;
}

/* Rodapé: "Tecla Descrição|Tecla Descrição". Cada atalho também é clicável. */
TUI void tRodape(const char *atalhos) {
    char item[96];
    int x = 1;
    snprintf(g_rodape, sizeof g_rodape, "%s", atalhos ? atalhos : "");
    tZonasRemover(0, LIN_RODAPE, g_telaL, 1);
    tPreenche(0, LIN_RODAPE, g_telaL, 1, COR_TEXTO);
    while (atalhos && *atalhos) {
        const char *fim = strchr(atalhos, '|');
        char *esp;
        size_t n = fim ? (size_t)(fim - atalhos) : strlen(atalhos);
        int lt, ld, cod;
        if (n >= sizeof item) n = sizeof item - 1;
        memcpy(item, atalhos, n);
        item[n] = '\0';
        esp = strchr(item, ' ');
        if (esp) *esp++ = '\0';
        lt = u8Larg(item) + 2;
        ld = esp ? u8Larg(esp) : 0;
        if (x + lt + 1 + ld >= g_telaL) break;
        cod = codigoTecla(item);
        tTexto(x, LIN_RODAPE, COR_TECLA, " %s ", item);
        if (esp) tTexto(x + lt + 1, LIN_RODAPE, COR_FRACO, "%s", esp);
        if (cod) tZona(x, LIN_RODAPE, lt + 1 + ld, 1, cod, COR_HOVER);
        x += lt + 1 + ld + 3;
        atalhos = fim ? fim + 1 : atalhos + n;
    }
}

/* Estrutura padrão: cabeçalho em degradê, trilha de navegação e rodapé. */
TUI void tTela(const char *trilha, const char *atalhos) {
    const char *p, *ultimo = NULL;
    char dir[96], hora[8];
    time_t t = time(NULL);
    int i;
    tLimpar();
    for (i = 0; i < g_telaL; i++) {                   /* cabeçalho em degradê */
        unsigned c = misturar(PALETA[P_AZUL], PALETA[P_ROXO], (double)i / (g_telaL - 1));
        g_tras[0][i].base = c;
        g_tras[0][i].lbase = 3;
        tCel(i, 0, ' ', 1, EST(P_ESCURO, 0));
    }
    tTexto(2, 0, EST(P_ESCURO, 0) | NEGRITO, "%s  %s", ic(g_icone, g_iconeAlt), g_app);
    strftime(hora, sizeof hora, "%H:%M", localtime(&t));
    snprintf(dir, sizeof dir, "%s   %s", g_rotulo, hora);
    tCampo(g_telaL - 2 - u8Larg(dir), 0, u8Larg(dir), EST(P_ESCURO, 0) | NEGRITO, AL_DIR, "%s", dir);

    for (p = strstr(trilha, "›"); p; p = strstr(p + 1, "›")) ultimo = p;
    {
        int x = tTexto(2, 1, COR_FRACO, "⌂ ");
        if (ultimo) {
            char ant[200];
            size_t n = (size_t)(ultimo - trilha) + 3;
            if (n >= sizeof ant) n = sizeof ant - 1;
            memcpy(ant, trilha, n);
            ant[n] = '\0';
            x = tTexto(x, 1, COR_FRACO, "%s", ant);
            tTexto(x, 1, COR_ACENTO | NEGRITO, "%s", ultimo + 3);
        } else {
            tTexto(x, 1, COR_ACENTO | NEGRITO, "%s", trilha);
        }
    }
    if (g_mouse) tCampo(g_telaL - 30, 1, 28, COR_FRACO, AL_DIR, "mouse e teclado ativos");
    tRepete(0, g_telaA - 3, g_telaL, COR_BORDA, "─");
    tRodape(atalhos);
    tStatus(MSG_LIMPA, "");
    if (g_pendTipo != MSG_LIMPA) {
        tStatus(g_pendTipo, "%s", g_pendTexto);
        g_pendTipo = MSG_LIMPA;
    }
}

/* ------------------------- Janelas sobrepostas ------------------------- */

TUI void tSalvar(Recorte *r, int x, int y, int w, int h) {
    int i;
    hoverDesfazer();
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->nZonas = g_nZonas; r->zonaBase = g_zonaBase;
    r->buf = (Cel *)malloc(sizeof(Cel) * (size_t)w * (size_t)h);
    if (r->buf)
        for (i = 0; i < h; i++)
            if (y + i < g_telaA) memcpy(&r->buf[i * w], &g_tras[y + i][x], sizeof(Cel) * (size_t)w);
    g_zonaBase = g_nZonas;
    g_modal++;
}

TUI void tRestaurar(Recorte *r) {
    int i;
    hoverDesfazer();
    if (r->buf) {
        for (i = 0; i < r->h; i++)
            if (r->y + i < g_telaA) memcpy(&g_tras[r->y + i][r->x], &r->buf[i * r->w], sizeof(Cel) * (size_t)r->w);
        free(r->buf);
        r->buf = NULL;
    }
    g_nZonas = r->nZonas;
    g_zonaBase = r->zonaBase;
    g_modal--;
}

static int tContaLinhas(const char *s) {
    int n = 1;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

/* Abre um diálogo centralizado com sombra e escreve a mensagem. */
static void tAbrirDialogo(Recorte *r, int w, const char *titulo, const char *msg,
                          Estilo borda, int *px, int *py, int *ph) {
    int h = tContaLinhas(msg) + 6, x = (g_telaL - w) / 2, y = (g_telaA - h) / 2 - 1, i = 0;
    char linha[200];
    tSalvar(r, x, y, w + 1, h + 1);
    tCaixa(x, y, w, h, borda, titulo);
    tSombra(x, y, w, h);
    while (*msg) {
        const char *fim = strchr(msg, '\n');
        size_t n = fim ? (size_t)(fim - msg) : strlen(msg);
        if (n >= sizeof linha) n = sizeof linha - 1;
        memcpy(linha, msg, n);
        linha[n] = '\0';
        tCampo(x + 2, y + 2 + i, w - 4, i == 0 ? COR_FORTE : COR_TEXTO, AL_CENTRO, "%s", linha);
        i++;
        msg = fim ? fim + 1 : msg + n;
    }
    *px = x; *py = y; *ph = h;
}

/* Diálogo Sim/Não — teclado (←/→, S/N, Enter, Esc) ou mouse. */
TUI int tConfirmar(const char *titulo, const char *msg, int padraoSim) {
    Recorte r;
    int x, y, h, sim = padraoSim ? 1 : 0, t, bx, querAnt = g_querPassa;
    char tit[120];
    snprintf(tit, sizeof tit, "%s %s", ic("💬", "?"), titulo);
    tAbrirDialogo(&r, 54, tit, msg, COR_AVISO, &x, &y, &h);
    bx = x + (54 - 27) / 2;
    g_querPassa = 1;
    for (;;) {
        tBotao(bx, y + h - 2, "  ✓  Sim  ", 's', sim ? P_AZUL : P_CAMPO, 1);
        tBotao(bx + 14, y + h - 2, "  ✕  Não  ", 'n', sim ? P_CAMPO : P_AZUL, 1);
        t = tTecla();
        if (t == TK_PASSA) { if (g_passa == 's') sim = 1; else if (g_passa == 'n') sim = 0; continue; }
        if (t == TK_ESQ || t == TK_DIR || t == TK_TAB) sim = !sim;
        else if (t == 's' || t == 'S') { sim = 1; break; }
        else if (t == 'n' || t == 'N' || t == TK_ESC) { sim = 0; break; }
        else if (t == TK_ENTER) break;
    }
    g_querPassa = querAnt;
    tRestaurar(&r);
    return sim;
}

/* Diálogo informativo com botão OK. */
TUI void tAlerta(TipoMsg tipo, const char *titulo, const char *msg) {
    Recorte r;
    int x, y, h, t;
    char tit[120];
    Estilo cor = tipo == MSG_ERRO ? COR_ERRO : tipo == MSG_OK ? COR_OK : tipo == MSG_AVISO ? COR_AVISO : COR_ACENTO;
    const char *icone = tipo == MSG_ERRO ? ic("🚫", "✕") : tipo == MSG_OK ? ic("🎉", "✓")
                      : tipo == MSG_AVISO ? ic("🔔", "!") : ic("💡", "i");
    snprintf(tit, sizeof tit, "%s %s", icone, titulo);
    tAbrirDialogo(&r, 58, tit, msg, cor, &x, &y, &h);
    tBotao(x + (58 - 12) / 2, y + h - 2, "   OK   ", TK_ENTER, P_AZUL, 1);
    do { t = tTecla(); } while (t != TK_ENTER && t != TK_ESC && t != ' ');
    tRestaurar(&r);
}

/* Janela com barra de progresso animada. */
TUI void tProgresso(const char *titulo, int passos) {
    Recorte r;
    int x, y, h, i;
    tAbrirDialogo(&r, 46, titulo, " ", COR_ACENTO, &x, &y, &h);
    for (i = 0; i <= passos; i++) {
        tBarra(x + 4, y + 3, 32, (double)i / passos, 0);
        tTexto(x + 37, y + 3, COR_TEXTO, "%3d%%", i * 100 / passos);
        tPresent();
        Sleep(14);
    }
    Sleep(150);
    tRestaurar(&r);
}

/* ------------------------- Entrada de dados ------------------------- */

/* Campo de texto. Enter confirma (1), Esc cancela (0).
 * Com 'selecionado', o valor inicial aparece destacado: digitar o
 * substitui, Enter o mantém e Backspace passa a editá-lo. */
TUI int tEditar(int x, int y, int w, char *buf, int tam, int maxCars, FiltroCampo f, int selecionado) {
    unsigned cp[128];
    char vis[600], tmp[4];
    const char *p = buf;
    int n = 0, i, t, ini, pos, k, temVirgula;
    if (maxCars > 120) maxCars = 120;
    while (*p && n < maxCars) cp[n++] = u8Prox(&p);
    g_modal++;
    tZona(x, y, w, 1, TK_ZONA - 1, 0);            /* clicar no campo não faz nada */
    for (;;) {
        ini = n > w - 2 ? n - (w - 2) : 0;
        for (i = ini, pos = 0; i < n; i++) pos += u8Codifica(cp[i], vis + pos);
        vis[pos] = '\0';
        tCampo(x, y, w, COR_CAMPO, AL_ESQ, "");
        tTexto(x + 1, y, selecionado && n ? COR_SEL : COR_CAMPO, "%s", vis);
        g_cursorX = x + 1 + n - ini;
        g_cursorY = y;
        t = tTecla();
        if (t == TK_ENTER) break;
        if (t == TK_ESC) { g_cursorX = -1; g_modal--; return 0; }
        if (t == TK_BS) { if (n > 0 && !selecionado) n--; selecionado = 0; continue; }
        if (t < 32 || t >= TK_CIMA || (t >= 0xD800 && t <= 0xDFFF) || larguraCP((unsigned)t) != 1) continue;
        if (f == FILTRO_INTEIRO) {
            if (!((t >= '0' && t <= '9') || (t == '-' && (n == 0 || selecionado)))) continue;
        } else if (f == FILTRO_REAL) {
            if (t == '.') t = ',';
            if (!((t >= '0' && t <= '9') || t == ',' || t == '-')) continue;
        }
        if (selecionado) { n = 0; selecionado = 0; }
        if (n >= maxCars) continue;
        if (f == FILTRO_REAL) {
            for (i = 0, temVirgula = 0; i < n; i++) if (cp[i] == ',') temVirgula = 1;
            if ((t == ',' && temVirgula) || (t == '-' && n != 0)) continue;
        }
        cp[n++] = (unsigned)t;
    }
    for (i = 0, pos = 0; i < n; i++) {
        k = u8Codifica(cp[i], tmp);
        if (pos + k >= tam) break;
        memcpy(buf + pos, tmp, (size_t)k);
        pos += k;
    }
    buf[pos] = '\0';
    tCampo(x, y, w, COR_CAMPO, AL_ESQ, " %s", buf);
    g_cursorX = -1;
    g_modal--;
    return 1;
}

/* Formata número no padrão brasileiro (1.234,5), sem zeros finais. */
TUI void tFmtNum(double v, char *out, size_t tam, int casas) {
    char tmp[64], res[80];
    char *dec, *p;
    int neg, lenInt, i, j = 0;
    if (v != v) { snprintf(out, tam, "indefinido"); return; }
    if (v > 1e15 || v < -1e15) {
        snprintf(tmp, sizeof tmp, "%.4e", v);
        for (p = tmp; *p; p++) if (*p == '.') *p = ',';
        snprintf(out, tam, "%s", tmp);
        return;
    }
    snprintf(tmp, sizeof tmp, "%.*f", casas, v);
    dec = strchr(tmp, '.');
    if (dec) {
        p = tmp + strlen(tmp) - 1;
        while (p > dec && *p == '0') *p-- = '\0';
        if (p == dec) *p = '\0';
    }
    if (strcmp(tmp, "-0") == 0) strcpy(tmp, "0");
    neg = tmp[0] == '-';
    dec = strchr(tmp, '.');
    lenInt = (int)((dec ? dec : tmp + strlen(tmp)) - tmp) - neg;
    if (neg) res[j++] = '-';
    for (i = 0; i < lenInt; i++) {
        if (i > 0 && (lenInt - i) % 3 == 0) res[j++] = '.';
        res[j++] = tmp[neg + i];
    }
    if (dec) { res[j++] = ','; strcpy(res + j, dec + 1); j += (int)strlen(dec + 1); }
    res[j] = '\0';
    snprintf(out, tam, "%s", res);
}

TUI int tLerReal(int x, int y, int w, const char *inicial, int selecionado,
                 double min, double max, const char *msgFaixa, double *out) {
    char buf[64], tmp[64], a[40], b[40], *fim;
    double v;
    size_t i;
    snprintf(buf, sizeof buf, "%s", inicial ? inicial : "");
    for (;;) {
        if (!tEditar(x, y, w, buf, sizeof buf, 18, FILTRO_REAL, selecionado)) return 0;
        selecionado = 0;
        strcpy(tmp, buf);
        for (i = 0; tmp[i]; i++) if (tmp[i] == ',') tmp[i] = '.';
        v = strtod(tmp, &fim);
        if (tmp[0] == '\0' || fim == tmp || *fim != '\0') {
            tStatus(MSG_ERRO, "Digite um número válido, por exemplo 12 ou 3,5.");
        } else if (v < min || v > max) {
            tFmtNum(min, a, sizeof a, 2);
            tFmtNum(max, b, sizeof b, 2);
            if (msgFaixa) tStatus(MSG_ERRO, "%s", msgFaixa);
            else tStatus(MSG_ERRO, "O valor precisa estar entre %s e %s.", a, b);
        } else {
            if (g_temStatus) tStatus(MSG_LIMPA, "");
            *out = v;
            return 1;
        }
    }
}

TUI int tLerInt(int x, int y, int w, const char *inicial, int selecionado, long min, long max, long *out) {
    char buf[32], *fim;
    long v;
    snprintf(buf, sizeof buf, "%s", inicial ? inicial : "");
    for (;;) {
        if (!tEditar(x, y, w, buf, sizeof buf, 9, FILTRO_INTEIRO, selecionado)) return 0;
        selecionado = 0;
        v = strtol(buf, &fim, 10);
        if (buf[0] == '\0' || fim == buf || *fim != '\0')
            tStatus(MSG_ERRO, "Digite um número inteiro.");
        else if (v < min || v > max)
            tStatus(MSG_ERRO, "O valor precisa estar entre %ld e %ld.", min, max);
        else {
            if (g_temStatus) tStatus(MSG_LIMPA, "");
            *out = v;
            return 1;
        }
    }
}

/* ------------------------------ Menu ------------------------------ */

#define ZN_MENU (TK_ZONA + 0x1000)

static void desenharItemMenu(int x, int y, int w, const ItemMenu *it, int i, int sel) {
    if (sel) {
        tCel(x, y, 0x2590, 1, EST(P_AZUL, 0));
        tPreenche(x + 1, y, w - 2, 1, COR_SEL);
        tTexto(x + 2, y, COR_SEL, "%s", ic(it->icone, it->alt));
        tCampo(x + 5, y, w - 10, COR_SEL, AL_ESQ, "%s", it->rotulo);
        tCampo(x + w - 5, y, 3, COR_SEL, AL_DIR, "%d", i + 1);
        tCel(x + w - 1, y, 0x258C, 1, EST(P_AZUL, 0));
    } else {
        tPreenche(x, y, w, 1, COR_TEXTO);
        tTexto(x + 2, y, COR_ACENTO, "%s", ic(it->icone, it->alt));
        tCampo(x + 5, y, w - 10, COR_TEXTO, AL_ESQ, "%s", it->rotulo);
        tCampo(x + w - 5, y, 3, COR_FRACO, AL_DIR, "%d", i + 1);
    }
}

/* Menu vertical: setas + Enter, número, atalho, clique ou passagem do mouse.
 * 'espaco' = linhas por item (1 ou 2). Retorna o índice, -1 (Esc) ou MENU_REDIM. */
TUI int tMenu(int x, int y, int w, const ItemMenu *it, int n, int *sel, void (*aoMudar)(int), int espaco) {
    int i, t, anterior = -1, querAnt = g_querPassa, res;
    g_querPassa = 1;
    for (;;) {
        for (i = 0; i < n; i++) {
            desenharItemMenu(x, y + i * espaco, w, &it[i], i, i == *sel);
            tZona(x, y + i * espaco, w, 1, ZN_MENU + i, 0);
        }
        if (*sel != anterior) { if (aoMudar) aoMudar(*sel); anterior = *sel; }
        t = tTecla();
        res = -3;
        if (t == TK_REDIM) res = MENU_REDIM;
        else if (t == TK_PASSA) {
            if (g_passa >= ZN_MENU && g_passa < ZN_MENU + n) *sel = g_passa - ZN_MENU;
            continue;
        } else if (t >= ZN_MENU && t < ZN_MENU + n) { *sel = t - ZN_MENU; res = *sel; }
        else if (t == TK_CIMA || t == TK_BAIXO || t == TK_INICIO || t == TK_FIM) {
            if (g_temStatus) tStatus(MSG_LIMPA, "");
            if (t == TK_CIMA)   *sel = (*sel + n - 1) % n;
            if (t == TK_BAIXO)  *sel = (*sel + 1) % n;
            if (t == TK_INICIO) *sel = 0;
            if (t == TK_FIM)    *sel = n - 1;
            continue;
        }
        else if (t == TK_ENTER) res = *sel;
        else if (t == TK_ESC) res = -1;
        else if (n <= 9 && t >= '1' && t < '1' + n) { *sel = t - '1'; res = *sel; }
        else if (t < TK_CIMA) {
            for (i = 0; i < n; i++) {
                int a = it[i].atalho;
                if (a && (t == a || (a >= 'a' && a <= 'z' && t == a - 32))) { *sel = i; res = i; break; }
            }
            if (res == -3)
                tStatus(MSG_AVISO, "Opção inválida. Use o mouse, as setas ↑ ↓ e Enter, ou um número de 1 a %d.", n);
        }
        if (res != -3) { g_querPassa = querAnt; return res; }
    }
}

/* --------------------------- Ciclo de vida --------------------------- */

static void tRestaurarConsole(void) {
    DWORD k;
    static const wchar_t fim[] = L"\x1b[0m\x1b[2J\x1b[H\x1b[?25h";
    if (g_vt) WriteConsoleW(g_saida, fim, (DWORD)(sizeof fim / sizeof fim[0] - 1), &k, NULL);
    SetConsoleMode(g_saida, g_modoSaida);
    if (g_mouse) SetConsoleMode(g_entrada, g_modoEntrada);
}

static BOOL WINAPI tAoInterromper(DWORD tipo) {   /* Ctrl+C: devolve o console limpo */
    (void)tipo;
    tRestaurarConsole();
    return FALSE;
}

TUI void tIniciar(const char *icone, const char *alt, const char *app, const char *rotulo) {
    wchar_t titulo[128];
    char tmp[128];
    const char *p;
    int n = 0;
    g_saida = GetStdHandle(STD_OUTPUT_HANDLE);
    g_entrada = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(g_saida, &g_modoSaida)) {
        fprintf(stderr, "Este programa precisa ser executado em um console do Windows.\n");
        exit(1);
    }
    g_vt = SetConsoleMode(g_saida, (g_modoSaida | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN)
                                   & ~(DWORD)ENABLE_WRAP_AT_EOL_OUTPUT) != 0;
    if (!g_vt) SetConsoleMode(g_saida, g_modoSaida & ~(DWORD)ENABLE_WRAP_AT_EOL_OUTPUT);
    g_emoji = g_vt && getenv("WT_SESSION") != NULL;      /* Windows Terminal */
    if (GetConsoleMode(g_entrada, &g_modoEntrada))
        g_mouse = SetConsoleMode(g_entrada, ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT
                                            | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT) != 0;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(tAoInterromper, TRUE);
    g_icone = icone; g_iconeAlt = alt; g_app = app; g_rotulo = rotulo;
    snprintf(tmp, sizeof tmp, "%s · %s", app, rotulo);
    for (p = tmp; *p && n < 126; ) titulo[n++] = (wchar_t)u8Prox(&p);
    titulo[n] = 0;
    SetConsoleTitleW(titulo);
}

/* Tela de boas-vindas animada. Qualquer tecla ou clique continua. */
TUI void tAbertura(const char *titulo, const char *linha1, const char *linha2) {
    char espacado[200];
    const char *p = titulo;
    int pos = 0, i, x, y, w = 60, h = 15, t;
    while (*p && pos < 190) {
        const char *ini = p;
        u8Prox(&p);
        memcpy(espacado + pos, ini, (size_t)(p - ini));
        pos += (int)(p - ini);
        if (*p) espacado[pos++] = ' ';
    }
    espacado[pos] = '\0';
    for (;;) {
        tLimpar();
        x = (g_telaL - w) / 2;
        y = (g_telaA - h) / 2;
        tCaixa(x, y, w, h, COR_ROXO, NULL);
        tSombra(x, y, w, h);
        tCampo(x + 2, y + 2, w - 4, COR_TEXTO, AL_CENTRO, "%s", ic(g_icone, g_iconeAlt));
        tTextoDegrade(x + (w - u8Larg(espacado)) / 2, y + 4, espacado, PALETA[P_ACENTO], PALETA[P_ROXO], 1);
        tCampo(x + 2, y + 6, w - 4, COR_TEXTO, AL_CENTRO, "%s", linha1);
        tCampo(x + 2, y + 7, w - 4, COR_FRACO, AL_CENTRO, "%s", linha2);
        for (i = 0; i <= 30; i++) {
            tBarra(x + 10, y + 10, w - 20, i / 30.0, 0);
            tPresent();
            if (!_kbhit()) Sleep(14);
        }
        while (_kbhit()) _getwch();
        tCampo(x + 2, y + 12, w - 4, COR_AVISO, AL_CENTRO, g_mouse ? "Clique ou pressione qualquer tecla para começar"
                                                                     : "Pressione qualquer tecla para começar");
        tZona(0, 0, g_telaL, g_telaA, TK_ENTER, 0);
        t = tTecla();
        if (t != TK_REDIM && t != TK_PASSA) break;
    }
}

TUI void tFinalizar(const char *mensagem) {
    int x, y, w = 50;
    tLimpar();
    x = (g_telaL - w) / 2;
    y = g_telaA / 2 - 3;
    tCaixa(x, y, w, 5, COR_ROXO, NULL);
    tSombra(x, y, w, 5);
    tCampo(x + 2, y + 2, w - 4, COR_FORTE, AL_CENTRO, "%s  %s", ic("👋", "◆"), mensagem);
    tPresent();
    Sleep(900);
    tRestaurarConsole();
}
/* ======================= FIM DO MÓDULO DE INTERFACE ======================= */

/* ============================== DADOS ============================== */

#define ARQ_PRODUTOS "produtos.txt"
#define ARQ_TEMP     "produtos.tmp"
#define ARQ_PEDIDOS  "pedidos.txt"
#define DESCONTO_PCT 10
#define TAM_NOME     64
#define MAX_NOME     28
#define MAX_ITENS    30
#define MAX_QTD      99

typedef long Centavos;
typedef enum { CAT_SANDUICHE, CAT_SUCO, CAT_SOBREMESA, NUM_CAT } Categoria;
typedef enum { PAG_DINHEIRO, PAG_CARTAO, PAG_PIX, NUM_PAG } Pagamento;

typedef struct { int id; Categoria cat; char nome[TAM_NOME]; Centavos preco; } Produto;
typedef struct { Produto *v; int n, cap; } Cardapio;
typedef struct { int id; char nome[TAM_NOME]; Centavos preco; int qtd; } Item;
typedef struct { Item v[MAX_ITENS]; int n; } Carrinho;
typedef struct {
    int numero;
    char data[20];
    Pagamento forma;
    Centavos subtotal, desconto, total, recebido;
    Carrinho itens;
} Pedido;
typedef struct { Pedido *v; int n, cap; } Historico;

static const char *CAT_PLURAL[NUM_CAT] = { "Sanduíches", "Sucos", "Sobremesas" };
static const char *CAT_NOME[NUM_CAT]   = { "Sanduíche", "Suco", "Sobremesa" };
static const char *CAT_ICONE[NUM_CAT]  = { "🍔", "🥤", "🍰" };
static const char *CAT_ALT[NUM_CAT]    = { "S ", "J ", "D " };
static const int   CAT_COR[NUM_CAT]    = { P_LARANJA, P_ACENTO, P_ROXO };
static const char *PAG_NOME[NUM_PAG]   = { "Dinheiro", "Cartão", "Pix" };
static const char *PAG_DESC[NUM_PAG]   = { "10% de desconto", "Débito ou crédito", "Aprovação imediata" };
static const char *PAG_ICONE[NUM_PAG]  = { "💵", "💳", "📱" };
static const char *PAG_ALT[NUM_PAG]    = { "$ ", "= ", "# " };

/* Zonas clicáveis */
#define ZN_ABA      (TK_ZONA + 0x100)
#define ZN_LINHA    (TK_ZONA + 0x200)
#define ZN_MAIS     (TK_ZONA + 0x400)
#define ZN_MENOS    (TK_ZONA + 0x600)
#define ZN_CARR     (TK_ZONA + 0x800)
#define ZN_FORMA    (TK_ZONA + 0x900)
#define ZN_CHIP     (TK_ZONA + 0x910)
#define ZN_FINALIZA (TK_ZONA + 0x920)
#define ZN_CMAIS    (TK_ZONA + 0x921)
#define ZN_CMENOS   (TK_ZONA + 0x922)
#define ZN_CREMOVE  (TK_ZONA + 0x923)
#define ZN_NOVO     (TK_ZONA + 0x924)
#define ZN_EDITAR   (TK_ZONA + 0x925)
#define ZN_EXCLUIR  (TK_ZONA + 0x926)
#define ZN_CONFIRMA (TK_ZONA + 0x927)
#define ZN_PED      (TK_ZONA + 0x10000)  /* + linha do histórico */

static Cardapio  g_card;
static Historico g_hist;
static int      *g_idx;
static int       g_idxCap;

static const ItemMenu MENU[] = {
    { "🛒", "+ ", "Novo pedido", "Monte o pedido clicando nos produtos, ajuste as quantidades e finalize escolhendo a forma de pagamento.", 'n' },
    { "📋", "≡ ", "Cardápio", "Cadastre, edite ou exclua produtos nas categorias sanduíches, sucos e sobremesas.", 'c' },
    { "📊", "$ ", "Histórico de vendas", "Veja os pedidos anteriores, o faturamento e reabra o cupom de qualquer venda.", 'h' },
    { "🚪", "< ", "Sair", "Encerra o sistema. Tudo já fica salvo automaticamente.", 0 }
};
#define N_MENU ((int)(sizeof MENU / sizeof MENU[0]))

/* ============================ UTILIDADES ============================ */

static const char *valor(Centavos c) {               /* "1.234,56" */
    static char buf[8][32];
    static int k;
    char dig[24], *o;
    unsigned long v = (unsigned long)(c < 0 ? -c : c);
    int len, i;
    k = (k + 1) % 8;
    o = buf[k];
    if (c < 0) *o++ = '-';
    snprintf(dig, sizeof dig, "%lu", v / 100);
    len = (int)strlen(dig);
    for (i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) *o++ = '.';
        *o++ = dig[i];
    }
    sprintf(o, ",%02lu", v % 100);
    return buf[k];
}

static const char *moeda(Centavos c) {               /* "R$ 1.234,56" */
    static char buf[8][40];
    static int k;
    k = (k + 1) % 8;
    snprintf(buf[k], sizeof buf[k], "R$ %s", valor(c));
    return buf[k];
}

static Centavos paraCentavos(double v) { return (Centavos)(v * 100.0 + (v < 0 ? -0.5 : 0.5)); }

static Centavos calcDesconto(Centavos subtotal, Pagamento f) {
    return f == PAG_DINHEIRO ? (subtotal * DESCONTO_PCT + 50) / 100 : 0;
}

static void aparar(char *s) {
    char *p = s;
    size_t n;
    while (*p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

static void tirarQuebra(char *s) { s[strcspn(s, "\r\n")] = '\0'; }

static int mesmoNome(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static void fmtData(const char *iso, char *data, char *hora) {
    if (strlen(iso) < 16) { strcpy(data, "?"); strcpy(hora, "?"); return; }
    sprintf(data, "%.2s/%.2s/%.4s", iso + 8, iso + 5, iso);
    sprintf(hora, "%.5s", iso + 11);
}

static void dataISO(time_t t, char *iso, size_t tam) { strftime(iso, tam, "%Y-%m-%d %H:%M:%S", localtime(&t)); }

static const char *tit(char *buf, size_t tam, const char *emoji, const char *texto) {
    if (g_emoji) snprintf(buf, tam, "%s %s", emoji, texto);
    else snprintf(buf, tam, "%s", texto);
    return buf;
}

/* ============================ CARDÁPIO ============================ */

static int cardReservar(int n) {
    Produto *nv;
    int nc;
    if (n <= g_card.cap) return 1;
    nc = g_card.cap ? g_card.cap * 2 : 16;
    while (nc < n) nc *= 2;
    nv = (Produto *)realloc(g_card.v, sizeof(Produto) * (size_t)nc);
    if (!nv) return 0;
    g_card.v = nv;
    g_card.cap = nc;
    return 1;
}

static int cardInserir(int pos, const Produto *p) {
    if (!cardReservar(g_card.n + 1)) return 0;
    memmove(&g_card.v[pos + 1], &g_card.v[pos], sizeof(Produto) * (size_t)(g_card.n - pos));
    g_card.v[pos] = *p;
    g_card.n++;
    return 1;
}

static void cardRemover(int pos) {
    memmove(&g_card.v[pos], &g_card.v[pos + 1], sizeof(Produto) * (size_t)(g_card.n - pos - 1));
    g_card.n--;
}

static int cardBuscar(int id) {
    int i;
    for (i = 0; i < g_card.n; i++) if (g_card.v[i].id == id) return i;
    return -1;
}

static int cardProximoId(void) {
    int i, max = 0;
    for (i = 0; i < g_card.n; i++) if (g_card.v[i].id > max) max = g_card.v[i].id;
    return max + 1;
}

static int cardNomeEmUso(const char *nome, int ignorarId) {
    int i;
    for (i = 0; i < g_card.n; i++)
        if (g_card.v[i].id != ignorarId && mesmoNome(g_card.v[i].nome, nome)) return 1;
    return 0;
}

static int cardContar(Categoria cat) {
    int i, n = 0;
    for (i = 0; i < g_card.n; i++) n += g_card.v[i].cat == cat;
    return n;
}

static int cardFiltrar(Categoria cat) {
    int i, n = 0;
    if (g_idxCap < g_card.n + 1) {
        int *nv = (int *)realloc(g_idx, sizeof(int) * (size_t)(g_card.n + 16));
        if (!nv) return 0;
        g_idx = nv;
        g_idxCap = g_card.n + 16;
    }
    for (i = 0; i < g_card.n; i++) if (g_card.v[i].cat == cat) g_idx[n++] = i;
    return n;
}

/* ============================ ARQUIVOS ============================ */

static int salvarCardapio(void) {
    FILE *f = fopen(ARQ_TEMP, "w");
    int i, ok;
    if (!f) return 0;
    fprintf(f, "# Cardápio da lanchonete: id;categoria(0=sanduíche 1=suco 2=sobremesa);nome;preço\n");
    for (i = 0; i < g_card.n; i++) {
        const Produto *p = &g_card.v[i];
        fprintf(f, "%d;%d;%s;%ld.%02ld\n", p->id, (int)p->cat, p->nome, p->preco / 100, p->preco % 100);
    }
    ok = fflush(f) == 0 && !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (ok && !MoveFileExA(ARQ_TEMP, ARQ_PRODUTOS, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = 0;
    if (!ok) remove(ARQ_TEMP);
    return ok;
}

/* 1 = carregado, 2 = criado com produtos iniciais, 0 = falha ao criar. */
static int carregarCardapio(int *ignoradas) {
    static const struct { Categoria c; const char *n; Centavos p; } INICIAL[] = {
        { CAT_SANDUICHE, "X-Burger", 1800 },     { CAT_SANDUICHE, "X-Salada", 2000 },
        { CAT_SANDUICHE, "X-Bacon", 2450 },      { CAT_SANDUICHE, "Misto quente", 1200 },
        { CAT_SUCO, "Suco de laranja", 800 },    { CAT_SUCO, "Suco de açaí", 1200 },
        { CAT_SUCO, "Limonada suíça", 950 },     { CAT_SUCO, "Suco de cupuaçu", 1000 },
        { CAT_SOBREMESA, "Pudim", 950 },         { CAT_SOBREMESA, "Sorvete", 700 },
        { CAT_SOBREMESA, "Brownie com calda", 1100 }
    };
    FILE *f = fopen(ARQ_PRODUTOS, "r");
    char linha[256], nome[TAM_NOME], txtPreco[32], *lin;
    Produto p;
    int cat, i;
    *ignoradas = 0;
    if (!f) {
        for (i = 0; i < (int)(sizeof INICIAL / sizeof INICIAL[0]); i++) {
            memset(&p, 0, sizeof p);
            p.id = i + 1;
            p.cat = INICIAL[i].c;
            p.preco = INICIAL[i].p;
            snprintf(p.nome, sizeof p.nome, "%s", INICIAL[i].n);
            if (!cardInserir(g_card.n, &p)) break;
        }
        return salvarCardapio() ? 2 : 0;
    }
    while (fgets(linha, sizeof linha, f)) {
        tirarQuebra(linha);
        lin = linha;
        if ((unsigned char)lin[0] == 0xEF && (unsigned char)lin[1] == 0xBB && (unsigned char)lin[2] == 0xBF) lin += 3;
        if (lin[0] == '#' || lin[0] == '\0') continue;
        memset(&p, 0, sizeof p);
        if (sscanf(lin, "%d;%d;%63[^;];%31s", &p.id, &cat, nome, txtPreco) == 4
            && p.id > 0 && cat >= 0 && cat < NUM_CAT && cardBuscar(p.id) < 0) {
            char *fim;
            double v = strtod(txtPreco, &fim);
            aparar(nome);
            if (*fim == '\0' && v > 0 && v < 100000 && nome[0]) {
                p.cat = (Categoria)cat;
                p.preco = paraCentavos(v);
                snprintf(p.nome, sizeof p.nome, "%s", nome);
                if (cardInserir(g_card.n, &p)) continue;
            }
        }
        (*ignoradas)++;
    }
    fclose(f);
    return 1;
}

static int histAdicionar(const Pedido *p) {
    if (g_hist.n == g_hist.cap) {
        int nc = g_hist.cap ? g_hist.cap * 2 : 32;
        Pedido *nv = (Pedido *)realloc(g_hist.v, sizeof(Pedido) * (size_t)nc);
        if (!nv) return 0;
        g_hist.v = nv;
        g_hist.cap = nc;
    }
    g_hist.v[g_hist.n++] = *p;
    return 1;
}

static void carregarPedidos(void) {
    FILE *f = fopen(ARQ_PEDIDOS, "r");
    char linha[256];
    Pedido p;
    Item it;
    int forma, atual = -1;
    if (!f) return;
    while (fgets(linha, sizeof linha, f)) {
        tirarQuebra(linha);
        if (linha[0] == 'P' && linha[1] == ';') {
            memset(&p, 0, sizeof p);
            atual = -1;
            if (sscanf(linha, "P;%d;%19[^;];%d;%ld;%ld;%ld;%ld", &p.numero, p.data, &forma,
                       &p.subtotal, &p.desconto, &p.total, &p.recebido) == 7
                && forma >= 0 && forma < NUM_PAG && p.total >= 0) {
                p.forma = (Pagamento)forma;
                if (histAdicionar(&p)) atual = g_hist.n - 1;
            }
        } else if (linha[0] == 'I' && linha[1] == ';' && atual >= 0) {
            Carrinho *c = &g_hist.v[atual].itens;
            memset(&it, 0, sizeof it);
            if (c->n < MAX_ITENS && sscanf(linha, "I;%d;%d;%ld;%63[^\n]", &it.id, &it.qtd, &it.preco, it.nome) == 4)
                c->v[c->n++] = it;
        }
    }
    fclose(f);
}

static int salvarPedido(const Pedido *p) {
    FILE *f = fopen(ARQ_PEDIDOS, "a");
    int i, ok;
    if (!f) return 0;
    fprintf(f, "P;%d;%s;%d;%ld;%ld;%ld;%ld\n", p->numero, p->data, (int)p->forma,
            p->subtotal, p->desconto, p->total, p->recebido);
    for (i = 0; i < p->itens.n; i++)
        fprintf(f, "I;%d;%d;%ld;%s\n", p->itens.v[i].id, p->itens.v[i].qtd, p->itens.v[i].preco, p->itens.v[i].nome);
    ok = fflush(f) == 0 && !ferror(f);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static int proximoNumero(void) {
    int i, max = 0;
    for (i = 0; i < g_hist.n; i++) if (g_hist.v[i].numero > max) max = g_hist.v[i].numero;
    return max + 1;
}

static void erroGravacao(const char *arquivo) {
    char msg[200];
    snprintf(msg, sizeof msg, "Não foi possível gravar o arquivo %s.\nVerifique se a pasta permite gravação.\nNenhuma alteração foi feita.", arquivo);
    tAlerta(MSG_ERRO, "Erro ao salvar", msg);
}

/* ============================ CARRINHO ============================ */

static int carQtd(const Carrinho *c, int id) {
    int i;
    for (i = 0; i < c->n; i++) if (c->v[i].id == id) return c->v[i].qtd;
    return 0;
}

static void carRemoverIdx(Carrinho *c, int i) {
    memmove(&c->v[i], &c->v[i + 1], sizeof(Item) * (size_t)(c->n - i - 1));
    c->n--;
}

/* Define a quantidade do item i (0 remove). -2 = acima do limite. */
static int carDefinirIdx(Carrinho *c, int i, int qtd) {
    if (qtd > MAX_QTD) return -2;
    if (qtd <= 0) { carRemoverIdx(c, i); return 0; }
    c->v[i].qtd = qtd;
    return qtd;
}

/* Soma 'delta' à quantidade do produto; -1 = pedido cheio, -2 = limite. */
static int carAlterar(Carrinho *c, const Produto *p, int delta) {
    int i;
    for (i = 0; i < c->n; i++) if (c->v[i].id == p->id) return carDefinirIdx(c, i, c->v[i].qtd + delta);
    if (delta <= 0) return 0;
    if (c->n == MAX_ITENS) return -1;
    c->v[c->n].id = p->id;
    c->v[c->n].preco = p->preco;
    c->v[c->n].qtd = 0;
    snprintf(c->v[c->n].nome, TAM_NOME, "%s", p->nome);
    c->n++;
    return carDefinirIdx(c, c->n - 1, delta);
}

static Centavos carSubtotal(const Carrinho *c) {
    Centavos s = 0;
    int i;
    for (i = 0; i < c->n; i++) s += c->v[i].preco * c->v[i].qtd;
    return s;
}

static int carUnidades(const Carrinho *c) {
    int i, n = 0;
    for (i = 0; i < c->n; i++) n += c->v[i].qtd;
    return n;
}

/* ========================= COMPONENTES VISUAIS ========================= */

/* Abas de categoria em forma de pílula, clicáveis. */
static void desenharAbas(int x, int y, Categoria cat, int contagem) {
    char t[64];
    int i;
    for (i = 0; i < NUM_CAT; i++) {
        int on = i == (int)cat, w;
        if (g_emoji) snprintf(t, sizeof t, "%s %s", CAT_ICONE[i], CAT_PLURAL[i]);
        else snprintf(t, sizeof t, "%s", CAT_PLURAL[i]);
        if (contagem) snprintf(t + strlen(t), sizeof t - strlen(t), "  %d", cardContar((Categoria)i));
        w = u8Larg(t);
        tCel(x, y, 0x2590, 1, EST(on ? P_AZUL : P_CAMPO, 0));
        tCampo(x + 1, y, w, on ? COR_SEL : EST(P_TEXTO, P_CAMPO), AL_CENTRO, "%s", t);
        tCel(x + w + 1, y, 0x258C, 1, EST(on ? P_AZUL : P_CAMPO, 0));
        tZona(x, y, w + 2, 1, ZN_ABA + i, on ? 0 : COR_HOVER);
        x += w + 3;
    }
}

static void linhaValor(int x, int y, int w, Estilo corR, Estilo corV, const char *rotulo, const char *v) {
    int lr = u8Larg(rotulo);
    tCampo(x, y, w, corR, AL_ESQ, "%s", rotulo);
    tCampo(x + lr + 1, y, w - lr - 1, corV, AL_DIR, "%s", v);
}

/* Botão quadrado pequeno (− / +). */
static void botaozinho(int x, int y, const char *s, int codigo, int cor) {
    tTexto(x, y, EST(cor == P_CAMPO ? P_FORTE : P_ESCURO, cor) | NEGRITO, " %s ", s);
    tZona(x, y, 3, 1, codigo, COR_HOVER);
}

/* =============================== CUPOM =============================== */

static void telaCupom(const Pedido *p, const char *trilha) {
    int w = 50, x, n = p->itens.n, fixas, h, visiveis, topo = 0, t, i, y, yItens;
    char d[16], hr[8], tt[48];
    fixas = 3 + 1 + 1 + (p->desconto > 0) + 1 + 1 + (p->forma == PAG_DINHEIRO ? 2 : 0) + 1 + 1 + 2;
    fmtData(p->data, d, hr);
    for (;;) {
        x = (TELA_L - w) / 2;
        visiveis = n;
        if (fixas + visiveis + 2 > TELA_A - 5) visiveis = TELA_A - 5 - 2 - fixas;
        if (visiveis < 1) visiveis = 1;
        h = fixas + visiveis + 2;
        tTela(trilha, n > visiveis ? "↑↓ Rolar itens|Enter Voltar" : "Enter Voltar");
        tCaixa(x, 2, w, h, COR_ROXO, NULL);
        tSombra(x, 2, w, h);
        y = 3;
        if (g_emoji) { tTexto(x + (w - 25) / 2 - 1, y, COR_TEXTO, "🧾"); tTexto(x + (w + 19) / 2 + 2, y, COR_TEXTO, "🧾"); }
        tTextoDegrade(x + (w - 19) / 2, y++, "L A N C H O N E T E", PALETA[P_ACENTO], PALETA[P_ROXO], 1);
        (void)tt;
        tCampo(x + 2, y++, w - 4, COR_FRACO, AL_CENTRO, "Pedido nº %04d  ·  %s às %s", p->numero, d, hr);
        tRepete(x + 2, y++, w - 4, COR_BORDA, "┄");
        yItens = y;
        y += visiveis;
        tRepete(x + 2, y++, w - 4, COR_BORDA, "┄");
        linhaValor(x + 2, y++, w - 4, COR_TEXTO, COR_TEXTO, "Subtotal", moeda(p->subtotal));
        if (p->desconto > 0) {
            char dsc[40];
            snprintf(dsc, sizeof dsc, "− %s", moeda(p->desconto));
            linhaValor(x + 2, y++, w - 4, COR_OK, COR_OK, "Desconto dinheiro (10%)", dsc);
        }
        tPreenche(x + 1, y, w - 2, 1, COR_SEL);
        linhaValor(x + 3, y++, w - 6, COR_SEL, COR_SEL, "TOTAL", moeda(p->total));
        {
            char fp[40];
            snprintf(fp, sizeof fp, "%s%s", g_emoji ? PAG_ICONE[p->forma] : "", PAG_NOME[p->forma]);
            if (g_emoji) snprintf(fp, sizeof fp, "%s %s", PAG_ICONE[p->forma], PAG_NOME[p->forma]);
            linhaValor(x + 2, y++, w - 4, COR_TEXTO, COR_TEXTO, "Pagamento", fp);
        }
        if (p->forma == PAG_DINHEIRO) {
            linhaValor(x + 2, y++, w - 4, COR_TEXTO, COR_TEXTO, "Valor recebido", moeda(p->recebido));
            linhaValor(x + 2, y++, w - 4, COR_FORTE, COR_OK | NEGRITO, "Troco", moeda(p->recebido - p->total));
        }
        tRepete(x + 2, y++, w - 4, COR_BORDA, "┄");
        tCampo(x + 2, y++, w - 4, COR_ACENTO, AL_CENTRO, "Obrigado pela preferência! %s", ic("💜", ""));
        tBotao(x + (w - 12) / 2, y + 1, "    OK    ", TK_ENTER, P_AZUL, 1);
        for (;;) {
            for (i = 0; i < visiveis && topo + i < n; i++) {
                const Item *it = &p->itens.v[topo + i];
                tCampo(x + 2, yItens + i, 32, COR_TEXTO, AL_ESQ, "%2d× %s", it->qtd, it->nome);
                tCampo(x + 34, yItens + i, w - 37, COR_TEXTO, AL_DIR, "%s", valor(it->preco * it->qtd));
            }
            if (n > visiveis) tBarraRolagem(x + w - 2, yItens, visiveis, topo, n);
            t = tTecla();
            if (t == TK_REDIM) break;
            if (t == TK_CIMA && topo > 0) topo--;
            else if (t == TK_BAIXO && topo + visiveis < n) topo++;
            else if (t == TK_ENTER || t == TK_ESC) return;
        }
    }
}

/* ============================= PAGAMENTO ============================= */

static void desenharResumo(int w, const Carrinho *car, Pagamento f) {
    Centavos sub = carSubtotal(car), desc = calcDesconto(sub, f);
    int i, base = TELA_A - 5, maxItens = base - 6 - 5, lim, wn = w - 4 - 5 - 10 - 11;
    char d[40], t[48];
    lim = car->n > maxItens ? maxItens - 1 : car->n;
    tCaixa(0, 2, w, TELA_A - 5, COR_BORDA, tit(t, sizeof t, "🧾", "Resumo do pedido"));
    tCampo(2, 3, wn, COR_FRACO, AL_ESQ, "Produto");
    tCampo(2 + wn, 3, 5, COR_FRACO, AL_DIR, "Qtd");
    tCampo(7 + wn, 3, 10, COR_FRACO, AL_DIR, "Unit.");
    tCampo(17 + wn, 3, 11, COR_FRACO, AL_DIR, "Total");
    tDivisoria(0, 4, w, COR_BORDA);
    for (i = 0; i < lim; i++) {
        const Item *it = &car->v[i];
        tCampo(2, 5 + i, wn, COR_TEXTO, AL_ESQ, "%s", it->nome);
        tCampo(2 + wn, 5 + i, 5, COR_ACENTO, AL_DIR, "%d", it->qtd);
        tCampo(7 + wn, 5 + i, 10, COR_FRACO, AL_DIR, "%s", valor(it->preco));
        tCampo(17 + wn, 5 + i, 11, COR_TEXTO, AL_DIR, "%s", valor(it->preco * it->qtd));
    }
    if (car->n > lim) tTexto(2, 5 + lim, COR_FRACO, "… e mais %d produto(s)", car->n - lim);
    tDivisoria(0, base - 4, w, COR_BORDA);
    linhaValor(2, base - 3, w - 4, COR_TEXTO, COR_TEXTO, "Subtotal", moeda(sub));
    if (desc > 0) {
        snprintf(d, sizeof d, "− %s", moeda(desc));
        linhaValor(2, base - 2, w - 4, COR_OK, COR_OK, "Desconto dinheiro (10%)", d);
    } else {
        linhaValor(2, base - 2, w - 4, COR_FRACO, COR_FRACO, "Desconto", "—");
    }
    tPreenche(1, base - 1, w - 2, 1, COR_SEL);
    linhaValor(3, base - 1, w - 6, COR_SEL, COR_SEL, "TOTAL A PAGAR", moeda(sub - desc));
    tCampo(2, base, w - 4, COR_FRACO, AL_ESQ, "%d unidade(s) de %d produto(s)", carUnidades(car), car->n);
}

static void desenharFormas(int x, int w, Pagamento f, Centavos sub) {
    int i;
    char t[48];
    tCaixa(x, 2, w, TELA_A - 5, COR_ACENTO, tit(t, sizeof t, "💰", "Pagamento"));
    for (i = 0; i < NUM_PAG; i++) {
        int y = 3 + i * 4, on = i == (int)f;
        Estilo borda = on ? COR_AZUL : COR_BORDA;
        tTexto(x + 1, y, borda, "╭");
        tRepete(x + 2, y, w - 4, borda, "─");
        tTexto(x + w - 2, y, borda, "╮");
        tTexto(x + 1, y + 1, borda, "│");
        tTexto(x + w - 2, y + 1, borda, "│");
        tTexto(x + 1, y + 2, borda, "│");
        tTexto(x + w - 2, y + 2, borda, "│");
        tTexto(x + 1, y + 3, borda, "╰");
        tRepete(x + 2, y + 3, w - 4, borda, "─");
        tTexto(x + w - 2, y + 3, borda, "╯");
        tPreenche(x + 2, y + 1, w - 4, 2, on ? EST(P_FORTE, P_CAMPO) : COR_TEXTO);
        tTexto(x + 3, y + 1, on ? EST(P_FORTE, P_CAMPO) | NEGRITO : COR_FORTE, "%s %s",
               ic(PAG_ICONE[i], PAG_ALT[i]), PAG_NOME[i]);
        tTexto(x + w - 5, y + 1, on ? EST(P_AZUL, P_CAMPO) : COR_BORDA, on ? "●" : "○");
        tCampo(x + 6, y + 2, w - 9, on ? EST(i == PAG_DINHEIRO ? P_OK : P_TEXTO, P_CAMPO) : (i == PAG_DINHEIRO ? COR_OK : COR_FRACO),
               AL_ESQ, "%s", PAG_DESC[i]);
        tZona(x + 1, y, w - 2, 4, ZN_FORMA + i, 0);
    }
    tPreenche(x + 1, 15, w - 2, TELA_A - 5 - 13 - 2, COR_TEXTO);
    if (f == PAG_DINHEIRO) {
        tTexto(x + 3, 15, COR_TEXTO, "Você economiza");
        tTexto(x + 3, 16, COR_OK | NEGRITO, "%s", moeda(calcDesconto(sub, f)));
    }
    tBotao(x + (w - 25) / 2, TELA_A - 5, "✓ Confirmar pagamento", ZN_CONFIRMA, P_OK, 1);
}

/* Lê o valor entregue em dinheiro. Retorna 0 se o usuário voltar. */
static int lerDinheiro(int x, int w, Centavos total, Centavos *recebido) {
    double v;
    char msg[96];
    tPreenche(x + 1, 15, w - 2, 3, COR_TEXTO);
    tTexto(x + 3, 15, COR_TEXTO, "Valor recebido (R$)");
    tRodape("Enter Confirmar|Esc Voltar");
    tStatus(MSG_INFO, "Digite quanto o cliente entregou em dinheiro (mínimo %s).", moeda(total));
    snprintf(msg, sizeof msg, "Valor insuficiente: o total é %s.", moeda(total));
    for (;;) {
        if (!tLerReal(x + 3, 16, w - 6, "", 0, 0, 99999.99, "Informe um valor de até R$ 99.999,99.", &v)) return 0;
        *recebido = paraCentavos(v);
        if (*recebido >= total) break;
        tStatus(MSG_ERRO, "%s", msg);
    }
    tTexto(x + 3, 17, COR_TEXTO, "Troco: ");
    tTexto(x + 10, 17, COR_OK | NEGRITO, "%s", moeda(*recebido - total));
    return 1;
}

static int telaPagamento(const Carrinho *car) {
    Pagamento f = PAG_DINHEIRO;
    Pedido ped;
    Centavos sub = carSubtotal(car), desc, total, receb;
    char msg[256];
    int t, salvo, ws, xp, wp;

    for (;;) {
        ws = TELA_L * 58 / 100;
        xp = ws + 2;
        wp = TELA_L - xp;
        tTela("Início › Novo pedido › Pagamento", "↑↓ Forma de pagamento|Enter Confirmar|Esc Voltar ao pedido");
        tStatus(MSG_INFO, "Escolha a forma de pagamento. Em dinheiro, 10%% de desconto na hora.");
        for (;;) {
            desenharResumo(ws, car, f);
            desenharFormas(xp, wp, f, sub);
            t = tTecla();
            if (t == TK_REDIM) break;
            if (t == TK_CIMA)  f = (Pagamento)((f + NUM_PAG - 1) % NUM_PAG);
            else if (t == TK_BAIXO || t == TK_TAB) f = (Pagamento)((f + 1) % NUM_PAG);
            else if (t >= '1' && t <= '3') f = (Pagamento)(t - '1');
            else if (t >= ZN_FORMA && t < ZN_FORMA + NUM_PAG) {
                Pagamento nf = (Pagamento)(t - ZN_FORMA);
                if (nf == f && g_duplo) t = TK_ENTER;
                f = nf;
                if (t != TK_ENTER) continue;
            }
            if (t == TK_ESC) return 0;
            if (t == TK_ENTER || t == ZN_CONFIRMA) {
                desc = calcDesconto(sub, f);
                total = sub - desc;
                receb = total;
                desenharResumo(ws, car, f);
                desenharFormas(xp, wp, f, sub);
                if (f == PAG_DINHEIRO && !lerDinheiro(xp, wp, total, &receb)) {
                    tRodape("↑↓ Forma de pagamento|Enter Confirmar|Esc Voltar ao pedido");
                    continue;
                }
                if (f == PAG_DINHEIRO)
                    snprintf(msg, sizeof msg, "Total %s em %s · troco %s\nDeseja registrar este pedido?",
                             moeda(total), PAG_NOME[f], moeda(receb - total));
                else
                    snprintf(msg, sizeof msg, "Total %s no %s\nDeseja registrar este pedido?", moeda(total), PAG_NOME[f]);
                if (!tConfirmar("Confirmar pedido", msg, 1)) {
                    tRodape("↑↓ Forma de pagamento|Enter Confirmar|Esc Voltar ao pedido");
                    continue;
                }
                memset(&ped, 0, sizeof ped);
                ped.numero = proximoNumero();
                dataISO(time(NULL), ped.data, sizeof ped.data);
                ped.forma = f;
                ped.subtotal = sub;
                ped.desconto = desc;
                ped.total = total;
                ped.recebido = receb;
                ped.itens = *car;
                tProgresso(g_emoji ? "💾 Registrando pedido" : "Registrando pedido", 25);
                salvo = 0;
                for (;;) {
                    if (salvarPedido(&ped)) { salvo = 1; break; }
                    if (!tConfirmar("Erro ao salvar", "Não foi possível gravar o arquivo pedidos.txt.\nVerifique se a pasta permite gravação.\nTentar novamente?", 1))
                        break;
                }
                if (!salvo) { tStatus(MSG_ERRO, "O pedido NÃO foi registrado. Revise e tente novamente."); continue; }
                if (!histAdicionar(&ped))
                    tAlerta(MSG_AVISO, "Memória", "O pedido foi gravado, mas só aparecerá\nno histórico na próxima execução.");
                tAvisoPendente(MSG_OK, "Pedido nº %04d registrado com sucesso!", ped.numero);
                telaCupom(&ped, "Início › Novo pedido › Cupom");
                return 1;
            }
        }
    }
}

/* ============================ NOVO PEDIDO ============================ */

#define RODAPE_CARDAPIO "←→ Categoria|↑↓ Produto|Enter Adicionar|F Finalizar|Esc Sair"
#define RODAPE_PEDIDO   "↑↓ Item|+ Mais|- Menos|Del Remover|Tab Cardápio|F Finalizar"

static int g_wc;                       /* largura do painel do cardápio */

static void pdvCatalogo(Categoria cat, int n, int sel, int topo, int focado, const Carrinho *car, int alt) {
    int i, w = g_wc, xc = w - 13, wn = xc - 17;
    char t[48];
    tCaixa(0, 2, w, TELA_A - 5, focado ? COR_ACENTO : COR_BORDA, tit(t, sizeof t, "🍴", "Cardápio"));
    desenharAbas(2, 3, cat, 0);
    tDivisoria(0, 4, w, focado ? COR_ACENTO : COR_BORDA);
    if (n == 0) {
        tCampo(1, 9, w - 2, COR_FRACO, AL_CENTRO, "Nenhum produto nesta categoria.");
        tCampo(1, 10, w - 2, COR_FRACO, AL_CENTRO, "Cadastre produtos na opção Cardápio.");
        return;
    }
    for (i = 0; i < alt; i++) {
        const Produto *p;
        Estilo c;
        int q, y = 5 + i, s;
        tPreenche(1, y, w - 2, 1, COR_TEXTO);
        if (topo + i >= n) continue;
        p = &g_card.v[g_idx[topo + i]];
        q = carQtd(car, p->id);
        s = focado && topo + i == sel;
        c = s ? COR_SEL : COR_TEXTO;
        if (s) tPreenche(1, y, w - 2, 1, COR_SEL);
        tTexto(2, y, c, "%s", ic(CAT_ICONE[p->cat], CAT_ALT[p->cat]));
        tCampo(5, y, wn, c, AL_ESQ, "%s", p->nome);
        tCampo(xc - 11, y, 10, s ? c : COR_FRACO, AL_DIR, "%s", moeda(p->preco));
        tZona(1, y, xc - 1, 1, ZN_LINHA + topo + i, 0);
        if (q) {
            botaozinho(xc, y, "−", ZN_MENOS + topo + i, P_CAMPO);
            tCampo(xc + 3, y, 4, s ? c : COR_ACENTO | NEGRITO, AL_CENTRO, "%d", q);
        } else {
            tCampo(xc, y, 7, c, AL_ESQ, "");
        }
        botaozinho(xc + 7, y, "+", ZN_MAIS + topo + i, P_AZUL);
    }
    tBarraRolagem(w - 2, 5, alt, topo, n);
}

static void pdvPedido(const Carrinho *car, int sel, int topo, int focado, int alt) {
    Centavos sub = carSubtotal(car);
    int i, x = g_wc + 1, w = TELA_L - g_wc - 1, yb = TELA_A - 4;
    char u[16], t[48];
    tCaixa(x, 2, w, TELA_A - 5, focado ? COR_ACENTO : COR_BORDA, tit(t, sizeof t, "🛒", "Seu pedido"));
    if (car->n == 0) {
        tCampo(x + 1, 6, w - 2, COR_FRACO, AL_CENTRO, "Pedido vazio");
        tCampo(x + 1, 8, w - 2, COR_FRACO, AL_CENTRO, "Clique em + ao lado de");
        tCampo(x + 1, 9, w - 2, COR_FRACO, AL_CENTRO, "um produto para adicionar.");
    }
    for (i = 0; i < alt && topo + i < car->n; i++) {
        const Item *it = &car->v[topo + i];
        int s = focado && topo + i == sel;
        Estilo c = s ? COR_SEL : COR_TEXTO;
        tPreenche(x + 1, 3 + i, w - 3, 1, c);
        tCampo(x + 2, 3 + i, w - 15, c, AL_ESQ, "%2d× %s", it->qtd, it->nome);
        tCampo(x + w - 13, 3 + i, 10, c, AL_DIR, "%s", valor(it->preco * it->qtd));
        tZona(x + 1, 3 + i, w - 3, 1, ZN_CARR + topo + i, 0);
    }
    tBarraRolagem(x + w - 2, 3, alt, topo, car->n);
    if (car->n > 0) {                                /* ações do item selecionado */
        int bx = x + 2;
        bx += tBotao(bx, yb - 7, "−", ZN_CMENOS, P_CAMPO, 1) + 1;
        bx += tBotao(bx, yb - 7, "+", ZN_CMAIS, P_CAMPO, 1) + 1;
        tBotao(bx, yb - 7, "✕ Remover", ZN_CREMOVE, P_ERRO, 1);
    }
    tDivisoria(x, yb - 6, w, focado ? COR_ACENTO : COR_BORDA);
    snprintf(u, sizeof u, "%d", carUnidades(car));
    linhaValor(x + 2, yb - 5, w - 4, COR_TEXTO, COR_TEXTO, "Itens", u);
    linhaValor(x + 2, yb - 4, w - 4, COR_TEXTO, COR_FORTE, "Subtotal", moeda(sub));
    if (sub > 0) linhaValor(x + 2, yb - 3, w - 4, COR_OK, COR_OK, "No dinheiro (−10%)", moeda(sub - calcDesconto(sub, PAG_DINHEIRO)));
    {
        const char *rot = car->n ? "✓ Finalizar pedido" : "Pedido vazio";
        tBotao(x + (w - u8Larg(rot) - 4) / 2, yb - 1, rot, ZN_FINALIZA, P_OK, car->n > 0);
    }
}

static void avisoQuantidade(const char *nome, int q) {
    if (q == -1)      tStatus(MSG_ERRO, "O pedido chegou ao limite de %d produtos diferentes.", MAX_ITENS);
    else if (q == -2) tStatus(MSG_AVISO, "A quantidade máxima por produto é %d.", MAX_QTD);
    else if (q == 0)  tStatus(MSG_INFO, "“%s” retirado do pedido.", nome);
    else              tStatus(MSG_OK, "“%s”: %d no pedido.", nome, q);
}

static void telaNovoPedido(void) {
    Carrinho car;
    Categoria cat = CAT_SANDUICHE;
    int sel = 0, topo = 0, foco = 0, selC = 0, topoC = 0, n, t, q, altCat, altPed;
    char nome[TAM_NOME];
    memset(&car, 0, sizeof car);

    for (;;) {                                       /* redesenho completo */
        g_wc = TELA_L * 58 / 100;
        if (g_wc < 46) g_wc = 46;
        altCat = TELA_A - 5 - 4;
        altPed = TELA_A - 4 - 7 - 3;
        tTela("Início › Novo pedido", foco ? RODAPE_PEDIDO : RODAPE_CARDAPIO);
        if (car.n == 0) tStatus(MSG_INFO, "Clique em + para adicionar, ou use as setas e Enter. As abas trocam a categoria.");
        for (;;) {
            n = cardFiltrar(cat);
            if (sel >= n) sel = n ? n - 1 : 0;
            tRolagem(sel, &topo, altCat, n);
            if (selC >= car.n) selC = car.n ? car.n - 1 : 0;
            tRolagem(selC, &topoC, altPed, car.n);
            pdvCatalogo(cat, n, sel, topo, !foco, &car, altCat);
            pdvPedido(&car, selC, topoC, foco, altPed);

            t = tTecla();
            if (t == TK_REDIM) break;
            if (t >= ZN_ABA && t < ZN_ABA + NUM_CAT) { cat = (Categoria)(t - ZN_ABA); sel = topo = 0; foco = 0; tRodape(RODAPE_CARDAPIO); continue; }
            if (t >= ZN_LINHA && t < ZN_LINHA + 0x200) {
                int novo = t - ZN_LINHA;
                if (novo >= n) continue;
                if (foco) { foco = 0; tRodape(RODAPE_CARDAPIO); }
                if (novo == sel && g_duplo) t = TK_ENTER; else { sel = novo; continue; }
            }
            if (t >= ZN_MAIS && t < ZN_MAIS + 0x200) {
                int k = t - ZN_MAIS;
                if (k < n) { sel = k; avisoQuantidade(g_card.v[g_idx[k]].nome, carAlterar(&car, &g_card.v[g_idx[k]], 1)); }
                continue;
            }
            if (t >= ZN_MENOS && t < ZN_MENOS + 0x200) {
                int k = t - ZN_MENOS;
                if (k < n) { sel = k; avisoQuantidade(g_card.v[g_idx[k]].nome, carAlterar(&car, &g_card.v[g_idx[k]], -1)); }
                continue;
            }
            if (t >= ZN_CARR && t < ZN_CARR + MAX_ITENS) {
                if (t - ZN_CARR < car.n) { selC = t - ZN_CARR; if (!foco) { foco = 1; tRodape(RODAPE_PEDIDO); } }
                continue;
            }
            if (t == ZN_CMAIS || t == ZN_CMENOS || t == ZN_CREMOVE) {   /* botões do carrinho */
                t = t == ZN_CMAIS ? '+' : t == ZN_CMENOS ? '-' : TK_DEL;
                if (!foco) { foco = 1; tRodape(RODAPE_PEDIDO); }
            }
            if (t == 'f' || t == 'F' || t == ZN_FINALIZA) {
                if (car.n == 0) { tStatus(MSG_AVISO, "Adicione pelo menos um produto antes de finalizar."); continue; }
                if (telaPagamento(&car)) return;
                break;
            }
            if (t == TK_TAB) {
                if (!foco && car.n == 0) { tStatus(MSG_AVISO, "O pedido ainda está vazio."); continue; }
                foco = !foco;
                tRodape(foco ? RODAPE_PEDIDO : RODAPE_CARDAPIO);
                continue;
            }
            if (t == TK_ESC) {
                if (foco) { foco = 0; tRodape(RODAPE_CARDAPIO); continue; }
                if (car.n == 0) return;
                if (tConfirmar("Cancelar pedido", "Os itens escolhidos serão descartados.\nDeseja cancelar este pedido?", 0)) {
                    tAvisoPendente(MSG_INFO, "Pedido cancelado.");
                    return;
                }
                continue;
            }
            if (!foco) {                             /* painel do cardápio */
                if (t == TK_ESQ)      { cat = (Categoria)((cat + NUM_CAT - 1) % NUM_CAT); sel = topo = 0; }
                else if (t == TK_DIR) { cat = (Categoria)((cat + 1) % NUM_CAT); sel = topo = 0; }
                else if (t == TK_CIMA && sel > 0) sel--;
                else if (t == TK_BAIXO && sel < n - 1) sel++;
                else if (t == TK_PGCIMA)  { sel -= altCat; if (sel < 0) sel = 0; }
                else if (t == TK_PGBAIXO) { sel += altCat; if (sel > n - 1) sel = n > 0 ? n - 1 : 0; }
                else if ((t == TK_ENTER || t == '+') && n > 0)
                    avisoQuantidade(g_card.v[g_idx[sel]].nome, carAlterar(&car, &g_card.v[g_idx[sel]], 1));
                else if (t == '-' && n > 0) {
                    const Produto *p = &g_card.v[g_idx[sel]];
                    if (carQtd(&car, p->id) == 0) tStatus(MSG_AVISO, "“%s” não está no pedido.", p->nome);
                    else avisoQuantidade(p->nome, carAlterar(&car, p, -1));
                }
            } else if (car.n > 0) {                  /* painel do pedido */
                if (t == TK_CIMA && selC > 0) selC--;
                else if (t == TK_BAIXO && selC < car.n - 1) selC++;
                else if (t == '+' || t == '-') {
                    snprintf(nome, sizeof nome, "%s", car.v[selC].nome);
                    q = carDefinirIdx(&car, selC, car.v[selC].qtd + (t == '+' ? 1 : -1));
                    avisoQuantidade(nome, q);
                } else if (t == TK_DEL) {
                    snprintf(nome, sizeof nome, "%s", car.v[selC].nome);
                    carRemoverIdx(&car, selC);
                    avisoQuantidade(nome, 0);
                } else if (t == TK_ENTER) {
                    long v;
                    char ini[8];
                    snprintf(ini, sizeof ini, "%d", car.v[selC].qtd);
                    snprintf(nome, sizeof nome, "%s", car.v[selC].nome);
                    tStatus(MSG_INFO, "Digite a nova quantidade (0 remove) e tecle Enter.");
                    if (tLerInt(g_wc + 3, 3 + selC - topoC, 4, ini, 1, 0, MAX_QTD, &v))
                        avisoQuantidade(nome, carDefinirIdx(&car, selC, (int)v));
                    else tStatus(MSG_LIMPA, "");
                }
                if (car.n == 0) { foco = 0; tRodape(RODAPE_CARDAPIO); }
            }
        }
    }
}

/* ======================== GESTÃO DO CARDÁPIO ======================== */

static void desenharChips(int x, int y, Categoria cat, int ativo) {
    int i;
    char t[48];
    for (i = 0; i < NUM_CAT; i++) {
        int on = i == (int)cat, w;
        if (g_emoji) snprintf(t, sizeof t, "%s %s", CAT_ICONE[i], CAT_NOME[i]);
        else snprintf(t, sizeof t, "%s", CAT_NOME[i]);
        w = u8Larg(t) + 2;
        tCel(x, y, 0x2590, 1, EST(on ? (ativo ? P_AZUL : P_ACENTO) : P_CAMPO, 0));
        tCampo(x + 1, y, w, on ? EST(P_ESCURO, ativo ? P_AZUL : P_ACENTO) | NEGRITO : EST(P_TEXTO, P_CAMPO), AL_CENTRO, "%s", t);
        tCel(x + w + 1, y, 0x258C, 1, EST(on ? (ativo ? P_AZUL : P_ACENTO) : P_CAMPO, 0));
        tZona(x, y, w + 2, 1, ZN_CHIP + i, on ? 0 : COR_HOVER);
        x += w + 3;
    }
}

/* Formulário de produto (pos = -1 para novo). Retorna o id salvo ou 0. */
static int formProduto(int pos, Categoria catPadrao) {
    Produto p;
    Categoria cat;
    char nome[TAM_NOME], preco[32], msg[256], tt[48];
    double v;
    int w = 68, h = 14, x = (TELA_L - 68) / 2, y = (TELA_A - 14) / 2 - 1, t;
    Recorte r;

    if (pos >= 0) p = g_card.v[pos];
    else { memset(&p, 0, sizeof p); p.cat = catPadrao; }
    snprintf(nome, sizeof nome, "%s", p.nome);
    cat = p.cat;
    if (pos >= 0) snprintf(preco, sizeof preco, "%ld,%02ld", p.preco / 100, p.preco % 100);
    else preco[0] = '\0';

    tSalvar(&r, x, y, w + 1, h + 1);
    tCaixa(x, y, w, h, COR_ACENTO, pos >= 0 ? tit(tt, sizeof tt, "📝", "Editar produto") : tit(tt, sizeof tt, "🆕", "Novo produto"));
    tSombra(x, y, w, h);
    tTexto(x + 3, y + 2, COR_TEXTO, "Nome");
    tTexto(x + 3, y + 4, COR_TEXTO, "Categoria");
    tTexto(x + 3, y + 6, COR_TEXTO, "Preço (R$)");
    tCampo(x + 16, y + 2, w - 19, COR_CAMPO, AL_ESQ, " %s", nome);
    desenharChips(x + 16, y + 4, cat, 0);
    tCampo(x + 16, y + 6, 14, COR_CAMPO, AL_ESQ, " %s", preco);
    tTexto(x + 3, y + 8, COR_FRACO, "Nome com até %d letras · preço até R$ 9.999,99", MAX_NOME);
    tBotao(x + w - 35, y + h - 3, " Cancelar ", TK_ESC, P_CAMPO, 1);
    tBotao(x + w - 20, y + h - 3, "  Avançar ›  ", TK_ENTER, P_AZUL, 1);

    tRodape("Enter Avançar|Esc Cancelar");
    tStatus(MSG_INFO, "1/3 · Digite o nome do produto.");
    for (;;) {                                       /* 1. nome */
        if (!tEditar(x + 16, y + 2, w - 19, nome, sizeof nome, MAX_NOME, FILTRO_TEXTO, 0)) goto cancelar;
        aparar(nome);
        if (!nome[0]) tStatus(MSG_ERRO, "O nome não pode ficar vazio.");
        else if (strchr(nome, ';')) tStatus(MSG_ERRO, "O nome não pode conter ponto e vírgula (;).");
        else if (cardNomeEmUso(nome, pos >= 0 ? p.id : 0)) tStatus(MSG_ERRO, "Já existe um produto chamado “%s”.", nome);
        else break;
    }
    tCampo(x + 16, y + 2, w - 19, COR_CAMPO, AL_ESQ, " %s", nome);

    tRodape("←→ Escolher|Enter Avançar|Esc Cancelar");
    tStatus(MSG_INFO, "2/3 · Clique na categoria (ou use ← →) e tecle Enter.");
    for (;;) {                                       /* 2. categoria */
        desenharChips(x + 16, y + 4, cat, 1);
        t = tTecla();
        if (t == TK_ESQ) cat = (Categoria)((cat + NUM_CAT - 1) % NUM_CAT);
        else if (t == TK_DIR || t == TK_TAB) cat = (Categoria)((cat + 1) % NUM_CAT);
        else if (t >= '1' && t <= '3') cat = (Categoria)(t - '1');
        else if (t >= ZN_CHIP && t < ZN_CHIP + NUM_CAT) cat = (Categoria)(t - ZN_CHIP);
        else if (t == TK_ENTER) break;
        else if (t == TK_ESC) goto cancelar;
    }
    desenharChips(x + 16, y + 4, cat, 0);

    tBotao(x + w - 20, y + h - 3, "   Salvar ✓  ", TK_ENTER, P_OK, 1);
    tRodape("Enter Salvar|Esc Cancelar");
    tStatus(MSG_INFO, "3/3 · Digite o preço (ex.: 18,50) e tecle Enter.");
    if (!tLerReal(x + 16, y + 6, 14, preco, pos >= 0, 0.01, 9999.99,
                  "O preço deve ficar entre R$ 0,01 e R$ 9.999,99.", &v)) goto cancelar;

    snprintf(msg, sizeof msg, "%s\n%s  ·  %s\n%s", nome, CAT_NOME[cat], moeda(paraCentavos(v)),
             pos >= 0 ? "Salvar as alterações?" : "Cadastrar este produto?");
    if (!tConfirmar(pos >= 0 ? "Confirmar alteração" : "Confirmar cadastro", msg, 1)) goto cancelar;
    tRestaurar(&r);

    snprintf(p.nome, sizeof p.nome, "%s", nome);
    p.cat = cat;
    p.preco = paraCentavos(v);
    if (pos < 0) {
        p.id = cardProximoId();
        if (!cardInserir(g_card.n, &p)) { tAlerta(MSG_ERRO, "Memória insuficiente", "Não foi possível cadastrar o produto."); return 0; }
        if (!salvarCardapio()) { cardRemover(g_card.n - 1); erroGravacao(ARQ_PRODUTOS); return 0; }
        tAvisoPendente(MSG_OK, "“%s” cadastrado em %s.", p.nome, CAT_PLURAL[p.cat]);
    } else {
        Produto antigo = g_card.v[pos];
        g_card.v[pos] = p;
        if (!salvarCardapio()) { g_card.v[pos] = antigo; erroGravacao(ARQ_PRODUTOS); return 0; }
        tAvisoPendente(MSG_OK, "“%s” atualizado.", p.nome);
    }
    return p.id;

cancelar:
    tRestaurar(&r);
    tAvisoPendente(MSG_INFO, "Operação cancelada.");
    return 0;
}

static void excluirProduto(int pos) {
    Produto removido = g_card.v[pos];
    char msg[200];
    snprintf(msg, sizeof msg, "Excluir “%s”?\nPedidos já registrados não são afetados.", removido.nome);
    if (!tConfirmar("Excluir produto", msg, 0)) return;
    cardRemover(pos);
    if (!salvarCardapio()) {
        cardInserir(pos, &removido);
        erroGravacao(ARQ_PRODUTOS);
        return;
    }
    tStatus(MSG_OK, "“%s” excluído do cardápio.", removido.nome);
}

static void telaCardapio(void) {
    Categoria cat = CAT_SANDUICHE;
    int sel = 0, topo = 0, n, t, i, id, alt, w, wn, xCat, xPreco;
    char tt[48];
    for (;;) {
        w = TELA_L;
        alt = TELA_A - 12;
        wn = w - 4 - 9 - 16 - 14 - 3;
        xCat = 12 + wn;
        xPreco = xCat + 16;
        tTela("Início › Cardápio", "←→ Categoria|N Novo|Enter Editar|Del Excluir|Esc Voltar");
        tCaixa(0, 2, w, TELA_A - 5, COR_BORDA, tit(tt, sizeof tt, "📋", "Cardápio"));
        tDivisoria(0, 4, w, COR_BORDA);
        tCampo(3, 5, 8, COR_FRACO, AL_ESQ, "Código");
        tCampo(12, 5, wn, COR_FRACO, AL_ESQ, "Produto");
        tCampo(xCat, 5, 14, COR_FRACO, AL_ESQ, "Categoria");
        tCampo(xPreco, 5, 13, COR_FRACO, AL_DIR, "Preço");
        tDivisoria(0, TELA_A - 6, w, COR_BORDA);
        for (;;) {
            n = cardFiltrar(cat);
            if (sel >= n) sel = n ? n - 1 : 0;
            tRolagem(sel, &topo, alt, n);
            desenharAbas(2, 3, cat, 1);
            for (i = 0; i < alt; i++) {
                int row = 6 + i;
                tPreenche(1, row, w - 3, 1, COR_TEXTO);
                if (topo + i < n) {
                    const Produto *p = &g_card.v[g_idx[topo + i]];
                    int s = topo + i == sel;
                    Estilo c = s ? COR_SEL : COR_TEXTO;
                    if (s) tPreenche(1, row, w - 3, 1, COR_SEL);
                    tTexto(3, row, c, "%s", ic(CAT_ICONE[p->cat], CAT_ALT[p->cat]));
                    tCampo(6, row, 5, s ? c : COR_FRACO, AL_ESQ, "#%02d", p->id);
                    tCampo(12, row, wn, c, AL_ESQ, "%s", p->nome);
                    tCampo(xCat, row, 14, s ? c : EST(CAT_COR[p->cat], 0), AL_ESQ, "%s", CAT_NOME[p->cat]);
                    tCampo(xPreco, row, 13, s ? c : COR_FORTE, AL_DIR, "%s", moeda(p->preco));
                    tZona(1, row, w - 3, 1, ZN_LINHA + topo + i, 0);
                }
            }
            if (n == 0) {
                tCampo(1, 6 + alt / 2 - 1, w - 3, COR_FRACO, AL_CENTRO, "Nenhum produto em %s.", CAT_PLURAL[cat]);
                tCampo(1, 6 + alt / 2, w - 3, COR_FRACO, AL_CENTRO, "Clique em “Novo produto” para cadastrar o primeiro.");
            }
            tBarraRolagem(w - 2, 6, alt, topo, n);
            {
                int bx = 2, yb = TELA_A - 5;
                bx += tBotao(bx, yb, g_emoji ? " 🆕 Novo produto " : " + Novo produto ", ZN_NOVO, P_OK, 1) + 2;
                bx += tBotao(bx, yb, g_emoji ? " 📝 Editar " : " Editar ", ZN_EDITAR, P_AZUL, n > 0) + 2;
                tBotao(bx, yb, " ✕ Excluir ", ZN_EXCLUIR, P_ERRO, n > 0);
                tCampo(w - 22, yb, 20, COR_FRACO, AL_DIR, "%d no total", g_card.n);
            }

            t = tTecla();
            if (t == TK_REDIM) break;
            if (t >= ZN_ABA && t < ZN_ABA + NUM_CAT) { cat = (Categoria)(t - ZN_ABA); sel = topo = 0; continue; }
            if (t >= ZN_LINHA && t < ZN_LINHA + 0x200) {
                int k = t - ZN_LINHA;
                if (k >= n) continue;
                if (k == sel && g_duplo) t = ZN_EDITAR; else { sel = k; continue; }
            }
            if (t == ZN_NOVO) t = 'n';
            if (t == ZN_EDITAR) t = 'e';
            if (t == ZN_EXCLUIR) t = TK_DEL;
            if (t == TK_ESQ)      { cat = (Categoria)((cat + NUM_CAT - 1) % NUM_CAT); sel = topo = 0; }
            else if (t == TK_DIR) { cat = (Categoria)((cat + 1) % NUM_CAT); sel = topo = 0; }
            else if (t == TK_CIMA && sel > 0) sel--;
            else if (t == TK_BAIXO && sel < n - 1) sel++;
            else if (t == TK_PGCIMA)  { sel -= alt; if (sel < 0) sel = 0; }
            else if (t == TK_PGBAIXO) { sel += alt; if (sel > n - 1) sel = n > 0 ? n - 1 : 0; }
            else if (t == TK_ESC) return;
            else if (t == 'n' || t == 'N' || ((t == TK_ENTER || t == 'e' || t == 'E') && n > 0)) {
                id = formProduto((t == 'n' || t == 'N') ? -1 : g_idx[sel], cat);
                if (id) {
                    int pos = cardBuscar(id), k;
                    cat = g_card.v[pos].cat;
                    n = cardFiltrar(cat);
                    for (k = 0; k < n; k++) if (g_idx[k] == pos) sel = k;
                }
                break;
            } else if ((t == TK_DEL || t == 'x' || t == 'X') && n > 0) {
                excluirProduto(g_idx[sel]);
            }
        }
    }
}

/* ============================ HISTÓRICO ============================ */

static void cartao(int x, int w, const char *emoji, const char *rotulo, const char *v, Estilo cor) {
    char t[64];
    tCaixa(x, 2, w, 4, COR_BORDA, NULL);
    tCampo(x + 2, 3, w - 4, COR_FRACO, AL_ESQ, "%s", tit(t, sizeof t, emoji, rotulo));
    tCampo(x + 2, 4, w - 4, cor | NEGRITO, AL_ESQ, "%s", v);
}

static void telaHistorico(void) {
    int sel = 0, topo = 0, t, i, n, alt, nHoje, wc, wtab, cN, cD, cH, cP, cI, cDs, cT;
    Centavos fat, hojeTot;
    char hoje[24], rot[32], num[16], trilha[64], d[16], h[8], tt[48];

    for (;;) {
        n = g_hist.n;
        dataISO(time(NULL), hoje, sizeof hoje);
        hoje[10] = '\0';
        for (i = 0, fat = 0, hojeTot = 0, nHoje = 0; i < n; i++) {
            fat += g_hist.v[i].total;
            if (strncmp(g_hist.v[i].data, hoje, 10) == 0) { nHoje++; hojeTot += g_hist.v[i].total; }
        }
        tTela("Início › Histórico de vendas", n ? "↑↓ Pedido|Enter Ver cupom|Esc Voltar" : "Esc Voltar");
        wc = (TELA_L - 6) / 4;
        snprintf(num, sizeof num, "%d", n);
        snprintf(rot, sizeof rot, "Hoje (%d)", nHoje);
        cartao(0, wc, "🧾", "Pedidos", num, COR_FORTE);
        cartao(wc + 2, wc, "💰", "Faturamento", moeda(fat), COR_OK);
        cartao(2 * (wc + 2), wc, "🎯", "Média", moeda(n ? fat / n : 0), COR_ACENTO);
        cartao(3 * (wc + 2), TELA_L - 3 * (wc + 2), "📅", rot, moeda(hojeTot), COR_ROXO);

        wtab = TELA_L;
        alt = TELA_A - 3 - 7 - 4;
        tCaixa(0, 7, wtab, TELA_A - 3 - 7, COR_BORDA, tit(tt, sizeof tt, "📜", "Pedidos registrados"));
        if (n == 0) {
            tCampo(1, 12, wtab - 2, COR_FRACO, AL_CENTRO, "Nenhuma venda registrada ainda.");
            tCampo(1, 13, wtab - 2, COR_FRACO, AL_CENTRO, "Os pedidos finalizados aparecem aqui.");
            do { t = tTecla(); } while (t != TK_ESC && t != TK_REDIM && t != TK_ENTER);
            if (t == TK_REDIM) continue;
            return;
        }
        cN = 2; cD = 9; cH = cD + 12; cP = cH + 8;
        cT = wtab - 16; cDs = cT - 13; cI = cDs - 8;
        tCampo(cN, 8, 6, COR_FRACO, AL_ESQ, "Nº");
        tCampo(cD, 8, 11, COR_FRACO, AL_ESQ, "Data");
        tCampo(cH, 8, 6, COR_FRACO, AL_ESQ, "Hora");
        tCampo(cP, 8, cI - cP, COR_FRACO, AL_ESQ, "Pagamento");
        tCampo(cI, 8, 6, COR_FRACO, AL_DIR, "Itens");
        tCampo(cDs, 8, 12, COR_FRACO, AL_DIR, "Desconto");
        tCampo(cT, 8, 13, COR_FRACO, AL_DIR, "Total");
        tDivisoria(0, 9, wtab, COR_BORDA);
        for (;;) {
            tRolagem(sel, &topo, alt, n);
            for (i = 0; i < alt; i++) {
                int row = 10 + i, s;
                const Pedido *p;
                Estilo c;
                tPreenche(1, row, wtab - 3, 1, COR_TEXTO);
                if (topo + i >= n) continue;
                p = &g_hist.v[n - 1 - (topo + i)];
                s = topo + i == sel;
                c = s ? COR_SEL : COR_TEXTO;
                fmtData(p->data, d, h);
                if (s) tPreenche(1, row, wtab - 3, 1, COR_SEL);
                tCampo(cN, row, 6, s ? c : COR_FRACO, AL_ESQ, "%04d", p->numero);
                tCampo(cD, row, 11, c, AL_ESQ, "%s", d);
                tCampo(cH, row, 6, c, AL_ESQ, "%s", h);
                tCampo(cP, row, cI - cP, c, AL_ESQ, "%s %s", ic(PAG_ICONE[p->forma], PAG_ALT[p->forma]), PAG_NOME[p->forma]);
                tCampo(cI, row, 6, c, AL_DIR, "%d", carUnidades(&p->itens));
                tCampo(cDs, row, 12, s ? c : (p->desconto ? COR_OK : COR_FRACO), AL_DIR, "%s", p->desconto ? valor(p->desconto) : "—");
                tCampo(cT, row, 13, s ? c : COR_FORTE, AL_DIR, "%s", moeda(p->total));
                tZona(1, row, wtab - 3, 1, ZN_PED + topo + i, 0);
            }
            tBarraRolagem(wtab - 2, 10, alt, topo, n);
            t = tTecla();
            if (t == TK_REDIM) break;
            if (t >= ZN_PED && t < ZN_PED + 0x10000) {
                int k = t - ZN_PED;
                if (k >= n) continue;
                if (k == sel || g_duplo) { sel = k; t = TK_ENTER; } else { sel = k; continue; }
            }
            if (t == TK_CIMA && sel > 0) sel--;
            else if (t == TK_BAIXO && sel < n - 1) sel++;
            else if (t == TK_PGCIMA)  { sel -= alt; if (sel < 0) sel = 0; }
            else if (t == TK_PGBAIXO) { sel += alt; if (sel > n - 1) sel = n - 1; }
            else if (t == TK_INICIO) sel = 0;
            else if (t == TK_FIM) sel = n - 1;
            else if (t == TK_ESC) return;
            else if (t == TK_ENTER) {
                const Pedido *p = &g_hist.v[n - 1 - sel];
                snprintf(trilha, sizeof trilha, "Início › Histórico › Pedido nº %04d", p->numero);
                telaCupom(p, trilha);
                break;
            }
        }
    }
}

/* ============================ TELA INICIAL ============================ */

static int g_px, g_pw;

/* Gráfico de barras das vendas dos últimos 7 dias. */
static void graficoSemana(int x, int y, int w, int alt) {
    static const char *DIA[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    Centavos tot[7], max = 0;
    char iso[24];
    int d, i, larg = w / 7, col;
    time_t agora = time(NULL);
    for (d = 0; d < 7; d++) {
        time_t t = agora - (time_t)(6 - d) * 86400;
        dataISO(t, iso, sizeof iso);
        tot[d] = 0;
        for (i = 0; i < g_hist.n; i++) if (!strncmp(g_hist.v[i].data, iso, 10)) tot[d] += g_hist.v[i].total;
        if (tot[d] > max) max = tot[d];
    }
    for (d = 0; d < 7; d++) {
        time_t t = agora - (time_t)(6 - d) * 86400;
        int wd = localtime(&t)->tm_wday, oitavos = max ? (int)(tot[d] * (alt * 8) / max) : 0, r;
        col = x + d * larg;
        for (r = 0; r < alt; r++) {
            int resto = oitavos - r * 8;
            unsigned cp = resto >= 8 ? 0x2588 : resto > 0 ? 0x2580 + (unsigned)resto : ' ';
            unsigned cor = misturar(PALETA[P_AZUL], PALETA[P_ROXO], (double)r / (alt > 1 ? alt - 1 : 1));
            int k;
            if (resto > 0 && resto < 8) cp = 0x2581 + (unsigned)(resto - 1);
            for (k = 0; k < larg - 1; k++)
                tCelRGB(col + k, y + alt - 1 - r, cp == ' ' && r == 0 ? 0x2581 : cp, 1,
                        cp == ' ' && r == 0 ? PALETA[P_CAMPO] : (d == 6 ? PALETA[P_ACENTO] : cor), 0xFFFFFFFFu, 0, 9, -1);
        }
        tCampo(col, y + alt, larg - 1, d == 6 ? COR_ACENTO | NEGRITO : COR_FRACO, AL_CENTRO, "%s", d == 6 ? "hoje" : DIA[wd]);
    }
}

static void painelInicio(int sel) {
    char hoje[24], txt[48];
    int i, nHoje = 0, x = g_px + 2, w = g_pw - 4, maxCat = 1, yg;
    Centavos fat = 0, hojeTot = 0;
    dataISO(time(NULL), hoje, sizeof hoje);
    for (i = 0; i < g_hist.n; i++) {
        fat += g_hist.v[i].total;
        if (strncmp(g_hist.v[i].data, hoje, 10) == 0) { nHoje++; hojeTot += g_hist.v[i].total; }
    }
    for (i = 0; i < NUM_CAT; i++) if (cardContar((Categoria)i) > maxCat) maxCat = cardContar((Categoria)i);
    tCampo(x, 3, w, COR_FORTE, AL_ESQ, "Cardápio · %d produto(s)", g_card.n);
    for (i = 0; i < NUM_CAT; i++) {
        int c = cardContar((Categoria)i), b = (w - 18) * c / maxCat, k;
        tTexto(x, 4 + i, COR_TEXTO, "%s", ic(CAT_ICONE[i], CAT_ALT[i]));
        tCampo(x + 3, 4 + i, 11, COR_TEXTO, AL_ESQ, "%s", CAT_PLURAL[i]);
        for (k = 0; k < w - 18; k++) tTexto(x + 14 + k, 4 + i, k < b ? EST(CAT_COR[i], 0) : COR_BORDA, k < b ? "█" : "·");
        tCampo(x + w - 3, 4 + i, 3, COR_FRACO, AL_DIR, "%d", c);
    }
    tTexto(x, 8, COR_FORTE, "Vendas");
    snprintf(txt, sizeof txt, "%d · %s", nHoje, moeda(hojeTot));
    linhaValor(x, 9, w, COR_TEXTO, COR_ACENTO, "Hoje", txt);
    linhaValor(x, 10, w, COR_TEXTO, COR_OK | NEGRITO, "Faturamento total", moeda(fat));
    yg = 12;
    if (TELA_A >= 28) {
        tTexto(x, yg, COR_FRACO, "Últimos 7 dias");
        graficoSemana(x, yg + 1, w, TELA_A - 5 - yg - 7);
        yg = TELA_A - 10;
    } else {
        graficoSemana(x, yg, w, 2);
        yg = 15;
    }
    tDivisoria(g_px, yg + 1, g_pw, COR_BORDA);
    tCampo(x, yg + 2, w, COR_ACENTO | NEGRITO, AL_ESQ, "%s %s", ic(MENU[sel].icone, MENU[sel].alt), MENU[sel].rotulo);
    tParagrafo(x, yg + 3, w, TELA_A - 4 - (yg + 3), COR_TEXTO, MENU[sel].descricao);
}

static void telaInicio(int *wMenu) {
    int wl = TELA_L * 46 / 100, yp;
    char t[48];
    if (wl < 36) wl = 36;
    g_px = wl + 2;
    g_pw = TELA_L - g_px;
    tTela("Início", "↑↓ Navegar|Enter Selecionar|1-4 Atalho|Esc Sair");
    tCaixa(0, 2, wl, 10, COR_BORDA, tit(t, sizeof t, "🍟", "O que deseja fazer?"));
    tCaixa(g_px, 2, g_pw, TELA_A - 5, COR_BORDA, tit(t, sizeof t, "📈", "Painel"));
    yp = 13;
    if (TELA_A - 4 - yp >= 5) {
        int hp = TELA_A - 4 - yp;
        if (hp > 8) hp = 8;
        tCaixa(0, yp, wl, hp, COR_OK, tit(t, sizeof t, "💵", "Promoção"));
        tCampo(1, yp + hp / 2 - 1, wl - 2, COR_FORTE, AL_CENTRO, "Pagou em dinheiro,");
        tTextoDegrade(1 + (wl - 2 - 23) / 2, yp + hp / 2, "ganhou 10% de desconto!", PALETA[P_OK], PALETA[P_ACENTO], 1);
        if (hp >= 6) tCampo(1, yp + hp / 2 + 1, wl - 2, COR_FRACO, AL_CENTRO, "Aplicado automaticamente no caixa");
    }
    *wMenu = wl - 4;
}

/* =============================== MAIN =============================== */

int main(void) {
    int sel = 0, op, ignoradas = 0, r, wMenu;

    tIniciar("🍔", "#", "Lanchonete", "Questão 03 · Linguagem C");
    tAbertura("LANCHONETE", "Pedidos, cardápio e controle de vendas", "Questão 03 · Linguagem C");

    r = carregarCardapio(&ignoradas);
    carregarPedidos();
    if (r == 2)      tAvisoPendente(MSG_INFO, "Bem-vindo! Um cardápio inicial com %d produtos foi criado.", g_card.n);
    else if (r == 0) tAvisoPendente(MSG_ERRO, "Não foi possível criar %s. Verifique a permissão da pasta.", ARQ_PRODUTOS);
    else if (ignoradas) tAvisoPendente(MSG_AVISO, "%d linha(s) inválida(s) de %s foram ignoradas.", ignoradas, ARQ_PRODUTOS);

    for (;;) {
        telaInicio(&wMenu);
        op = tMenu(2, 3, wMenu, MENU, N_MENU, &sel, painelInicio, 2);
        if (op == MENU_REDIM) continue;
        if (op == 0)      telaNovoPedido();
        else if (op == 1) telaCardapio();
        else if (op == 2) telaHistorico();
        else if (tConfirmar("Sair", "Deseja encerrar o sistema?\nTodos os dados já estão salvos.", 1)) break;
    }
    free(g_card.v);
    free(g_hist.v);
    free(g_idx);
    tFinalizar("Obrigado e volte sempre!");
    return 0;
}
