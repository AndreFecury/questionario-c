/*
 * ==========================================================================
 *  QUESTÃO 02 — CALCULADORA
 * ==========================================================================
 *  Calculadora com teclado na tela (clicável) e pelo teclado do computador.
 *  Um laço de repetição processa cada tecla, e condicionais if/else if
 *  decidem a ação e a operação: adição, subtração, multiplicação e divisão.
 *
 *  Recursos:
 *    • operações sucessivas: 2 + 3 × 4 = calcula passo a passo, e o
 *      resultado sempre pode ser usado na próxima conta;
 *    • visor com dígitos grandes e expressão completa;
 *    • histórico clicável: clique num cálculo para reutilizar o resultado;
 *    • tratamento de divisão por zero, resultados fora do limite e teclas
 *      inválidas, com mensagens claras;
 *    • teclas ganham destaque quando o mouse passa e "afundam" ao clicar;
 *    • layout responsivo: teclado e histórico crescem com a janela.
 *
 *  Teclado: 0-9  ,  + - * /  Enter (=)  Backspace  Esc (limpar)  Q (sair)
 *
 *  Compilar (MinGW / w64devkit):  gcc questao02_calculadora.c -o questao02.exe
 *  Executar:                      .\questao02.exe
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

#define MAX_HIST   200
#define MAX_DIG    15         /* dígitos aceitos na entrada */
#define LIMITE     1e300      /* além disso o resultado é considerado estouro */

typedef enum { OP_NENHUMA = -1, OP_SOMA, OP_SUB, OP_MUL, OP_DIV } Operacao;
typedef struct { double a, b, r; Operacao op; } Calculo;
typedef enum { T_NUM, T_OP, T_IGUAL, T_LIMPA, T_FUNC } TipoTecla;

typedef struct {
    const char *rotulo;
    int codigo;                /* tecla equivalente no teclado */
    int col, lin, larg;        /* posição na grade 5 × 4       */
    TipoTecla tipo;
} Tecla;

static const char *SIMB[] = { "+", "−", "×", "÷" };
static const char *NOME[] = { "Adição", "Subtração", "Multiplicação", "Divisão" };

static const Tecla TECLAS[] = {
    { "7", '7', 0, 0, 1, T_NUM }, { "8", '8', 1, 0, 1, T_NUM }, { "9", '9', 2, 0, 1, T_NUM },
    { "÷", '/', 3, 0, 1, T_OP },  { "C", 'c', 4, 0, 1, T_LIMPA },
    { "4", '4', 0, 1, 1, T_NUM }, { "5", '5', 1, 1, 1, T_NUM }, { "6", '6', 2, 1, 1, T_NUM },
    { "×", '*', 3, 1, 1, T_OP },  { "⌫", TK_BS, 4, 1, 1, T_FUNC },
    { "1", '1', 0, 2, 1, T_NUM }, { "2", '2', 1, 2, 1, T_NUM }, { "3", '3', 2, 2, 1, T_NUM },
    { "−", '-', 3, 2, 1, T_OP },  { "±", 'n', 4, 2, 1, T_FUNC },
    { "0", '0', 0, 3, 2, T_NUM }, { ",", ',', 2, 3, 1, T_NUM },
    { "+", '+', 3, 3, 1, T_OP },  { "=", '=', 4, 3, 1, T_IGUAL }
};
#define N_TECLAS ((int)(sizeof TECLAS / sizeof TECLAS[0]))

/* cor de fundo e do texto de cada tipo de tecla */
static const unsigned COR_TIPO[] = { 0x2B3250, 0xBB9AF7, 0x7AA2F7, 0xF7768E, 0x464F78 };
static const unsigned TXT_TIPO[] = { 0xFFFFFF, 0x11131C, 0x11131C, 0x11131C, 0xFFFFFF };

#define ZN_TECLA  (TK_ZONA + 0x100)
#define ZN_HIST   (TK_ZONA + 0x200)
#define ZN_LIMPAR (TK_ZONA + 0x300)

