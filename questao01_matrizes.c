/*
 * ==========================================================================
 *  QUESTÃO 01 — MATRIZES
 * ==========================================================================
 *  Lê duas matrizes A e B (até 5 × 5) e realiza:
 *    • Soma elemento a elemento ............ R[i,j] = A[i,j] + B[i,j]
 *    • Multiplicação elemento a elemento ... R[i,j] = A[i,j] × B[i,j]
 *    • Produto matricial (extra) ........... R[i,j] = Σ A[i,k] × B[k,j]
 *  respeitando as dimensões compatíveis de cada operação.
 *
 *  Interface:
 *    • mouse e teclado em todas as telas (clique, passagem e rolagem);
 *    • escolha do tamanho clicando direto na pré-visualização 5 × 5;
 *    • editor em grade: clique numa célula e digite;
 *    • resultado interativo: clique (ou use as setas) numa célula de R
 *      para ver os elementos usados destacados e a conta por extenso;
 *    • layout responsivo: as matrizes crescem com a janela.
 *
 *  Compilar (MinGW / w64devkit):  gcc questao01_matrizes.c -o questao01.exe
 *  Executar:                      .\questao01.exe
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

#define MAX_DIM 5

typedef struct {
    int lin, col;
    double v[MAX_DIM][MAX_DIM];
    int definida;
} Matriz;

typedef enum { OP_SOMA, OP_ELEMENTO, OP_PRODUTO } Operacao;
enum { MARCA_NADA, MARCA_USADA, MARCA_ATUAL };

/* Códigos das zonas clicáveis desta aplicação */
#define ZN_CEL      (TK_ZONA + 0x100)    /* + i*MAX_DIM + j                 */
#define ZN_PREV     (TK_ZONA + 0x200)    /* pré-visualização do tamanho     */
#define ZN_MENOS    (TK_ZONA + 0x300)    /* + campo (0 linhas, 1 colunas)   */
#define ZN_MAIS     (TK_ZONA + 0x310)
#define ZN_CAMPO    (TK_ZONA + 0x320)
#define ZN_CONCLUIR (TK_ZONA + 0x400)

static const char *SIMBOLO[]  = { "+", "∘", "×" };
static const char *NOME_OP[]  = { "Soma", "Multiplicação elemento a elemento", "Produto matricial" };
static const char *CURTO_OP[] = { "Soma  A + B", "Elemento a elemento  A ∘ B", "Produto  A × B" };
static const char *EXPLICA_OP[] = {
    "Cada posição de R é a soma das posições de A e B.",
    "Cada posição de R é o produto das posições de A e B.",
    "R[i,j] = soma dos produtos da linha i de A pela coluna j de B."
};

static Matriz g_A, g_B;
static int g_px, g_pw;                    /* posição do painel "Situação" */

static const ItemMenu MENU[] = {
    { "📘", "A ", "Definir matriz A", "Escolha o tamanho (até 5×5) clicando na grade e digite os valores, como numa planilha.", 'a' },
    { "📙", "B ", "Definir matriz B", "Escolha o tamanho (até 5×5) clicando na grade e digite os valores, como numa planilha.", 'b' },
    { "🔍", "◫ ", "Visualizar matrizes", "Mostra A e B lado a lado, com soma, maior, menor valor e diagonal de cada uma.", 'v' },
    { " +", " +", "Soma  (A + B)", "Soma posição por posição. As duas matrizes precisam ter o mesmo tamanho.", '+' },
    { " ∘", " ∘", "Elemento a elemento  (A ∘ B)", "Multiplica posição por posição. As duas matrizes precisam ter o mesmo tamanho.", '*' },
    { " ×", " ×", "Produto matricial  (A × B)", "Multiplicação clássica: o nº de colunas de A deve ser igual ao nº de linhas de B.", 'x' },
    { "🎲", "* ", "Carregar exemplo pronto", "Preenche A e B com valores 3 × 3 para você experimentar as operações na hora.", 'e' },
    { "🚪", "< ", "Sair", "Encerra o programa.", 0 }
};
#define N_MENU ((int)(sizeof MENU / sizeof MENU[0]))

/* ============================ CÁLCULOS ============================ */

static int compativel(Operacao op, const Matriz *a, const Matriz *b) {
    if (op == OP_PRODUTO) return a->col == b->lin;
    return a->lin == b->lin && a->col == b->col;
}

static void calcular(Operacao op, const Matriz *a, const Matriz *b, Matriz *r) {
    int i, j, k;
    memset(r, 0, sizeof *r);
    r->lin = a->lin;
    r->col = op == OP_PRODUTO ? b->col : a->col;
    r->definida = 1;
    for (i = 0; i < r->lin; i++)
        for (j = 0; j < r->col; j++) {
            if (op == OP_SOMA)          r->v[i][j] = a->v[i][j] + b->v[i][j];
            else if (op == OP_ELEMENTO) r->v[i][j] = a->v[i][j] * b->v[i][j];
            else for (k = 0; k < a->col; k++) r->v[i][j] += a->v[i][k] * b->v[k][j];
        }
}