/* Estado da calculadora */
static Calculo  g_hist[MAX_HIST];
static int      g_nHist;
static char     g_ent[40] = "0";          /* número exibido / em edição */
static double   g_acc;                    /* valor acumulado            */
static Operacao g_op = OP_NENHUMA;        /* operação pendente          */
static int      g_novo = 1;               /* próximo dígito inicia número novo */
static int      g_erro;
static char     g_expr[200], g_legenda[200];
static int      g_corLegenda = P_FRACO;

/* Geometria (recalculada a cada redesenho) */
static int g_wl, g_kx, g_ky, g_kw, g_kh, g_kgap, g_hx, g_hw, g_hh, g_teclaHover = -1;

/* =========================== DÍGITOS GRANDES =========================== */

typedef struct { char c; const char *px[5]; } Glifo;
static const Glifo FONTE[] = {
    { '0', { "###", "#.#", "#.#", "#.#", "###" } }, { '1', { ".#.", "##.", ".#.", ".#.", "###" } },
    { '2', { "###", "..#", "###", "#..", "###" } }, { '3', { "###", "..#", ".##", "..#", "###" } },
    { '4', { "#.#", "#.#", "###", "..#", "..#" } }, { '5', { "###", "#..", "###", "..#", "###" } },
    { '6', { "###", "#..", "###", "#.#", "###" } }, { '7', { "###", "..#", "..#", ".#.", ".#." } },
    { '8', { "###", "#.#", "###", "#.#", "###" } }, { '9', { "###", "#.#", "###", "..#", "###" } },
    { '-', { "...", "...", "###", "...", "..." } }, { '+', { "...", ".#.", "###", ".#.", "..." } },
    { ',', { "..", "..", "..", ".#", "#." } },      { '.', { ".", ".", ".", ".", "#" } },
    { 'e', { "###", "#..", "##.", "#..", "###" } }
};
#define N_FONTE ((int)(sizeof FONTE / sizeof FONTE[0]))

static const Glifo *glifo(char c) {
    int i;
    for (i = 0; i < N_FONTE; i++) if (FONTE[i].c == c) return &FONTE[i];
    return NULL;
}

/* Dígitos grandes (meios-blocos) alinhados à direita em xDir, em degradê.
 * Retorna 0 se algum caractere não tiver glifo ou o texto não couber. */
static int desenharGrande(int xDir, int y, const char *txt, int larguraMax) {
    int largura = 0, r, c, x, i = 0, n = (int)strlen(txt);
    const char *p;
    for (p = txt; *p; p++) {
        const Glifo *g = glifo(*p);
        if (!g) return 0;
        largura += (int)strlen(g->px[0]) + (p[1] ? 1 : 0);
    }
    if (largura > larguraMax) return 0;
    x = xDir - largura + 1;
    for (p = txt; *p; p++, i++) {
        const Glifo *g = glifo(*p);
        int w = (int)strlen(g->px[0]);
        unsigned cor = misturar(PALETA[P_ACENTO], PALETA[P_FORTE], n > 1 ? (double)i / (n - 1) : 1);
        for (r = 0; r < 3; r++)
            for (c = 0; c < w; c++) {
                int cima = g->px[2 * r][c] == '#';
                int baixo = 2 * r + 1 < 5 && g->px[2 * r + 1][c] == '#';
                unsigned cp = cima && baixo ? 0x2588 : cima ? 0x2580 : baixo ? 0x2584 : ' ';
                tCelRGB(x + c, y + r, cp, 1, cor, 0xFFFFFFFFu, 0, 15, -1);
            }
        x += w + 1;
    }
    return 1;
}

/* ============================ FORMATAÇÃO ============================ */

static void fmtEditavel(double v, char *out, size_t tam) {
    char *p;
    snprintf(out, tam, "%.12g", v);
    for (p = out; *p; p++) if (*p == '.') *p = ',';
}

/* "-1234,5" -> "-1.234,5" (separador de milhar na exibição). */
static void fmtExibicao(const char *ent, char *out, size_t tam) {
    const char *virg = strchr(ent, ','), *ini = ent;
    char res[80];
    int j = 0, nInt, i;
    if (strchr(ent, 'e')) { snprintf(out, tam, "%s", ent); return; }
    if (*ini == '-') res[j++] = *ini++;
    nInt = (int)((virg ? virg : ent + strlen(ent)) - ini);
    for (i = 0; i < nInt; i++) {
        if (i > 0 && (nInt - i) % 3 == 0) res[j++] = '.';
        res[j++] = ini[i];
    }
    if (virg) { strcpy(res + j, virg); j += (int)strlen(virg); }
    res[j] = '\0';
    snprintf(out, tam, "%s", res);
}