/* ============================ FORMATAÇÃO ============================ */

/* Valor com no máximo 'larg' caracteres (notação científica compacta se preciso). */
static void fmtCelula(double v, int larg, char *out, size_t tam) {
    char tmp[48], *p, *e;
    int prec;
    tFmtNum(v, tmp, sizeof tmp, 2);
    if (u8Len(tmp) <= larg) { snprintf(out, tam, "%s", tmp); return; }
    for (prec = 2; prec >= 0; prec--) {
        snprintf(tmp, sizeof tmp, "%.*e", prec, v);
        e = strchr(tmp, 'e');                         /* e+006 -> e6 */
        if (e) {
            char *d = e + 1, *s = e + 1;
            if (*s == '+') s++;
            else if (*s == '-') *d++ = *s++;
            while (*s == '0' && s[1]) s++;
            memmove(d, s, strlen(s) + 1);
        }
        for (p = tmp; *p; p++) if (*p == '.') *p = ',';
        if ((int)strlen(tmp) <= larg) break;
    }
    snprintf(out, tam, "%s", tmp);
}

static void fmtEditavel(double v, char *out, size_t tam) {
    char *p;
    snprintf(out, tam, "%.10g", v);
    for (p = out; *p; p++) if (*p == '.') *p = ',';
}

static const char *termo(double v, char *buf, size_t tam) {
    char n[40];
    tFmtNum(v, n, sizeof n, 4);
    snprintf(buf, tam, v < 0 ? "(%s)" : "%s", n);
    return buf;
}

/* Título com emoji opcional. */
static const char *titulo(char *buf, size_t tam, const char *emoji, const char *texto) {
    if (g_emoji) snprintf(buf, tam, "%s %s", emoji, texto);
    else snprintf(buf, tam, "%s", texto);
    return buf;
}

/* ============================ DESENHO ============================ */

static int larguraMatriz(const Matriz *m, int cel) { return m->col * cel + 3; }

/* Matriz entre colchetes coloridos. 'marca' destaca células; 'zonas' torna
 * as células clicáveis. */
static void desenharMatriz(int x, int y, const Matriz *m, const char *tit, int cel, int corCol,
                           Estilo corNum, unsigned char marca[MAX_DIM][MAX_DIM], int zonas) {
    int i, j, w = larguraMatriz(m, cel);
    char v[32];
    tCampo(x, y, w, EST(corCol, 0) | NEGRITO, AL_CENTRO, "%s", tit);
    tTexto(x, y + 1, EST(corCol, 0), "┌");
    tTexto(x + w - 1, y + 1, EST(corCol, 0), "┐");
    for (i = 0; i < m->lin; i++) {
        tTexto(x, y + 2 + i, EST(corCol, 0), "│");
        tTexto(x + w - 1, y + 2 + i, EST(corCol, 0), "│");
        for (j = 0; j < m->col; j++) {
            Estilo c = corNum;
            int mk = marca ? marca[i][j] : MARCA_NADA;
            if (mk == MARCA_USADA) c = EST(P_ESCURO, P_AVISO) | NEGRITO;
            else if (mk == MARCA_ATUAL) c = COR_SEL;
            fmtCelula(m->v[i][j], cel - 2, v, sizeof v);
            tCampo(x + 1 + j * cel, y + 2 + i, cel, c, AL_DIR, "%s ", v);
            if (zonas) tZona(x + 1 + j * cel, y + 2 + i, cel, 1, ZN_CEL + i * MAX_DIM + j, 0);
        }
    }
    tTexto(x, y + 2 + m->lin, EST(corCol, 0), "└");
    tTexto(x + w - 1, y + 2 + m->lin, EST(corCol, 0), "┘");
}

/* Maior largura de célula que permite A e B lado a lado na janela. */
static int escolherCelula(const Matriz *a, const Matriz *b) {
    int cel;
    for (cel = 13; cel > 7; cel--)
        if (larguraMatriz(a, cel) + 9 + larguraMatriz(b, cel) <= TELA_L - 4) break;
    return cel;
}

/* Selo do operador: ▐ + ▌ */
static void seloOperador(int x, int y, const char *s, int cor) {
    tCel(x, y, 0x2590, 1, EST(cor, 0));
    tTexto(x + 1, y, EST(P_ESCURO, cor) | NEGRITO, " %s ", s);
    tCel(x + 4, y, 0x258C, 1, EST(cor, 0));
}

/* ============================ TELA INICIAL ============================ */

static void miniatura(int x, int y, const Matriz *m, const char *nome, const char *icone, int cor) {
    int i, j;
    tTexto(x, y, COR_FORTE, "%sMatriz %s", g_emoji ? icone : "", nome);
    if (m->definida) tTexto(x, y + 1, EST(cor, 0), "%d × %d", m->lin, m->col);
    else             tTexto(x, y + 1, COR_FRACO, "vazia");
    for (i = 0; i < MAX_DIM; i++)
        for (j = 0; j < MAX_DIM; j++) {
            int on = m->definida && i < m->lin && j < m->col;
            tTexto(x + j * 2, y + 3 + i, on ? EST(cor, 0) : COR_BORDA, on ? "■" : "·");
        }
}

static void painelInicio(int sel) {
    int op, x = g_px + 2, w = g_pw - 4, metade = (g_pw - 4) / 2;
    miniatura(x, 3, &g_A, "A", "📘 ", P_AZUL);
    miniatura(x + metade, 3, &g_B, "B", "📙 ", P_LARANJA);
    tTexto(x, 11, COR_FORTE, "Operações possíveis");
    for (op = 0; op < 3; op++) {
        if (!g_A.definida || !g_B.definida)
            tCampo(x, 12 + op, w, COR_FRACO, AL_ESQ, "·  %s", CURTO_OP[op]);
        else if (compativel((Operacao)op, &g_A, &g_B))
            tCampo(x, 12 + op, w, COR_OK, AL_ESQ, "✓  %s", CURTO_OP[op]);
        else
            tCampo(x, 12 + op, w, COR_ERRO, AL_ESQ, "✕  %s", CURTO_OP[op]);
    }
    tDivisoria(g_px, 16, g_pw, COR_BORDA);
    tCampo(x, 17, w, COR_ACENTO | NEGRITO, AL_ESQ, "%s %s", ic(MENU[sel].icone, MENU[sel].alt), MENU[sel].rotulo);
    tParagrafo(x, 18, w, TELA_A - 5 - 18 + 2 - 1, COR_TEXTO, MENU[sel].descricao);
}

/* Desenha a tela inicial e informa onde fica o menu. */
static void telaInicio(int *yMenu, int *espaco, int *wMenu) {
    int wl = TELA_L * 56 / 100, hMenu, yAj, hAj;
    char t[64];
    *espaco = TELA_A >= 32 ? 2 : 1;
    hMenu = N_MENU * *espaco + 2 + (*espaco == 1);
    g_px = wl + 2;
    g_pw = TELA_L - g_px;
    tTela("Início", "↑↓ Navegar|Enter Selecionar|1-8 Atalho|Esc Sair");
    tCaixa(0, 2, wl, hMenu, COR_BORDA, titulo(t, sizeof t, "📋", "O que você quer fazer?"));
    tCaixa(g_px, 2, g_pw, TELA_A - 5, COR_BORDA, titulo(t, sizeof t, "📊", "Situação"));
    yAj = 2 + hMenu + 1;
    hAj = TELA_A - 3 - yAj;
    if (hAj >= 7) {
        tCaixa(0, yAj, wl, hAj, COR_BORDA, titulo(t, sizeof t, "💡", "Como usar"));
        tTexto(2, yAj + 1, COR_ACENTO | NEGRITO, "1");
        tTexto(4, yAj + 1, COR_TEXTO, "Defina o tamanho e os valores");
        tTexto(4, yAj + 2, COR_TEXTO, "das matrizes A e B.");
        tTexto(2, yAj + 3, COR_ACENTO | NEGRITO, "2");
        tTexto(4, yAj + 3, COR_TEXTO, "Escolha uma operação (4, 5 ou 6).");
        tTexto(2, yAj + 4, COR_ACENTO | NEGRITO, "3");
        tTexto(4, yAj + 4, COR_TEXTO, "Clique nas células do resultado.");
    }
    *yMenu = 3 + (*espaco == 1);
    *wMenu = wl - 4;
}

/* ======================== DEFINIÇÃO DE MATRIZ ======================== */