static double valorEntrada(void) {
    char tmp[40], *p;
    snprintf(tmp, sizeof tmp, "%s", g_ent);
    for (p = tmp; *p; p++) if (*p == ',') *p = '.';
    return strtod(tmp, NULL);
}

static int contarDigitos(const char *s) {
    int n = 0;
    for (; *s; s++) n += *s >= '0' && *s <= '9';
    return n;
}

/* ============================== DESENHO ============================== */

static void desenharVisor(void) {
    char exib[80];
    int x0 = 2, w = g_wl - 4;
    tCaixa(0, 2, g_wl, 7, COR_BORDA, g_emoji ? "📟 Visor" : "Visor");
    if (g_op != OP_NENHUMA) {                         /* selo da operação pendente */
        tCel(x0, 3, 0x2590, 1, EST(P_ROXO, 0));
        tTexto(x0 + 1, 3, EST(P_ESCURO, P_ROXO) | NEGRITO, " %s ", SIMB[g_op]);
        tCel(x0 + 4, 3, 0x258C, 1, EST(P_ROXO, 0));
    }
    tCampo(x0 + 6, 3, w - 6, COR_FRACO, AL_DIR, "%s", g_expr);
    if (g_erro) {
        tCampo(x0, 5, w, COR_ERRO | NEGRITO, AL_DIR, "Erro");
    } else {
        fmtExibicao(g_ent, exib, sizeof exib);
        if (!desenharGrande(x0 + w - 1, 4, exib, w))
            tCampo(x0, 5, w, COR_FORTE, AL_DIR, "%s", exib);
    }
    tCampo(x0, 7, w, EST(g_corLegenda, 0), AL_ESQ, "%s", g_legenda);
}

static void desenharTecla(int k, int estado) {        /* 0 normal, 1 hover, 2 pressionada */
    const Tecla *t = &TECLAS[k];
    int x = g_kx + t->col * (g_kw + 1), y = g_ky + t->lin * (g_kh + g_kgap);
    int w = t->larg * g_kw + (t->larg - 1), i, j;
    unsigned fundo = COR_TIPO[t->tipo], txt = TXT_TIPO[t->tipo];
    if (estado == 1) fundo = misturar(fundo, 0xFFFFFF, 0.22);
    if (estado == 2) fundo = misturar(fundo, 0x000000, 0.35);
    for (j = 0; j < w; j++) {
        tCelRGB(x + j, y, 0x2584, 1, fundo, 0xFFFFFFFFu, 0, 8, -1);            /* ▄ topo */
        tCelRGB(x + j, y + g_kh - 1, 0x2580, 1, fundo, 0xFFFFFFFFu, 0, 8, -1); /* ▀ base */
    }
    for (i = 1; i < g_kh - 1; i++)
        for (j = 0; j < w; j++) tCelRGB(x + j, y + i, ' ', 1, txt, fundo, 1, 15, 8);
    {
        const char *p = t->rotulo;
        unsigned cp = u8Prox(&p);
        tCelRGB(x + w / 2, y + (g_kh - 1) / 2, cp, 1, txt, fundo, 1, 15, 8);
    }
    tZona(x, y, w, g_kh, ZN_TECLA + k, 0);
}

static void desenharTeclado(void) {
    int k;
    for (k = 0; k < N_TECLAS; k++) desenharTecla(k, k == g_teclaHover);
}

/* Efeito de tecla "afundando" ao ser usada (mouse ou teclado). */
static void piscarTecla(int k) {
    if (k < 0) return;
    desenharTecla(k, 2);
    tPresent();
    Sleep(70);
    desenharTecla(k, k == g_teclaHover);
}