/* Passo 1: dimensões — botões ◂ ▸, teclado ou clique na pré-visualização. */
static int escolherDimensoes(char nome, int *lin, int *col) {
    int val[2], foco = 0, t, i, j, x0, cw = 36, cor = nome == 'A' ? P_AZUL : P_LARANJA;
    char trilha[64], tit[64], t2[64];
    val[0] = *lin;
    val[1] = *col;
    snprintf(trilha, sizeof trilha, "Início › Matriz %c › Tamanho", nome);
    snprintf(t2, sizeof t2, "Tamanho da matriz %c", nome);
    for (;;) {
        tTela(trilha, "↑↓ Trocar campo|←→ Ajustar|Enter Continuar|Esc Cancelar");
        x0 = (TELA_L - (cw * 2 + 2)) / 2;
        tCaixa(x0, 3, cw, 13, COR_BORDA, titulo(tit, sizeof tit, nome == 'A' ? "📘" : "📙", t2));
        tCaixa(x0 + cw + 2, 3, cw, 13, COR_BORDA, titulo(tit, sizeof tit, "👀", "Pré-visualização"));
        tTexto(x0 + 3, 11, COR_FRACO, "Dica: clique na grade ao lado");
        tTexto(x0 + 3, 12, COR_FRACO, "para escolher o tamanho direto.");
        tTexto(x0 + 3, 13, COR_FRACO, "Máximo: 5 × 5.");
        tBotao(x0 + cw * 2 + 2 - 17, 17, "  Continuar ›  ", ZN_CONCLUIR, P_AZUL, 1);
        tBotao(x0 + cw * 2 + 2 - 31, 17, " Cancelar ", TK_ESC, P_CAMPO, 1);
        tStatus(MSG_INFO, "Escolha quantas linhas e colunas a matriz %c terá.", nome);
        for (;;) {
            for (i = 0; i < 2; i++) {
                int y = i == 0 ? 5 : 8;
                tTexto(x0 + 3, y, foco == i ? COR_FORTE : COR_TEXTO, i == 0 ? "Linhas " : "Colunas");
                tTexto(x0 + 15, y, EST(P_FORTE, P_BORDA), " ◂ ");
                tZona(x0 + 15, y, 3, 1, ZN_MENOS + i, COR_HOVER);
                tCampo(x0 + 19, y, 7, foco == i ? COR_SEL : COR_CAMPO, AL_CENTRO, "%d", val[i]);
                tZona(x0 + 19, y, 7, 1, ZN_CAMPO + i, 0);
                tTexto(x0 + 27, y, EST(P_FORTE, P_BORDA), " ▸ ");
                tZona(x0 + 27, y, 3, 1, ZN_MAIS + i, COR_HOVER);
            }
            for (i = 0; i < MAX_DIM; i++)
                for (j = 0; j < MAX_DIM; j++) {
                    int on = i < val[0] && j < val[1], px = x0 + cw + 2 + 7 + j * 5, py = 5 + i;
                    tTexto(px, py, on ? EST(cor, 0) : COR_BORDA, on ? " ██ " : " ·· ");
                    tZona(px, py, 4, 1, ZN_PREV + i * MAX_DIM + j, COR_HOVER);
                }
            tCampo(x0 + cw + 4, 12, cw - 4, COR_TEXTO, AL_CENTRO, "%d × %d  =  %d valores", val[0], val[1], val[0] * val[1]);

            t = tTecla();
            if (t == TK_REDIM) break;
            if (t == TK_CIMA || t == TK_BAIXO || t == TK_TAB) foco = !foco;
            else if (t == TK_ESQ && val[foco] > 1) val[foco]--;
            else if (t == TK_DIR && val[foco] < MAX_DIM) val[foco]++;
            else if (t >= ZN_MENOS && t <= ZN_MENOS + 1) { foco = t - ZN_MENOS; if (val[foco] > 1) val[foco]--; }
            else if (t >= ZN_MAIS && t <= ZN_MAIS + 1) { foco = t - ZN_MAIS; if (val[foco] < MAX_DIM) val[foco]++; }
            else if (t >= ZN_CAMPO && t <= ZN_CAMPO + 1) foco = t - ZN_CAMPO;
            else if (t >= ZN_PREV && t < ZN_PREV + MAX_DIM * MAX_DIM) {
                val[0] = (t - ZN_PREV) / MAX_DIM + 1;
                val[1] = (t - ZN_PREV) % MAX_DIM + 1;
            }
            else if (t >= '1' && t <= '5') val[foco] = t - '0';
            else if (t == TK_ESC) return 0;
            else if (t == TK_ENTER && foco == 0) foco = 1;
            else if (t == TK_ENTER || t == ZN_CONCLUIR) {
                *lin = val[0];
                *col = val[1];
                return 1;
            }
        }
    }
}

/* Passo 2: editor em grade. Retorna 1 se a matriz foi salva. */
static int editarMatriz(Matriz *m, char nome) {
    Matriz tmp;
    unsigned char cheia[MAX_DIM][MAX_DIM];
    int lin = m->definida ? m->lin : 3, col = m->definida ? m->col : 3;
    int li = 0, co = 0, t, i, j, gx, gy = 4, cw, total, nCheias = 0, yb, cor = nome == 'A' ? P_AZUL : P_LARANJA;
    char trilha[64], buf[40], v[32];
    double valor;

    if (!escolherDimensoes(nome, &lin, &col)) return 0;
    tmp = *m;
    if (m->definida && m->lin == lin && m->col == col) memset(cheia, 1, sizeof cheia);
    else { memset(&tmp, 0, sizeof tmp); memset(cheia, 0, sizeof cheia); }
    tmp.lin = lin;
    tmp.col = col;
    total = lin * col;
    snprintf(trilha, sizeof trilha, "Início › Matriz %c › Valores", nome);

    for (;;) {                                        /* redesenho completo */
        cw = (TELA_L - 14) / col;
        if (cw > 14) cw = 14;
        if (cw < 10) cw = 10;
        gx = (TELA_L - (col * cw + 1 + 9)) / 2 + 9;
        tTela(trilha, "←↑↓→ Mover|Enter Editar|Del Apagar|Esc Concluir|D Descartar");
        for (j = 0; j < col; j++)
            tCampo(gx + 1 + j * cw, gy - 1, cw - 1, COR_FRACO, AL_CENTRO, "coluna %d", j + 1);
        for (i = 0; i <= lin; i++) {
            int y = gy + 2 * i;
            const char *e = i == 0 ? "╭" : i == lin ? "╰" : "├";
            const char *mm = i == 0 ? "┬" : i == lin ? "┴" : "┼";
            const char *d = i == 0 ? "╮" : i == lin ? "╯" : "┤";
            tTexto(gx, y, EST(cor, 0), "%s", e);
            for (j = 0; j < col; j++) {
                tRepete(gx + 1 + j * cw, y, cw - 1, EST(cor, 0), "─");
                tTexto(gx + (j + 1) * cw, y, EST(cor, 0), "%s", j == col - 1 ? d : mm);
            }
        }
        for (i = 0; i < lin; i++) {
            tTexto(gx - 9, gy + 1 + 2 * i, COR_FRACO, "linha %d", i + 1);
            for (j = 0; j <= col; j++) tTexto(gx + j * cw, gy + 1 + 2 * i, EST(cor, 0), "│");
        }
        yb = gy + 2 * lin + 2;
        {
            int bx = (TELA_L - 52) / 2;
            bx += tBotao(bx, yb + 2, " ✓ Concluir ", ZN_CONCLUIR, P_OK, 1) + 2;
            bx += tBotao(bx, yb + 2, " ⌫ Apagar célula ", TK_DEL, P_CAMPO, 1) + 2;
            tBotao(bx, yb + 2, " ✕ Descartar ", 'd', P_ERRO, 1);
        }
        tStatus(MSG_INFO, "Clique numa célula (ou use as setas), digite o número e tecle Enter.");

        for (;;) {
            for (i = 0, nCheias = 0; i < lin; i++)
                for (j = 0; j < col; j++) {
                    Estilo c = (i == li && j == co) ? COR_SEL : cheia[i][j] ? COR_FORTE : COR_FRACO;
                    if (cheia[i][j]) { fmtCelula(tmp.v[i][j], cw - 3, v, sizeof v); nCheias++; }
                    else strcpy(v, "·");
                    tCampo(gx + 1 + j * cw, gy + 1 + 2 * i, cw - 1, c, AL_CENTRO, "%s", v);
                    tZona(gx + 1 + j * cw, gy + 1 + 2 * i, cw - 1, 1, ZN_CEL + i * MAX_DIM + j, 0);
                }
            tTexto((TELA_L - 44) / 2, yb, COR_TEXTO, "Preenchidas");
            tBarra((TELA_L - 44) / 2 + 13, yb, 22, (double)nCheias / total, 0);
            tTexto((TELA_L - 44) / 2 + 37, yb, nCheias == total ? COR_OK : COR_TEXTO, "%2d de %d", nCheias, total);

            t = tTecla();
            if (t == TK_REDIM) break;
            if (t >= ZN_CEL && t < ZN_CEL + MAX_DIM * MAX_DIM) {
                int ni = (t - ZN_CEL) / MAX_DIM, nj = (t - ZN_CEL) % MAX_DIM;
                int editar = (ni == li && nj == co) || g_duplo;   /* 2º clique edita */
                li = ni; co = nj;
                if (!editar) continue;
                t = TK_ENTER;
            }
            if (t == TK_CIMA && li > 0) li--;
            else if (t == TK_BAIXO && li < lin - 1) li++;
            else if (t == TK_ESQ && co > 0) co--;
            else if (t == TK_DIR && co < col - 1) co++;
            else if (t == TK_DEL || t == TK_BS) { cheia[li][co] = 0; tmp.v[li][co] = 0; }
            else if (t == 'd' || t == 'D') {
                if (tConfirmar("Descartar", "Deseja sair sem salvar esta matriz?", 0)) return 0;
            } else if (t == TK_ENTER || (t >= '0' && t <= '9') || t == '-' || t == ',' || t == '.') {
                if (t == TK_ENTER) {
                    if (cheia[li][co]) fmtEditavel(tmp.v[li][co], buf, sizeof buf);
                    else buf[0] = '\0';
                } else if (t == ',' || t == '.') strcpy(buf, "0,");
                else { buf[0] = (char)t; buf[1] = '\0'; }
                if (tLerReal(gx + 1 + co * cw, gy + 1 + 2 * li, cw - 1, buf, t == TK_ENTER, -1e9, 1e9,
                             "Use valores entre -1.000.000.000 e 1.000.000.000.", &valor)) {
                    tmp.v[li][co] = valor;
                    cheia[li][co] = 1;
                    if (++co == col) { co = 0; if (++li == lin) { li = lin - 1; co = col - 1; } }
                    for (i = 0, nCheias = 0; i < lin; i++) for (j = 0; j < col; j++) nCheias += cheia[i][j];
                    if (nCheias == total) tStatus(MSG_OK, "Todas as células preenchidas! Clique em Concluir ou tecle Esc.");
                }
            } else if (t == TK_ESC || t == ZN_CONCLUIR) {
                if (nCheias < total) {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Ainda há %d célula(s) vazia(s).\nPreencher com zero e salvar?", total - nCheias);
                    if (!tConfirmar("Células vazias", msg, 1)) continue;
                }
                tmp.definida = 1;
                *m = tmp;
                tAvisoPendente(MSG_OK, "Matriz %c salva (%d × %d).", nome, lin, col);
                return 1;
            }
        }
    }
}