static void desenharHistorico(void) {
    char a[40], b[40], r[40], tit[48];
    int i, y = 3, max = (g_hh - 4) / 2;
    snprintf(tit, sizeof tit, "%sHistórico", g_emoji ? "🕘 " : "");
    tCaixa(g_hx, 2, g_hw, g_hh, COR_BORDA, tit);
    if (g_nHist == 0) {
        tCampo(g_hx + 1, 2 + g_hh / 2 - 1, g_hw - 2, COR_FRACO, AL_CENTRO, "Nenhum cálculo ainda");
        tCampo(g_hx + 1, 2 + g_hh / 2, g_hw - 2, COR_FRACO, AL_CENTRO, "Os resultados vêm para cá");
        return;
    }
    for (i = 0; i < max && i < g_nHist; i++, y += 2) {
        const Calculo *c = &g_hist[g_nHist - 1 - i];
        tFmtNum(c->a, a, sizeof a, 6);
        tFmtNum(c->b, b, sizeof b, 6);
        tFmtNum(c->r, r, sizeof r, 6);
        tCampo(g_hx + 2, y, g_hw - 4, COR_FRACO, AL_ESQ, "%s %s %s", a, SIMB[c->op], b);
        tCampo(g_hx + 2, y + 1, g_hw - 4, i == 0 ? COR_ACENTO | NEGRITO : COR_TEXTO, AL_DIR, "= %s", r);
        tZona(g_hx + 1, y, g_hw - 2, 2, ZN_HIST + i, EST(P_FORTE, P_CAMPO));
    }
    tCampo(g_hx + 2, 2 + g_hh - 2, g_hw - 16, COR_FRACO, AL_ESQ, "%d no total", g_nHist);
    tBotao(g_hx + g_hw - 13, 2 + g_hh - 2, g_emoji ? "🧹 Limpar" : " Limpar ", ZN_LIMPAR, P_CAMPO, 1);
}

static void desenharTudo(void) {
    int hk;
    g_hw = TELA_L * 34 / 100;
    if (g_hw < 26) g_hw = 26;
    if (g_hw > 44) g_hw = 44;
    g_wl = TELA_L - g_hw - 2;
    g_hx = g_wl + 2;
    g_hh = TELA_A - 5;
    hk = TELA_A - 12;                                 /* altura disponível p/ teclado */
    g_kgap = hk >= 4 * 3 + 3 + 2;                     /* espaço entre fileiras se couber */
    g_kh = (hk - 3 * g_kgap) / 4;
    if (g_kh < 3) g_kh = 3;
    if (g_kh > 5) g_kh = 5;
    if (g_kh % 2 == 0) g_kh--;                        /* altura ímpar: rótulo no centro */
    g_kw = (g_wl - 8) / 5;
    if (g_kw > 14) g_kw = 14;
    g_kx = (g_wl - (5 * g_kw + 4)) / 2;
    g_ky = 9 + (hk - (4 * g_kh + 3 * g_kgap)) / 2;
    if (g_ky < 9) g_ky = 9;
    tTela("Calculadora", "Enter =|Esc Limpar|⌫ Apagar|Q Sair");
    desenharVisor();
    desenharTeclado();
    desenharHistorico();
}

/* ============================== LÓGICA ============================== */

static void legenda(int cor, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_legenda, sizeof g_legenda, fmt, ap);
    va_end(ap);
    g_corLegenda = cor;
}

static void limparTudo(void) {
    strcpy(g_ent, "0");
    g_acc = 0;
    g_op = OP_NENHUMA;
    g_novo = 1;
    g_erro = 0;
    g_expr[0] = '\0';
    legenda(P_FRACO, "Pronto. Digite um número.");
}

static void registrar(double a, double b, double r, Operacao op) {
    if (g_nHist == MAX_HIST) {
        memmove(g_hist, g_hist + 1, sizeof(Calculo) * (MAX_HIST - 1));
        g_nHist--;
    }
    g_hist[g_nHist].a = a; g_hist[g_nHist].b = b; g_hist[g_nHist].r = r; g_hist[g_nHist].op = op;
    g_nHist++;
}

/* Executa a operação escolhida (if / else if). Retorna 0 em caso de erro. */
static int aplicar(double a, Operacao op, double b, double *r) {
    char va[40];
    if (op == OP_SOMA)     *r = a + b;
    else if (op == OP_SUB) *r = a - b;
    else if (op == OP_MUL) *r = a * b;
    else if (op == OP_DIV) {
        if (b == 0.0) {
            tFmtNum(a, va, sizeof va, 8);
            snprintf(g_expr, sizeof g_expr, "%s ÷ 0", va);
            legenda(P_ERRO, "✕ Não é possível dividir por zero.");
            tStatus(MSG_ERRO, "Divisão por zero não é permitida. Tecle C (ou qualquer número) para recomeçar.");
            return 0;
        }
        *r = a / b;
    } else return 0;
    if (*r != *r || *r > LIMITE || *r < -LIMITE) {
        legenda(P_ERRO, "✕ Resultado fora do limite da calculadora.");
        tStatus(MSG_ERRO, "O resultado é grande demais para ser representado.");
        return 0;
    }
    registrar(a, b, *r, op);
    return 1;
}

/* Calcula a operação pendente com o número atual. */
static int resolverPendente(void) {
    double b = valorEntrada(), r;
    char va[40], vb[40];
    tFmtNum(g_acc, va, sizeof va, 8);
    tFmtNum(b, vb, sizeof vb, 8);
    if (!aplicar(g_acc, g_op, b, &r)) { g_erro = 1; g_op = OP_NENHUMA; return 0; }
    snprintf(g_expr, sizeof g_expr, "%s %s %s =", va, SIMB[g_op], vb);
    tStatus(MSG_OK, "%s concluída e guardada no histórico.", NOME[g_op]);
    g_acc = r;
    fmtEditavel(r, g_ent, sizeof g_ent);
    return 1;
}

/* Trata uma tecla da calculadora. Retorna 0 se ela não for reconhecida. */
static int processar(int t) {
    char va[40];
    int n = (int)strlen(g_ent);
    Operacao op = t == '+' ? OP_SOMA : t == '-' ? OP_SUB : (t == '*' || t == 'x' || t == 'X') ? OP_MUL
                : t == '/' ? OP_DIV : OP_NENHUMA;

    if (g_erro && (t != TK_BS && op == OP_NENHUMA && t != '=' && t != TK_ENTER)) limparTudo();
    else if (g_erro) return 1;                         /* ignora até recomeçar */

    if (t >= '0' && t <= '9') {
        if (g_novo) { strcpy(g_ent, "0"); n = 1; g_novo = 0; if (g_op == OP_NENHUMA) g_expr[0] = '\0'; }
        if (contarDigitos(g_ent) >= MAX_DIG) { tStatus(MSG_AVISO, "Limite de %d dígitos.", MAX_DIG); return 1; }
        if (!strcmp(g_ent, "0")) n = 0;
        else if (!strcmp(g_ent, "-0")) n = 1;
        g_ent[n] = (char)t;
        g_ent[n + 1] = '\0';
        legenda(P_FRACO, g_op != OP_NENHUMA ? "Digite o segundo número e tecle =" : "Escolha uma operação: + − × ÷");
    } else if (t == ',' || t == '.') {
        if (g_novo) { strcpy(g_ent, "0,"); g_novo = 0; if (g_op == OP_NENHUMA) g_expr[0] = '\0'; }
        else if (!strchr(g_ent, ',')) strcat(g_ent, ",");
    } else if (t == TK_BS) {
        if (!g_novo) {
            g_ent[--n] = '\0';
            if (n == 0 || !strcmp(g_ent, "-")) strcpy(g_ent, "0");
        }
    } else if (t == 'n' || t == 'N') {                 /* ± */
        if (strcmp(g_ent, "0")) {
            if (g_ent[0] == '-') memmove(g_ent, g_ent + 1, strlen(g_ent));
            else { memmove(g_ent + 1, g_ent, strlen(g_ent) + 1); g_ent[0] = '-'; }
        }
    } else if (op != OP_NENHUMA) {
        if (g_op != OP_NENHUMA && !g_novo) {           /* operações sucessivas */
            if (!resolverPendente()) return 1;
        } else if (g_op == OP_NENHUMA) {
            g_acc = valorEntrada();
        }
        g_op = op;
        g_novo = 1;
        fmtEditavel(g_acc, g_ent, sizeof g_ent);
        tFmtNum(g_acc, va, sizeof va, 8);
        snprintf(g_expr, sizeof g_expr, "%s %s", va, SIMB[op]);
        legenda(P_ACENTO, "%s: digite o próximo número", NOME[op]);
    } else if (t == '=' || t == TK_ENTER) {
        if (g_op == OP_NENHUMA) { tStatus(MSG_INFO, "Escolha uma operação antes do =."); return 1; }
        if (!resolverPendente()) return 1;
        g_op = OP_NENHUMA;
        g_novo = 1;
        legenda(P_OK, "✓ Resultado. Continue a conta com + − × ÷");
    } else if (t == 'c' || t == 'C') {
        limparTudo();
        tStatus(MSG_INFO, "Calculadora limpa.");
    } else {
        return 0;
    }
    return 1;
}