/* ============================ VISUALIZAÇÃO ============================ */

static void linhaEstatistica(int x, int y, int w, const char *nome, const Matriz *m, int cor) {
    char s[32], mx[32], mn[32], tr[32];
    double soma = 0, maior = m->v[0][0], menor = m->v[0][0], traco = 0;
    int i, j, c = (w - 22) / 4;
    for (i = 0; i < m->lin; i++)
        for (j = 0; j < m->col; j++) {
            soma += m->v[i][j];
            if (m->v[i][j] > maior) maior = m->v[i][j];
            if (m->v[i][j] < menor) menor = m->v[i][j];
            if (i == j) traco += m->v[i][j];
        }
    tFmtNum(soma, s, sizeof s, 2);
    tFmtNum(maior, mx, sizeof mx, 2);
    tFmtNum(menor, mn, sizeof mn, 2);
    if (m->lin == m->col) tFmtNum(traco, tr, sizeof tr, 2); else strcpy(tr, "—");
    tCampo(x, y, 12, EST(cor, 0) | NEGRITO, AL_ESQ, "Matriz %s", nome);
    tCampo(x + 12, y, 10, COR_TEXTO, AL_DIR, "%d × %d", m->lin, m->col);
    tCampo(x + 22, y, c, COR_FORTE, AL_DIR, "%s", s);
    tCampo(x + 22 + c, y, c, COR_TEXTO, AL_DIR, "%s", mx);
    tCampo(x + 22 + 2 * c, y, c, COR_TEXTO, AL_DIR, "%s", mn);
    tCampo(x + 22 + 3 * c, y, c, COR_TEXTO, AL_DIR, "%s", tr);
}

static void telaVisualizar(void) {
    int cel, wa, wb, xa, alt, y, w, x, c, t;
    char ta[48], tb[48];
    if (!g_A.definida && !g_B.definida) {
        tAlerta(MSG_AVISO, "Nada para mostrar", "Nenhuma matriz foi definida ainda.\nUse as opções 1 e 2, ou carregue o exemplo (7).");
        return;
    }
    titulo(ta, sizeof ta, "📘", "Matriz A");
    titulo(tb, sizeof tb, "📙", "Matriz B");
    do {
        tTela("Início › Visualizar matrizes", "Esc Voltar ao menu");
        if (g_A.definida && g_B.definida) {
            cel = escolherCelula(&g_A, &g_B);
            wa = larguraMatriz(&g_A, cel);
            wb = larguraMatriz(&g_B, cel);
            xa = (TELA_L - (wa + 9 + wb)) / 2;
            desenharMatriz(xa, 3, &g_A, ta, cel, P_AZUL, COR_TEXTO, NULL, 0);
            desenharMatriz(xa + wa + 9, 3, &g_B, tb, cel, P_LARANJA, COR_TEXTO, NULL, 0);
            alt = g_A.lin > g_B.lin ? g_A.lin : g_B.lin;
        } else {
            const Matriz *m = g_A.definida ? &g_A : &g_B;
            desenharMatriz((TELA_L - larguraMatriz(m, 11)) / 2, 3, m, g_A.definida ? ta : tb, 11,
                           g_A.definida ? P_AZUL : P_LARANJA, COR_TEXTO, NULL, 0);
            tStatus(MSG_AVISO, "A matriz %s ainda não foi definida.", g_A.definida ? "B" : "A");
            alt = m->lin;
        }
        w = TELA_L - 8 < 90 ? TELA_L - 8 : 90;
        x = (TELA_L - w) / 2;
        y = alt + 7;
        {
            char te[48];
            tCaixa(x, y, w, 6, COR_BORDA, titulo(te, sizeof te, "📈", "Estatísticas"));
        }
        c = (w - 4 - 22) / 4;
        tCampo(x + 14, y + 1, 10, COR_FRACO, AL_DIR, "Tamanho");
        tCampo(x + 24, y + 1, c, COR_FRACO, AL_DIR, "Soma");
        tCampo(x + 24 + c, y + 1, c, COR_FRACO, AL_DIR, "Maior");
        tCampo(x + 24 + 2 * c, y + 1, c, COR_FRACO, AL_DIR, "Menor");
        tCampo(x + 24 + 3 * c, y + 1, c, COR_FRACO, AL_DIR, "Diagonal");
        if (g_A.definida) linhaEstatistica(x + 2, y + 3, w - 4, "A", &g_A, P_AZUL);
        if (g_B.definida) linhaEstatistica(x + 2, y + (g_A.definida ? 4 : 3), w - 4, "B", &g_B, P_LARANJA);
        do { t = tTecla(); } while (t != TK_ESC && t != TK_ENTER && t != TK_REDIM);
    } while (t == TK_REDIM);
}

/* ============================ RESULTADOS ============================ */

static void explicarCelula(Operacao op, const Matriz *r, int i, int j, char *out, size_t tam) {
    char t1[48], t2[48], res[48];
    size_t n;
    int k;
    tFmtNum(r->v[i][j], res, sizeof res, 4);
    if (op != OP_PRODUTO) {
        const char *s = op == OP_SOMA ? "+" : "×";
        snprintf(out, tam, "R[%d,%d] = A[%d,%d] %s B[%d,%d] = %s %s %s = %s", i + 1, j + 1, i + 1, j + 1, s,
                 i + 1, j + 1, termo(g_A.v[i][j], t1, sizeof t1), s, termo(g_B.v[i][j], t2, sizeof t2), res);
        return;
    }
    n = (size_t)snprintf(out, tam, "R[%d,%d] = ", i + 1, j + 1);
    for (k = 0; k < g_A.col && n < tam; k++)
        n += (size_t)snprintf(out + n, tam - n, "%s%s×%s", k ? " + " : "",
                              termo(g_A.v[i][k], t1, sizeof t1), termo(g_B.v[k][j], t2, sizeof t2));
    if (n < tam) snprintf(out + n, tam - n, " = %s", res);
}

static void telaOperacao(Operacao op) {
    Matriz r;
    unsigned char mA[MAX_DIM][MAX_DIM], mB[MAX_DIM][MAX_DIM], mR[MAX_DIM][MAX_DIM];
    char trilha[96], tA[48], tB[48], tR[64], expl[400], msg[200], tmp[48];
    int cel, wa, wb, wr, xa, xb, xr, yR, yE, alt, li = 0, co = 0, t, k, folga;

    if (!g_A.definida || !g_B.definida) {
        tAlerta(MSG_AVISO, "Faltam dados", "Defina as matrizes A e B antes de calcular.\nUse as opções 1 e 2, ou carregue o exemplo (7).");
        return;
    }
    if (!compativel(op, &g_A, &g_B)) {
        if (op == OP_PRODUTO)
            snprintf(msg, sizeof msg, "A tem %d coluna(s) e B tem %d linha(s).\nNo produto A × B esses números precisam ser iguais.", g_A.col, g_B.lin);
        else
            snprintf(msg, sizeof msg, "A é %d × %d e B é %d × %d.\nEsta operação exige matrizes do mesmo tamanho.", g_A.lin, g_A.col, g_B.lin, g_B.col);
        tAlerta(MSG_ERRO, "Tamanhos incompatíveis", msg);
        return;
    }
    calcular(op, &g_A, &g_B, &r);
    snprintf(trilha, sizeof trilha, "Início › %s", NOME_OP[op]);
    snprintf(tmp, sizeof tmp, "A · %d×%d", g_A.lin, g_A.col);
    titulo(tA, sizeof tA, "📘", tmp);
    snprintf(tmp, sizeof tmp, "B · %d×%d", g_B.lin, g_B.col);
    titulo(tB, sizeof tB, "📙", tmp);
    snprintf(tmp, sizeof tmp, "R = A %s B · %d×%d", SIMBOLO[op], r.lin, r.col);
    titulo(tR, sizeof tR, "🎯", tmp);

    for (;;) {
        tTela(trilha, "←↑↓→ Explorar o resultado|Esc Voltar ao menu");
        tStatus(MSG_INFO, "%s", EXPLICA_OP[op]);
        cel = escolherCelula(&g_A, &g_B);
        wa = larguraMatriz(&g_A, cel);
        wb = larguraMatriz(&g_B, cel);
        wr = larguraMatriz(&r, cel);
        xa = (TELA_L - (wa + 9 + wb)) / 2;
        xb = xa + wa + 9;
        xr = (TELA_L - wr) / 2;
        alt = g_A.lin > g_B.lin ? g_A.lin : g_B.lin;
        folga = (TELA_A - 24) / 3;                    /* telas altas ganham respiro */
        if (folga > 3) folga = 3;
        yR = 2 + folga + alt + 4 + folga;
        yE = yR + r.lin + 4 + (folga > 0);
        if (yE > TELA_A - 5) yE = TELA_A - 5;
        seloOperador(xa + wa + 2, 4 + folga + (alt - 1) / 2, SIMBOLO[op], P_ROXO);
        seloOperador(xr - 7, yR + 2 + (r.lin - 1) / 2, "=", P_OK);

        for (;;) {
            memset(mA, MARCA_NADA, sizeof mA);
            memset(mB, MARCA_NADA, sizeof mB);
            memset(mR, MARCA_NADA, sizeof mR);
            mR[li][co] = MARCA_ATUAL;
            if (op == OP_PRODUTO) {
                for (k = 0; k < g_A.col; k++) { mA[li][k] = MARCA_USADA; mB[k][co] = MARCA_USADA; }
            } else {
                mA[li][co] = mB[li][co] = MARCA_USADA;
            }
            desenharMatriz(xa, 2 + folga, &g_A, tA, cel, P_AZUL, COR_TEXTO, mA, 0);
            desenharMatriz(xb, 2 + folga, &g_B, tB, cel, P_LARANJA, COR_TEXTO, mB, 0);
            desenharMatriz(xr, yR, &r, tR, cel, P_OK, COR_OK | NEGRITO, mR, 1);
            explicarCelula(op, &r, li, co, expl, sizeof expl);
            tPreenche(2, yE, TELA_L - 4, 1, EST(P_TEXTO, P_PAINEL));
            tCel(1, yE, 0x2590, 1, EST(P_PAINEL, 0));
            tCel(TELA_L - 2, yE, 0x258C, 1, EST(P_PAINEL, 0));
            tTexto(3, yE, EST(P_AVISO, P_PAINEL) | NEGRITO, "▸");
            tCampo(5, yE, TELA_L - 8, EST(P_FORTE, P_PAINEL) | NEGRITO, AL_ESQ, "%s", expl);

            t = tTecla();
            if (t == TK_REDIM) break;
            if (t >= ZN_CEL && t < ZN_CEL + MAX_DIM * MAX_DIM) { li = (t - ZN_CEL) / MAX_DIM; co = (t - ZN_CEL) % MAX_DIM; }
            else if (t == TK_CIMA && li > 0) li--;
            else if (t == TK_BAIXO && li < r.lin - 1) li++;
            else if (t == TK_ESQ && co > 0) co--;
            else if (t == TK_DIR && co < r.col - 1) co++;
            else if (t == TK_ESC || t == TK_ENTER) return;
        }
    }
}