/* Índice da tecla na tela equivalente a uma tecla do teclado. */
static int teclaDe(int t) {
    int k;
    if (t == TK_ENTER) t = '=';
    if (t == '.') t = ',';
    if (t == 'x' || t == 'X') t = '*';
    if (t == 'C' || t == TK_ESC) t = 'c';
    for (k = 0; k < N_TECLAS; k++) if (TECLAS[k].codigo == t) return k;
    return -1;
}

/* =============================== MAIN =============================== */

int main(void) {
    int t, k;
    tIniciar("🧮", "#", "Calculadora", "Questão 02 · Linguagem C");
    tAbertura("CALCULADORA", "Adição, subtração, multiplicação e divisão", "Questão 02 · Linguagem C");
    limparTudo();
    desenharTudo();
    g_querPassa = 1;                                   /* realce das teclas ao passar o mouse */

    for (;;) {                                         /* laço principal: uma tecla por volta */
        desenharVisor();
        t = tTecla();
        if (t == TK_REDIM) { desenharTudo(); continue; }
        if (t == TK_PASSA) {                           /* mouse sobre outra tecla */
            int novo = (g_passa >= ZN_TECLA && g_passa < ZN_TECLA + N_TECLAS) ? g_passa - ZN_TECLA : -1;
            int velho = g_teclaHover;
            g_teclaHover = novo;
            if (velho >= 0) desenharTecla(velho, 0);
            if (novo >= 0) desenharTecla(novo, 1);
            continue;
        }
        if (t == 0x232B) t = TK_BS;                    /* atalho "⌫" do rodapé */
        if (t >= ZN_TECLA && t < ZN_TECLA + N_TECLAS) { k = t - ZN_TECLA; t = TECLAS[k].codigo; }
        else k = teclaDe(t);

        if (t >= ZN_HIST && t < ZN_HIST + MAX_HIST) {  /* reutiliza um resultado */
            const Calculo *c = &g_hist[g_nHist - 1 - (t - ZN_HIST)];
            g_erro = 0;
            fmtEditavel(c->r, g_ent, sizeof g_ent);
            g_novo = 1;
            if (g_op == OP_NENHUMA) { g_expr[0] = '\0'; g_acc = c->r; }
            legenda(P_ACENTO, "Valor recuperado do histórico.");
            tStatus(MSG_INFO, "Resultado do histórico pronto para a próxima operação.");
            continue;
        }
        if (t == ZN_LIMPAR) {
            if (tConfirmar("Limpar histórico", "Todos os cálculos da lista serão apagados.\nDeseja continuar?", 0)) {
                g_nHist = 0;
                desenharHistorico();
                tStatus(MSG_OK, "Histórico apagado.");
            }
            continue;
        }
        if (t == 'q' || t == 'Q' || (t == TK_ESC && !strcmp(g_ent, "0") && g_op == OP_NENHUMA && !g_erro)) {
            if (tConfirmar("Sair", "Deseja fechar a calculadora?", 1)) break;
            continue;
        }
        if (t == TK_ESC) t = 'c';
        if (t == 'C') t = 'c';
        if (t == '.') t = ',';
        piscarTecla(k);
        if (!processar(t))
            tStatus(MSG_AVISO, "Tecla não reconhecida. Use números, + − * /, Enter (=), Esc (limpar) ou Q (sair).");
        if (g_nHist) desenharHistorico();
    }
    tFinalizar("Obrigado por usar a calculadora!");
    return 0;
}