/* ============================== EXEMPLO ============================== */

static void carregarExemplo(void) {
    int i, j;
    if ((g_A.definida || g_B.definida) &&
        !tConfirmar("Carregar exemplo", "As matrizes atuais serão substituídas\npor um exemplo 3 × 3. Continuar?", 1))
        return;
    memset(&g_A, 0, sizeof g_A);
    memset(&g_B, 0, sizeof g_B);
    g_A.lin = g_A.col = g_B.lin = g_B.col = 3;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            g_A.v[i][j] = i * 3 + j + 1;
            g_B.v[i][j] = 9 - (i * 3 + j);
        }
    g_A.definida = g_B.definida = 1;
    tAvisoPendente(MSG_OK, "Exemplo carregado: A e B são 3 × 3. Experimente as opções 4, 5 e 6.");
}

/* =============================== MAIN =============================== */

int main(void) {
    int sel = 0, op, yMenu, espaco, wMenu;
    tIniciar("🔢", "#", "Matrizes", "Questão 01 · Linguagem C");
    tAbertura("MATRIZES", "Soma e multiplicação de matrizes de até 5 × 5", "Questão 01 · Linguagem C");
    for (;;) {
        telaInicio(&yMenu, &espaco, &wMenu);
        op = tMenu(2, yMenu, wMenu, MENU, N_MENU, &sel, painelInicio, espaco);
        if (op == MENU_REDIM) continue;
        if (op == 0)      editarMatriz(&g_A, 'A');
        else if (op == 1) editarMatriz(&g_B, 'B');
        else if (op == 2) telaVisualizar();
        else if (op == 3) telaOperacao(OP_SOMA);
        else if (op == 4) telaOperacao(OP_ELEMENTO);
        else if (op == 5) telaOperacao(OP_PRODUTO);
        else if (op == 6) carregarExemplo();
        else if (tConfirmar("Sair", "Deseja encerrar o programa?\nAs matrizes digitadas não são guardadas.", 1)) break;
    }
    tFinalizar("Até a próxima!");
    return 0;
}
