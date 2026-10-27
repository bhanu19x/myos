#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

volatile uint16_t *vga = (uint16_t *)0xB8000;
int row = 0;
int col = 0;
uint8_t color = 0x0A;
uint8_t theme = 0x0A;

/* ---------- port input/output ---------- */

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ---------- screen ---------- */

int gfx_on = 0;
void gfx_clear(void);
void gfx_print_char(char c);

void clear_screen(void) {
    if (gfx_on) {
        gfx_clear();
        return;
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (uint16_t)' ' | (0x0F << 8);
    row = 0;
    col = 0;
}

void scroll(void) {
    for (int r = 1; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            vga[(r - 1) * VGA_WIDTH + c] = vga[r * VGA_WIDTH + c];
    for (int c = 0; c < VGA_WIDTH; c++)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = (uint16_t)' ' | (0x0F << 8);
    row = VGA_HEIGHT - 1;
}

void print_char(char c) {
    if (gfx_on) {
        gfx_print_char(c);
        return;
    }
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            vga[row * VGA_WIDTH + col] = (uint16_t)' ' | (0x0F << 8);
        }
    } else {
                vga[row * VGA_WIDTH + col] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
        col++;
        if (col >= VGA_WIDTH) {
            col = 0;
            row++;
        }
    }
    if (row >= VGA_HEIGHT)
        scroll();
}

void print(const char *s) {
    for (int i = 0; s[i]; i++)
        print_char(s[i]);
}

void print_int(int n) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        print_char('0');
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n = n / 10;
    }
    while (i > 0)
        print_char(buf[--i]);
}

/* ---------- GDT (memory rulebook) ---------- */

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gp;

extern void gdt_flush(uint32_t);

void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].access    = access;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_flush((uint32_t)&gp);
}

/* ---------- IDT (doorbell list) ---------- */

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr ip;

extern void idt_load(uint32_t);
extern void isr_timer(void);
extern void isr_keyboard(void);

void idt_set(int n, uint32_t handler) {
    idt[n].base_low  = handler & 0xFFFF;
    idt[n].sel       = 0x08;
    idt[n].zero      = 0;
    idt[n].flags     = 0x8E;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void) {
    idt_set(32, (uint32_t)isr_timer);
    idt_set(33, (uint32_t)isr_keyboard);
    ip.limit = sizeof(idt) - 1;
    ip.base = (uint32_t)&idt;
    idt_load((uint32_t)&ip);
}

/* ---------- PIC (doorbell collector) and PIT (timer) ---------- */

void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void pit_init(void) {
    uint32_t divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

/* ---------- interrupt handlers ---------- */

volatile uint32_t ticks = 0;

void timer_handler(void) {
    ticks++;
    outb(0x20, 0x20);
}

const char scancode_map[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

const char shifted_map[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

volatile int shift_down = 0;
volatile char key_buf[256];
volatile uint8_t kb_head = 0;
volatile uint8_t kb_tail = 0;

volatile int extended = 0;

void keyboard_handler(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        extended = 1;
    } else if (extended) {
        extended = 0;
        if (sc == 0x48) {        // Up
            key_buf[kb_head++] = 1;
        } else if (sc == 0x50) { // Down
            key_buf[kb_head++] = 2;
        } else if (sc == 0x4B) { // Left
            key_buf[kb_head++] = 3;
        } else if (sc == 0x4D) { // Right
            key_buf[kb_head++] = 4;
        } else if (sc == 0x47) { // Home
            key_buf[kb_head++] = 5;
        } else if (sc == 0x4F) { // End
            key_buf[kb_head++] = 6;
        } else if (sc == 0x53) { // Delete
            key_buf[kb_head++] = 7;
        }
    } else if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
    } else if (sc == 0xAA || sc == 0xB6) {
        shift_down = 0;
    } else if (!(sc & 0x80) && sc < 58) {
        char ch = shift_down ? shifted_map[sc] : scancode_map[sc];
        if (ch != 0) {
            key_buf[kb_head++] = ch;
        }
    }
    outb(0x20, 0x20);
}

int has_key(void) {
    return kb_head != kb_tail;
}

char check_key(void) {
    if (kb_head == kb_tail)
        return 0;
    char c = key_buf[kb_tail++];
    return c;
}

char read_key(void) {
    while (kb_head == kb_tail)
        __asm__ volatile ("hlt");
    char c = key_buf[kb_tail++];
    return c;
}

void sleep_ticks(uint32_t count) {
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) < count) {
        __asm__ volatile ("hlt");
    }
}

/* ---------- memory allocator ---------- */

uint32_t heap_start = 0x200000;
uint32_t heap_next  = 0x200000;
uint32_t heap_end   = 0x300000;

void *kmalloc(uint32_t size) {
    size = (size + 7) & ~7;
    if (heap_next + size > heap_end)
        return 0;
    void *p = (void *)heap_next;
    heap_next += size;
    return p;
}

void print_hex(uint32_t n) {
    const char *digits = "0123456789ABCDEF";
    print("0x");
    for (int i = 28; i >= 0; i -= 4)
        print_char(digits[(n >> i) & 0xF]);
}

/* ---------- shell ---------- */

int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i])
            return 0;
        i++;
    }
    return 1;
}
/* ---------- RAM file system ---------- */

#define MAX_FILES 16
#define NAME_LEN  16
#define FILE_SIZE 1024

struct file {
    int used;
    char name[NAME_LEN];
    char *data;
    int size;
};

struct file files[MAX_FILES];

int str_len(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

int fs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used && str_equal(files[i].name, name))
            return i;
    return -1;
}

int fs_create(const char *name) {
    int len = str_len(name);
    if (len == 0 || len >= NAME_LEN)
        return -1;
    if (fs_find(name) >= 0)
        return -2;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            if (files[i].data == 0) {
                files[i].data = (char *)kmalloc(FILE_SIZE);
                if (files[i].data == 0)
                    return -3;
            }
            for (int j = 0; j < NAME_LEN; j++)
                files[i].name[j] = 0;
            for (int j = 0; name[j]; j++)
                files[i].name[j] = name[j];
            files[i].data[0] = 0;
            files[i].size = 0;
            files[i].used = 1;
            return i;
        }
    }
    return -3;
}

void fs_init(void) {
    int idx = fs_create("notes");
    if (idx >= 0) {
        const char *msg = "Welcome to MyOS Text Editor!\nType notes here and press ESC to save.\nEnjoy Batch 2 programs!";
        int len = str_len(msg);
        for (int i = 0; i < len; i++)
            files[idx].data[i] = msg[i];
        files[idx].data[len] = 0;
        files[idx].size = len;
    }
}

/* ---------- looks ---------- */

void print_repeat(char c, int n) {
    for (int i = 0; i < n; i++)
        print_char(c);
}

void banner(void) {
    color = 0x0B;
    print_char((char)0xC9); print_repeat((char)0xCD, 36); print_char((char)0xBB); print_char('\n');

    print_char((char)0xBA);
    color = 0x0F;
    print("          MY OWN OS v0.3            ");
    color = 0x0B;
    print_char((char)0xBA); print_char('\n');

    print_char((char)0xBA);
    color = 0x0E;
    print("  GUI - Snake - Pong - Edit - Sys   ");
    color = 0x0B;
    print_char((char)0xBA); print_char('\n');

    print_char((char)0xC8); print_repeat((char)0xCD, 36); print_char((char)0xBC); print_char('\n');

    color = 0x07;
    print("  Type gui, snake, pong, or help.\n\n");
    color = theme;
}

/* ---------- graphics (VGA mode 13h: 320x200, 256 colors) ---------- */

uint8_t mode13h_regs[61] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

volatile uint8_t *fb = (uint8_t *)0xA0000;

void set_mode13h(void) {
    uint8_t *r = mode13h_regs;
    outb(0x3C2, *r++);
    for (int i = 0; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, *r++); }
    outb(0x3D4, 0x03); outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80);
    r[0x03] |= 0x80;
    r[0x11] &= ~0x80;
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, *r++); }
    for (int i = 0; i < 9; i++) { outb(0x3CE, i); outb(0x3CF, *r++); }
    for (int i = 0; i < 21; i++) { inb(0x3DA); outb(0x3C0, i); outb(0x3C0, *r++); }
    inb(0x3DA);
    outb(0x3C0, 0x20);
}

void set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, idx);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

void put_pixel(int x, int y, uint8_t c) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200)
        return;
    fb[y * 320 + x] = c;
}

void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put_pixel(x + i, y + j, c);
}

void draw_scene(void) {
    for (int y = 0; y < 200; y++) {
        uint8_t c = 32 + (y * 63) / 199;
        for (int x = 0; x < 320; x++)
            fb[y * 320 + x] = c;
    }
    for (int dy = -15; dy <= 15; dy++)
        for (int dx = -15; dx <= 15; dx++)
            if (dx * dx + dy * dy <= 225)
                put_pixel(250 + dx, 40 + dy, 100);
    for (int x = 0; x < 320; x++) {
        int m = x % 80;
        int h = 150 + (m < 40 ? m / 4 : (80 - m) / 4);
        for (int y = h; y < 200; y++)
            put_pixel(x, y, 101);
    }
    fill_rect(0, 180, 320, 20, 102);
    fill_rect(20, 20, 110, 60, 103);
    fill_rect(20, 20, 110, 10, 104);
    fill_rect(24, 34, 102, 42, 105);
    fill_rect(30, 40, 40, 6, 106);
    fill_rect(30, 52, 70, 6, 100);
    fill_rect(30, 64, 55, 6, 104);
}

void gfx_demo(void) {
    set_mode13h();
    for (int i = 0; i < 64; i++)
        set_color(32 + i, i / 2, 10 + (i * 3) / 4, 40 + i / 3);
    set_color(100, 63, 55, 10);
    set_color(101, 8, 35, 12);
    set_color(102, 14, 48, 18);
    set_color(103, 45, 45, 48);
    set_color(104, 10, 20, 50);
    set_color(105, 63, 63, 63);
    set_color(106, 63, 10, 10);

    int px = 150, py = 130;
    draw_scene();
    fill_rect(px, py, 12, 12, 106);

    while (1) {
        char c = read_key();
        if (c == 'a' && px > 0)   px -= 4;
        if (c == 'd' && px < 308) px += 4;
        if (c == 'w' && py > 0)   py -= 4;
        if (c == 's' && py < 188) py += 4;
        if (c == 'q')
            outb(0x64, 0xFE);
        draw_scene();
        fill_rect(px, py, 12, 12, 106);
    }
}
/* ---------- graphical console (text drawn on top of a picture) ---------- */

#define GCOLS 53
#define GROWS 25

volatile uint8_t *bg = (uint8_t *)0x400000;
uint8_t gtext[GROWS][GCOLS];
uint8_t gcol[GROWS][GCOLS];

const uint8_t font5x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 */
    {0x00,0x20,0x20,0x20,0x20,0x00,0x20,0x00}, /* 0x21 */
    {0x00,0x50,0x50,0x50,0x00,0x00,0x00,0x00}, /* 0x22 */
    {0x50,0x50,0xF8,0x50,0xF8,0x50,0x50,0x00}, /* 0x23 */
    {0x20,0x70,0xA0,0x70,0x28,0x70,0x20,0x00}, /* 0x24 */
    {0x00,0x40,0x50,0x20,0x50,0x10,0x00,0x00}, /* 0x25 */
    {0x40,0xA0,0xA0,0x40,0xA0,0xA0,0x50,0x00}, /* 0x26 */
    {0x00,0x20,0x20,0x20,0x00,0x00,0x00,0x00}, /* 0x27 */
    {0x00,0x20,0x40,0x40,0x40,0x40,0x20,0x00}, /* 0x28 */
    {0x00,0x40,0x20,0x20,0x20,0x20,0x40,0x00}, /* 0x29 */
    {0x00,0x00,0x90,0x60,0xF0,0x60,0x90,0x00}, /* 0x2A */
    {0x00,0x00,0x20,0x20,0xF8,0x20,0x20,0x00}, /* 0x2B */
    {0x00,0x00,0x00,0x00,0x00,0x30,0x20,0x40}, /* 0x2C */
    {0x00,0x00,0x00,0x00,0xF0,0x00,0x00,0x00}, /* 0x2D */
    {0x00,0x00,0x00,0x00,0x00,0x20,0x70,0x20}, /* 0x2E */
    {0x00,0x10,0x10,0x20,0x40,0x80,0x80,0x00}, /* 0x2F */
    {0x00,0x20,0x50,0x50,0x50,0x50,0x20,0x00}, /* 0x30 */
    {0x00,0x20,0x60,0x20,0x20,0x20,0x70,0x00}, /* 0x31 */
    {0x00,0x60,0x90,0x10,0x60,0x80,0xF0,0x00}, /* 0x32 */
    {0x00,0xF0,0x20,0x60,0x10,0x90,0x60,0x00}, /* 0x33 */
    {0x00,0x20,0x60,0xA0,0xF0,0x20,0x20,0x00}, /* 0x34 */
    {0x00,0xF0,0x80,0xE0,0x10,0x90,0x60,0x00}, /* 0x35 */
    {0x00,0x60,0x80,0xE0,0x90,0x90,0x60,0x00}, /* 0x36 */
    {0x00,0xF0,0x10,0x20,0x20,0x40,0x40,0x00}, /* 0x37 */
    {0x00,0x60,0x90,0x60,0x90,0x90,0x60,0x00}, /* 0x38 */
    {0x00,0x60,0x90,0x90,0x70,0x10,0x60,0x00}, /* 0x39 */
    {0x00,0x00,0x60,0x60,0x00,0x60,0x60,0x00}, /* 0x3A */
    {0x00,0x00,0x30,0x30,0x00,0x30,0x20,0x40}, /* 0x3B */
    {0x00,0x10,0x20,0x40,0x40,0x20,0x10,0x00}, /* 0x3C */
    {0x00,0x00,0x00,0xF0,0x00,0xF0,0x00,0x00}, /* 0x3D */
    {0x00,0x40,0x20,0x10,0x10,0x20,0x40,0x00}, /* 0x3E */
    {0x00,0x20,0x50,0x10,0x20,0x00,0x20,0x00}, /* 0x3F */
    {0x30,0x48,0x98,0xA8,0xA8,0x90,0x40,0x30}, /* 0x40 */
    {0x00,0x60,0x90,0x90,0xF0,0x90,0x90,0x00}, /* 0x41 */
    {0x00,0xE0,0x90,0xE0,0x90,0x90,0xE0,0x00}, /* 0x42 */
    {0x00,0x60,0x90,0x80,0x80,0x90,0x60,0x00}, /* 0x43 */
    {0x00,0xE0,0x90,0x90,0x90,0x90,0xE0,0x00}, /* 0x44 */
    {0x00,0xF0,0x80,0xE0,0x80,0x80,0xF0,0x00}, /* 0x45 */
    {0x00,0xF0,0x80,0xE0,0x80,0x80,0x80,0x00}, /* 0x46 */
    {0x00,0x60,0x90,0x80,0xB0,0x90,0x60,0x00}, /* 0x47 */
    {0x00,0x90,0x90,0xF0,0x90,0x90,0x90,0x00}, /* 0x48 */
    {0x00,0x70,0x20,0x20,0x20,0x20,0x70,0x00}, /* 0x49 */
    {0x00,0x70,0x20,0x20,0x20,0xA0,0x40,0x00}, /* 0x4A */
    {0x00,0x90,0xA0,0xC0,0xA0,0xA0,0x90,0x00}, /* 0x4B */
    {0x00,0x80,0x80,0x80,0x80,0x80,0xF0,0x00}, /* 0x4C */
    {0x00,0x90,0xF0,0xF0,0x90,0x90,0x90,0x00}, /* 0x4D */
    {0x00,0x90,0xD0,0xF0,0xB0,0xB0,0x90,0x00}, /* 0x4E */
    {0x00,0x60,0x90,0x90,0x90,0x90,0x60,0x00}, /* 0x4F */
    {0x00,0xE0,0x90,0x90,0xE0,0x80,0x80,0x00}, /* 0x50 */
    {0x00,0x60,0x90,0x90,0xD0,0xB0,0x60,0x10}, /* 0x51 */
    {0x00,0xE0,0x90,0x90,0xE0,0x90,0x90,0x00}, /* 0x52 */
    {0x00,0x60,0x90,0x40,0x20,0x90,0x60,0x00}, /* 0x53 */
    {0x00,0x70,0x20,0x20,0x20,0x20,0x20,0x00}, /* 0x54 */
    {0x00,0x90,0x90,0x90,0x90,0x90,0x60,0x00}, /* 0x55 */
    {0x00,0x90,0x90,0x90,0x90,0x60,0x60,0x00}, /* 0x56 */
    {0x00,0x90,0x90,0x90,0xF0,0xF0,0x90,0x00}, /* 0x57 */
    {0x00,0x90,0x90,0x60,0x60,0x90,0x90,0x00}, /* 0x58 */
    {0x00,0x88,0x88,0x50,0x20,0x20,0x20,0x00}, /* 0x59 */
    {0x00,0xF0,0x10,0x20,0x40,0x80,0xF0,0x00}, /* 0x5A */
    {0x00,0x70,0x40,0x40,0x40,0x40,0x70,0x00}, /* 0x5B */
    {0x00,0x80,0x80,0x40,0x20,0x10,0x10,0x00}, /* 0x5C */
    {0x00,0x70,0x10,0x10,0x10,0x10,0x70,0x00}, /* 0x5D */
    {0x00,0x20,0x50,0x00,0x00,0x00,0x00,0x00}, /* 0x5E */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0}, /* 0x5F */
    {0x00,0x40,0x20,0x00,0x00,0x00,0x00,0x00}, /* 0x60 */
    {0x00,0x00,0x00,0x70,0x90,0x90,0x70,0x00}, /* 0x61 */
    {0x00,0x80,0x80,0xE0,0x90,0x90,0xE0,0x00}, /* 0x62 */
    {0x00,0x00,0x00,0x30,0x40,0x40,0x30,0x00}, /* 0x63 */
    {0x00,0x10,0x10,0x70,0x90,0x90,0x70,0x00}, /* 0x64 */
    {0x00,0x00,0x00,0x60,0xB0,0xC0,0x60,0x00}, /* 0x65 */
    {0x00,0x20,0x50,0x40,0xE0,0x40,0x40,0x00}, /* 0x66 */
    {0x00,0x00,0x00,0x60,0x90,0x70,0x10,0x60}, /* 0x67 */
    {0x00,0x80,0x80,0xE0,0x90,0x90,0x90,0x00}, /* 0x68 */
    {0x00,0x20,0x00,0x60,0x20,0x20,0x70,0x00}, /* 0x69 */
    {0x00,0x10,0x00,0x10,0x10,0x10,0x50,0x20}, /* 0x6A */
    {0x00,0x80,0x80,0x90,0xE0,0x90,0x90,0x00}, /* 0x6B */
    {0x00,0x60,0x20,0x20,0x20,0x20,0x70,0x00}, /* 0x6C */
    {0x00,0x00,0x00,0xD0,0xA8,0xA8,0xA8,0x00}, /* 0x6D */
    {0x00,0x00,0x00,0xE0,0x90,0x90,0x90,0x00}, /* 0x6E */
    {0x00,0x00,0x00,0x60,0x90,0x90,0x60,0x00}, /* 0x6F */
    {0x00,0x00,0x00,0xE0,0x90,0xE0,0x80,0x80}, /* 0x70 */
    {0x00,0x00,0x00,0x70,0x90,0x70,0x10,0x10}, /* 0x71 */
    {0x00,0x00,0x00,0xA0,0xD0,0x80,0x80,0x00}, /* 0x72 */
    {0x00,0x00,0x00,0x30,0x60,0x10,0x60,0x00}, /* 0x73 */
    {0x00,0x40,0x40,0xE0,0x40,0x50,0x20,0x00}, /* 0x74 */
    {0x00,0x00,0x00,0x90,0x90,0x90,0x70,0x00}, /* 0x75 */
    {0x00,0x00,0x00,0x50,0x50,0x50,0x20,0x00}, /* 0x76 */
    {0x00,0x00,0x00,0x88,0xA8,0xA8,0x50,0x00}, /* 0x77 */
    {0x00,0x00,0x00,0x90,0x60,0x60,0x90,0x00}, /* 0x78 */
    {0x00,0x00,0x00,0x90,0x90,0x70,0x10,0x60}, /* 0x79 */
    {0x00,0x00,0x00,0xF0,0x20,0x40,0xF0,0x00}, /* 0x7A */
    {0x30,0x40,0x20,0xC0,0x20,0x40,0x30,0x00}, /* 0x7B */
    {0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x00}, /* 0x7C */
    {0xC0,0x20,0x40,0x30,0x40,0x20,0xC0,0x00}, /* 0x7D */
    {0x00,0x50,0xA0,0x00,0x00,0x00,0x00,0x00}, /* 0x7E */
};

void gfx_palette(void) {
    set_color(0,  0,  0,  0);
    set_color(1,  0,  0,  42);
    set_color(2,  0,  42, 0);
    set_color(3,  0,  42, 42);
    set_color(4,  42, 0,  0);
    set_color(5,  42, 0,  42);
    set_color(6,  42, 21, 0);
    set_color(7,  42, 42, 42);
    set_color(8,  21, 21, 21);
    set_color(9,  21, 21, 63);
    set_color(10, 21, 63, 21);
    set_color(11, 21, 63, 63);
    set_color(12, 63, 21, 21);
    set_color(13, 63, 21, 63);
    set_color(14, 63, 63, 21);
    set_color(15, 63, 63, 63);
    for (int i = 0; i < 64; i++)
        set_color(32 + i, i / 4, 6 + i / 2, 30 + i / 2);
    set_color(100, 63, 55, 10);
    set_color(101, 4, 22, 9);
    set_color(102, 6, 30, 11);
}

void bg_pixel(int x, int y, uint8_t c) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200)
        return;
    bg[y * 320 + x] = c;
}

void build_background(void) {
    for (int y = 0; y < 200; y++) {
        uint8_t c = 32 + (y * 63) / 199;
        for (int x = 0; x < 320; x++)
            bg[y * 320 + x] = c;
    }
    for (int dy = -15; dy <= 15; dy++)
        for (int dx = -15; dx <= 15; dx++)
            if (dx * dx + dy * dy <= 225)
                bg_pixel(260 + dx, 36 + dy, 100);
    for (int x = 0; x < 320; x++) {
        int m = x % 80;
        int h = 150 + (m < 40 ? m / 4 : (80 - m) / 4);
        for (int y = h; y < 200; y++)
            bg_pixel(x, y, 101);
    }
    for (int y = 180; y < 200; y++)
        for (int x = 0; x < 320; x++)
            bg_pixel(x, y, 102);
}

void gfx_cell(int r, int c) {
    int px = 1 + c * 6;
    int py = r * 8;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 6; i++)
            fb[(py + j) * 320 + px + i] = bg[(py + j) * 320 + px + i];
    uint8_t ch = gtext[r][c];
    if (ch <= 32 || ch > 126)
        return;
    const uint8_t *g = font5x8[ch - 32];
    uint8_t fg = gcol[r][c] & 0x0F;
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 5; i++)
            if (g[j] & (0x80 >> i))
                fb[(py + j + 1) * 320 + px + i + 1] = 0;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 5; i++)
            if (g[j] & (0x80 >> i))
                fb[(py + j) * 320 + px + i] = fg;
}

void gfx_cursor(void) {
    int px = 1 + col * 6;
    int py = row * 8;
    for (int i = 0; i < 5; i++)
        fb[(py + 7) * 320 + px + i] = 15;
}

void gfx_redraw(void) {
    for (int r = 0; r < GROWS; r++)
        for (int c = 0; c < GCOLS; c++)
            gfx_cell(r, c);
}

void gfx_print_char(char ch) {
    uint8_t c = (uint8_t)ch;
    if (c == 0xC9 || c == 0xBB || c == 0xC8 || c == 0xBC)
        c = '+';
    else if (c == 0xCD)
        c = '=';
    else if (c == 0xBA)
        c = '|';

    gfx_cell(row, col);

    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            gtext[row][col] = ' ';
            gfx_cell(row, col);
        }
    } else {
        gtext[row][col] = c;
        gcol[row][col] = color;
        gfx_cell(row, col);
        col++;
        if (col >= GCOLS) {
            col = 0;
            row++;
        }
    }

    if (row >= GROWS) {
        for (int r = 1; r < GROWS; r++) {
            for (int k = 0; k < GCOLS; k++) {
                gtext[r - 1][k] = gtext[r][k];
                gcol[r - 1][k] = gcol[r][k];
            }
        }
        for (int k = 0; k < GCOLS; k++) {
            gtext[GROWS - 1][k] = ' ';
            gcol[GROWS - 1][k] = 0x0A;
        }
        row = GROWS - 1;
        gfx_redraw();
    }
    gfx_cursor();
}

void gfx_clear(void) {
    for (int r = 0; r < GROWS; r++) {
        for (int c = 0; c < GCOLS; c++) {
            gtext[r][c] = ' ';
            gcol[r][c] = 0x0A;
        }
    }
    row = 0;
    col = 0;
    gfx_redraw();
    gfx_cursor();
}

void gfx_console_start(void) {
    set_mode13h();
    gfx_palette();
    build_background();
    for (int i = 0; i < 64000; i++)
        fb[i] = bg[i];
    for (int r = 0; r < GROWS; r++) {
        for (int c = 0; c < GCOLS; c++) {
            gtext[r][c] = ' ';
            gcol[r][c] = 0x0A;
        }
    }
    row = 0;
    col = 0;
    gfx_on = 1;
    gfx_cursor();
}


int parse_int(const char **p) {
    while (**p == ' ')
        (*p)++;
    int neg = 0;
    if (**p == '-') {
        neg = 1;
        (*p)++;
    }
    int n = 0;
    while (**p >= '0' && **p <= '9') {
        n = n * 10 + (**p - '0');
        (*p)++;
    }
    return neg ? -n : n;
}

uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

int rtc_val(uint8_t reg) {
    while (rtc_read(0x0A) & 0x80)
        ;
    uint8_t v = rtc_read(reg);
    if (!(rtc_read(0x0B) & 0x04))
        v = (v & 0x0F) + (v >> 4) * 10;
    return v;
}

void print_2d(int n) {
    print_char('0' + n / 10);
    print_char('0' + n % 10);
}

/* ---------- Graphics Primitives & Text in Mode 13h ---------- */

void draw_char(int px, int py, char ch, uint8_t fg, uint8_t bg_col) {
    uint8_t c = (uint8_t)ch;
    if (c < 32 || c > 126)
        return;
    const uint8_t *g = font5x8[c - 32];
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 5; i++) {
            if (g[j] & (0x80 >> i))
                put_pixel(px + i, py + j, fg);
            else if (bg_col != 0xFF)
                put_pixel(px + i, py + j, bg_col);
        }
    }
}

void draw_string(int px, int py, const char *s, uint8_t fg, uint8_t bg_col) {
    while (*s) {
        draw_char(px, py, *s, fg, bg_col);
        px += 6;
        s++;
    }
}

void draw_int(int px, int py, int n, uint8_t fg, uint8_t bg_col) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        draw_char(px, py, '0', fg, bg_col);
        return;
    }
    if (n < 0) {
        draw_char(px, py, '-', fg, bg_col);
        px += 6;
        n = -n;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        draw_char(px, py, buf[--i], fg, bg_col);
        px += 6;
    }
}

void draw_rect_border(int x, int y, int w, int h, uint8_t c) {
    for (int i = 0; i < w; i++) {
        put_pixel(x + i, y, c);
        put_pixel(x + i, y + h - 1, c);
    }
    for (int j = 0; j < h; j++) {
        put_pixel(x, y + j, c);
        put_pixel(x + w - 1, y + j, c);
    }
}

/* ---------- Snake Game ---------- */

#define SNAKE_GW 32
#define SNAKE_GH 20
#define MAX_SNAKE 400
#define SNAKE_BX 32
#define SNAKE_BY 22

static int snake_x[MAX_SNAKE];
static int snake_y[MAX_SNAKE];
static uint32_t rng_seed = 987654321;

static uint32_t snake_rand(void) {
    rng_seed = rng_seed * 1103515245 + 12345 + ticks;
    return (rng_seed >> 16) & 0x7FFF;
}

static void snake_draw_cell(int gx, int gy, int type, int dir) {
    int px = SNAKE_BX + gx * 8;
    int py = SNAKE_BY + gy * 8;

    if (type == 0) {
        fill_rect(px, py, 8, 8, 0);
        put_pixel(px + 3, py + 3, 8);
    } else if (type == 1) {
        fill_rect(px + 1, py + 1, 6, 6, 14);
        if (dir == 0) {
            put_pixel(px + 2, py + 2, 0);
            put_pixel(px + 5, py + 2, 0);
        } else if (dir == 2) {
            put_pixel(px + 2, py + 5, 0);
            put_pixel(px + 5, py + 5, 0);
        } else if (dir == 3) {
            put_pixel(px + 2, py + 2, 0);
            put_pixel(px + 2, py + 5, 0);
        } else {
            put_pixel(px + 5, py + 2, 0);
            put_pixel(px + 5, py + 5, 0);
        }
    } else if (type == 2) {
        fill_rect(px + 1, py + 1, 6, 6, 10);
    } else if (type == 3) {
        fill_rect(px + 1, py + 1, 6, 6, 2);
    } else if (type == 4) {
        fill_rect(px + 2, py + 2, 4, 4, 12);
        put_pixel(px + 3, py + 1, 10);
        put_pixel(px + 3, py + 3, 15);
    } else if (type == 5) {
        fill_rect(px + 1, py + 1, 6, 6, 14);
        put_pixel(px + 3, py + 0, 10);
        put_pixel(px + 2, py + 2, 15);
    }
}

void snake_game(void) {
    static int high_score = 0;

play_again:
    set_mode13h();
    gfx_palette();

    fill_rect(0, 0, 320, 200, 0);

    draw_string(24, 7, "SNAKE OS", 10, 0);
    draw_string(115, 7, "SCORE:", 14, 0);
    draw_int(155, 7, 0, 15, 0);
    draw_string(215, 7, "HIGH:", 11, 0);
    draw_int(250, 7, high_score, 11, 0);

    draw_rect_border(SNAKE_BX - 2, SNAKE_BY - 2, SNAKE_GW * 8 + 4, SNAKE_GH * 8 + 4, 9);
    draw_rect_border(SNAKE_BX - 1, SNAKE_BY - 1, SNAKE_GW * 8 + 2, SNAKE_GH * 8 + 2, 1);

    for (int y = 0; y < SNAKE_GH; y++) {
        for (int x = 0; x < SNAKE_GW; x++) {
            snake_draw_cell(x, y, 0, 0);
        }
    }

    draw_string(20, 188, "[ARROWS/WASD]: Move   [P]: Pause   [ESC]: Exit", 7, 0);

    int len = 4;
    int dir = 1;
    int next_dir = 1;
    int score = 0;
    int delay = 9;
    int paused = 0;

    int food_x = 22;
    int food_y = 10;
    int bonus_x = -1;
    int bonus_y = -1;
    int bonus_timer = 0;
    int food_count = 0;

    for (int i = 0; i < len; i++) {
        snake_x[i] = 14 - i;
        snake_y[i] = 10;
    }

    snake_draw_cell(food_x, food_y, 4, 0);
    for (int i = len - 1; i > 0; i--) {
        snake_draw_cell(snake_x[i], snake_y[i], (i % 2) ? 3 : 2, 0);
    }
    snake_draw_cell(snake_x[0], snake_y[0], 1, dir);

    while (has_key()) check_key();

    while (1) {
        while (has_key()) {
            char c = check_key();
            if (c == 1 || c == 'w' || c == 'W') {
                if (dir != 2) next_dir = 0;
            } else if (c == 2 || c == 's' || c == 'S') {
                if (dir != 0) next_dir = 2;
            } else if (c == 3 || c == 'a' || c == 'A') {
                if (dir != 1) next_dir = 3;
            } else if (c == 4 || c == 'd' || c == 'D') {
                if (dir != 3) next_dir = 1;
            } else if (c == 'p' || c == 'P' || c == ' ') {
                paused = !paused;
                if (paused) {
                    fill_rect(120, 92, 80, 16, 4);
                    draw_rect_border(120, 92, 80, 16, 12);
                    draw_string(136, 96, "PAUSED", 14, 4);
                } else {
                    for (int gy = 8; gy <= 12; gy++) {
                        for (int gx = 12; gx <= 20; gx++) {
                            int is_snake = 0;
                            for (int si = 0; si < len; si++) {
                                if (snake_x[si] == gx && snake_y[si] == gy) {
                                    snake_draw_cell(gx, gy, (si == 0) ? 1 : ((si % 2) ? 3 : 2), dir);
                                    is_snake = 1;
                                    break;
                                }
                            }
                            if (!is_snake) {
                                if (food_x == gx && food_y == gy)
                                    snake_draw_cell(gx, gy, 4, 0);
                                else if (bonus_x == gx && bonus_y == gy)
                                    snake_draw_cell(gx, gy, 5, 0);
                                else
                                    snake_draw_cell(gx, gy, 0, 0);
                            }
                        }
                    }
                }
            } else if (c == 27 || c == 'q' || c == 'Q') {
                gfx_console_start();
                banner();
                return;
            }
        }

        if (paused) {
            sleep_ticks(5);
            continue;
        }

        dir = next_dir;

        int nx = snake_x[0];
        int ny = snake_y[0];
        if (dir == 0) ny--;
        else if (dir == 1) nx++;
        else if (dir == 2) ny++;
        else if (dir == 3) nx--;

        if (nx < 0 || nx >= SNAKE_GW || ny < 0 || ny >= SNAKE_GH) {
            break;
        }

        int collided = 0;
        for (int i = 0; i < len - 1; i++) {
            if (snake_x[i] == nx && snake_y[i] == ny) {
                collided = 1;
                break;
            }
        }
        if (collided) break;

        int ate_food = 0;
        if (nx == food_x && ny == food_y) {
            ate_food = 1;
            score += 10;
            food_count++;
            if (len < MAX_SNAKE - 1) len++;

            int tries = 0;
            while (tries < 500) {
                food_x = snake_rand() % SNAKE_GW;
                food_y = snake_rand() % SNAKE_GH;
                int on_s = 0;
                for (int i = 0; i < len; i++) {
                    if (snake_x[i] == food_x && snake_y[i] == food_y) {
                        on_s = 1;
                        break;
                    }
                }
                if (!on_s && !(food_x == bonus_x && food_y == bonus_y))
                    break;
                tries++;
            }
            snake_draw_cell(food_x, food_y, 4, 0);

            if (food_count % 4 == 0 && bonus_timer <= 0) {
                bonus_x = snake_rand() % SNAKE_GW;
                bonus_y = snake_rand() % SNAKE_GH;
                bonus_timer = 70;
                snake_draw_cell(bonus_x, bonus_y, 5, 0);
            }

            if (delay > 4 && (score % 30 == 0)) {
                delay--;
            }
        }

        if (bonus_timer > 0 && nx == bonus_x && ny == bonus_y) {
            score += 50;
            bonus_timer = 0;
            bonus_x = -1;
            bonus_y = -1;
        }

        if (bonus_timer > 0) {
            bonus_timer--;
            if (bonus_timer == 0) {
                snake_draw_cell(bonus_x, bonus_y, 0, 0);
                bonus_x = -1;
                bonus_y = -1;
            }
        }

        if (!ate_food) {
            snake_draw_cell(snake_x[len - 1], snake_y[len - 1], 0, 0);
        }

        for (int i = len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i - 1];
            snake_y[i] = snake_y[i - 1];
        }
        snake_x[0] = nx;
        snake_y[0] = ny;

        if (len > 1) {
            snake_draw_cell(snake_x[1], snake_y[1], (1 % 2) ? 3 : 2, 0);
        }
        snake_draw_cell(snake_x[0], snake_y[0], 1, dir);

        if (score > high_score) high_score = score;
        fill_rect(155, 7, 45, 9, 0);
        draw_int(155, 7, score, 15, 0);
        fill_rect(250, 7, 45, 9, 0);
        draw_int(250, 7, high_score, 11, 0);

        sleep_ticks(delay);
    }

    fill_rect(60, 52, 200, 84, 4);
    draw_rect_border(60, 52, 200, 84, 12);
    draw_rect_border(62, 54, 196, 80, 0);

    draw_string(105, 62, "=== GAME OVER ===", 14, 4);
    draw_string(92, 78, "Final Score: ", 15, 4);
    draw_int(170, 78, score, 10, 4);

    if (score == high_score && score > 0) {
        draw_string(88, 92, "* NEW HIGH SCORE! *", 14, 4);
    }

    draw_string(76, 110, "[ENTER/SPACE] Play Again", 15, 4);
    draw_string(88, 120, "[ESC/Q] Exit to OS", 7, 4);

    while (1) {
        char c = read_key();
        if (c == '\n' || c == ' ') {
            goto play_again;
        } else if (c == 27 || c == 'q' || c == 'Q') {
            gfx_console_start();
            banner();
            return;
        }
    }
}

/* ---------- Text Editor ---------- */

static char ed_buf[FILE_SIZE];

void cmd_edit(const char *arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        print("Usage: edit <filename>\n");
        return;
    }

    char filename[NAME_LEN];
    int fn_len = 0;
    while (arg[fn_len] && arg[fn_len] != ' ' && fn_len < NAME_LEN - 1) {
        filename[fn_len] = arg[fn_len];
        fn_len++;
    }
    filename[fn_len] = 0;

    int file_idx = fs_find(filename);
    if (file_idx < 0) {
        file_idx = fs_create(filename);
        if (file_idx < 0) {
            print("Error: Could not create file '");
            print(filename);
            print("'\n");
            return;
        }
    }

    int ed_len = files[file_idx].size;
    if (ed_len >= FILE_SIZE) ed_len = FILE_SIZE - 1;
    for (int i = 0; i < ed_len; i++) {
        ed_buf[i] = files[file_idx].data[i];
    }
    ed_buf[ed_len] = 0;

    int cursor_pos = ed_len;

    clear_screen();

    while (1) {
        for (int c = 0; c < GCOLS; c++) {
            gtext[0][c] = ' ';
            gcol[0][c] = 0x0B;
        }
        const char *t1 = "MYOS EDIT: ";
        int tc = 1;
        for (int i = 0; t1[i]; i++) gtext[0][tc++] = t1[i];
        for (int i = 0; filename[i]; i++) gtext[0][tc++] = filename[i];

        const char *t2 = " [";
        for (int i = 0; t2[i]; i++) gtext[0][tc++] = t2[i];
        char numbuf[8]; int ni = 0; int tmp = ed_len;
        if (tmp == 0) numbuf[ni++] = '0';
        else { while (tmp > 0) { numbuf[ni++] = '0' + (tmp % 10); tmp /= 10; } }
        while (ni > 0) gtext[0][tc++] = numbuf[--ni];
        const char *t3 = "/1024 chars]";
        for (int i = 0; t3[i]; i++) gtext[0][tc++] = t3[i];

        for (int c = 0; c < GCOLS; c++) {
            gtext[23][c] = ' ';
            gcol[23][c] = 0x08;
        }
        const char *h = "[ARROWS] Move  [ENTER] Line  [ESC] Menu";
        for (int i = 0; h[i] && i < GCOLS - 2; i++) gtext[23][i + 1] = h[i];

        for (int c = 0; c < GCOLS; c++) {
            gtext[24][c] = ' ';
            gcol[24][c] = 0x0A;
        }
        const char *st = "Editing file. Press ESC to Save & Exit.";
        for (int i = 0; st[i] && i < GCOLS - 2; i++) gtext[24][i + 1] = st[i];

        for (int r = 1; r < 23; r++) {
            for (int c = 0; c < GCOLS; c++) {
                gtext[r][c] = ' ';
                gcol[r][c] = 0x0F;
            }
        }

        int cur_row = 1;
        int cur_col = 1;
        int r = 1, c = 1;

        for (int i = 0; i <= ed_len; i++) {
            if (i == cursor_pos) {
                cur_row = r;
                cur_col = c;
            }
            if (i == ed_len) break;

            char ch = ed_buf[i];
            if (ch == '\n') {
                r++;
                c = 1;
                if (r >= 23) break;
            } else {
                if (c < GCOLS - 1) {
                    gtext[r][c] = (uint8_t)ch;
                    c++;
                } else {
                    r++;
                    c = 1;
                    if (r >= 23) break;
                    gtext[r][c] = (uint8_t)ch;
                    c++;
                }
            }
        }

        row = cur_row;
        col = cur_col;
        gfx_redraw();
        gfx_cursor();

        char key = read_key();

        if (key == 27) {
            for (int ci = 0; ci < GCOLS; ci++) {
                gtext[24][ci] = ' ';
                gcol[24][ci] = 0x0E;
            }
            const char *prompt = "Save? (Y)es  (N)o / Discard  (C)ancel: ";
            for (int i = 0; prompt[i] && i < GCOLS - 1; i++) gtext[24][i + 1] = prompt[i];
            row = 24;
            col = 39;
            gfx_redraw();
            gfx_cursor();

            char choice = read_key();
            if (choice == 'y' || choice == 'Y' || choice == '\n') {
                for (int i = 0; i < ed_len; i++) {
                    files[file_idx].data[i] = ed_buf[i];
                }
                files[file_idx].data[ed_len] = 0;
                files[file_idx].size = ed_len;
                files[file_idx].used = 1;

                gfx_console_start();
                banner();
                color = 0x0A;
                print("File '");
                print(filename);
                print("' saved (");
                print_int(ed_len);
                print(" bytes).\n");
                color = theme;
                return;
            } else if (choice == 'n' || choice == 'N' || choice == 'q' || choice == 'Q') {
                gfx_console_start();
                banner();
                color = 0x0C;
                print("Closed without saving.\n");
                color = theme;
                return;
            } else {
                continue;
            }
        } else if (key == '\b') {
            if (cursor_pos > 0) {
                for (int i = cursor_pos - 1; i < ed_len; i++) {
                    ed_buf[i] = ed_buf[i + 1];
                }
                cursor_pos--;
                ed_len--;
                ed_buf[ed_len] = 0;
            }
        } else if (key == 7) {
            if (cursor_pos < ed_len) {
                for (int i = cursor_pos; i < ed_len; i++) {
                    ed_buf[i] = ed_buf[i + 1];
                }
                ed_len--;
                ed_buf[ed_len] = 0;
            }
        } else if (key == 1) {
            int line_start = cursor_pos;
            while (line_start > 0 && ed_buf[line_start - 1] != '\n') {
                line_start--;
            }
            int col_offset = cursor_pos - line_start;
            if (line_start > 0) {
                int prev_line_end = line_start - 1;
                int prev_line_start = prev_line_end;
                while (prev_line_start > 0 && ed_buf[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int prev_len = prev_line_end - prev_line_start;
                if (col_offset > prev_len) col_offset = prev_len;
                cursor_pos = prev_line_start + col_offset;
            }
        } else if (key == 2) {
            int line_start = cursor_pos;
            while (line_start > 0 && ed_buf[line_start - 1] != '\n') line_start--;
            int col_offset = cursor_pos - line_start;

            int line_end = cursor_pos;
            while (line_end < ed_len && ed_buf[line_end] != '\n') line_end++;

            if (line_end < ed_len) {
                int next_line_start = line_end + 1;
                int next_line_end = next_line_start;
                while (next_line_end < ed_len && ed_buf[next_line_end] != '\n') next_line_end++;
                int next_len = next_line_end - next_line_start;
                if (col_offset > next_len) col_offset = next_len;
                cursor_pos = next_line_start + col_offset;
            }
        } else if (key == 3) {
            if (cursor_pos > 0) cursor_pos--;
        } else if (key == 4) {
            if (cursor_pos < ed_len) cursor_pos++;
        } else if (key == 5) {
            while (cursor_pos > 0 && ed_buf[cursor_pos - 1] != '\n') cursor_pos--;
        } else if (key == 6) {
            while (cursor_pos < ed_len && ed_buf[cursor_pos] != '\n') cursor_pos++;
        } else if (key == '\n') {
            if (ed_len < FILE_SIZE - 2) {
                for (int i = ed_len; i >= cursor_pos; i--) {
                    ed_buf[i + 1] = ed_buf[i];
                }
                ed_buf[cursor_pos] = '\n';
                cursor_pos++;
                ed_len++;
                ed_buf[ed_len] = 0;
            }
        } else if (key >= ' ' && key <= '~') {
            if (ed_len < FILE_SIZE - 2) {
                for (int i = ed_len; i >= cursor_pos; i--) {
                    ed_buf[i + 1] = ed_buf[i];
                }
                ed_buf[cursor_pos] = key;
                cursor_pos++;
                ed_len++;
                ed_buf[ed_len] = 0;
            }
        }
    }
}

/* ---------- Sysinfo Screen ---------- */

void get_cpu_info(char *vendor, char *brand) {
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    uint32_t vparts[3];
    vparts[0] = ebx;
    vparts[1] = edx;
    vparts[2] = ecx;
    char *vsrc = (char *)vparts;
    for (int i = 0; i < 12; i++) vendor[i] = vsrc[i];
    vendor[12] = 0;

    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax >= 0x80000004) {
        uint32_t bparts[12];
        __asm__ volatile ("cpuid" : "=a"(bparts[0]), "=b"(bparts[1]), "=c"(bparts[2]), "=d"(bparts[3]) : "a"(0x80000002));
        __asm__ volatile ("cpuid" : "=a"(bparts[4]), "=b"(bparts[5]), "=c"(bparts[6]), "=d"(bparts[7]) : "a"(0x80000003));
        __asm__ volatile ("cpuid" : "=a"(bparts[8]), "=b"(bparts[9]), "=c"(bparts[10]), "=d"(bparts[11]) : "a"(0x80000004));
        char *src = (char *)bparts;
        for (int i = 0; i < 48; i++) brand[i] = src[i];
        brand[48] = 0;

        int s = 0;
        while (brand[s] == ' ') s++;
        if (s > 0) {
            int k = 0;
            while (brand[s + k]) {
                brand[k] = brand[s + k];
                k++;
            }
            brand[k] = 0;
        }
    } else {
        int i = 0;
        for (; vendor[i]; i++) brand[i] = vendor[i];
        const char *comp = " Compatible CPU";
        for (int j = 0; comp[j]; j++) brand[i++] = comp[j];
        brand[i] = 0;
    }
}

void cmd_sysinfo(void) {
    char vendor[16];
    char brand[50];
    get_cpu_info(vendor, brand);

    color = 0x0B;
    print("+---------------------------------------------------+\n");
    print("|              MYOS SYSTEM INFORMATION              |\n");
    print("+---------------------------------------------------+\n");

    color = 0x0E; print(" [CPU & Hardware]\n");
    color = 0x0F; print("   Model:   "); color = 0x0A; print(brand); print("\n");
    color = 0x0F; print("   Vendor:  "); color = 0x0A; print(vendor);
    color = 0x07; print(" (x86 32-bit Protected Mode)\n");

    color = 0x0E; print(" [System Clocks & Uptime]\n");
    color = 0x0F; print("   Uptime:  "); color = 0x0B;
    uint32_t sec = ticks / 100;
    print_int(sec / 3600); print("h ");
    print_int((sec % 3600) / 60); print("m ");
    print_int(sec % 60); print("s ");
    color = 0x07; print("("); print_int(ticks); print(" ticks @ 100Hz)\n");

    color = 0x0F; print("   RTC:     "); color = 0x0B;
    print_2d(rtc_val(0x04)); print(":");
    print_2d(rtc_val(0x02)); print(":");
    print_2d(rtc_val(0x00)); print(" UTC  ");
    print_2d(rtc_val(0x07)); print("/");
    print_2d(rtc_val(0x08)); print("/20");
    print_2d(rtc_val(0x09)); print("\n");

    color = 0x0E; print(" [Memory Allocation (Heap)]\n");
    uint32_t total_kb = (heap_end - heap_start) / 1024;
    uint32_t used_b = (heap_next - heap_start);
    uint32_t free_kb = (heap_end - heap_next) / 1024;
    uint32_t pct = (used_b * 100) / (heap_end - heap_start);
    color = 0x0F; print("   Total:   "); color = 0x07; print_int(total_kb); print(" KB\n");
    color = 0x0F; print("   Used:    "); color = 0x07; print_int(used_b); print(" bytes ("); print_int(pct); print("%)\n");
    color = 0x0F; print("   Free:    "); color = 0x07; print_int(free_kb); print(" KB\n");
    color = 0x0B; print("   [");
    int filled = (pct * 24) / 100;
    if (filled == 0 && used_b > 0) filled = 1;
    for (int i = 0; i < 24; i++) {
        if (i < filled) { color = 0x0A; print("#"); }
        else { color = 0x08; print("."); }
    }
    color = 0x0B; print("]\n");

    color = 0x0E; print(" [Storage (RAM File System)]\n");
    int count = 0, bytes_used = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            count++;
            bytes_used += files[i].size;
        }
    }
    color = 0x0F; print("   Files:   "); color = 0x07; print_int(count); print(" / "); print_int(MAX_FILES); print(" used\n");
    color = 0x0F; print("   Storage: "); color = 0x07; print_int(bytes_used); print(" / "); print_int(MAX_FILES * FILE_SIZE); print(" bytes\n");

    color = 0x0B;
    print("+---------------------------------------------------+\n");
    color = theme;
}

/* ---------- Shell Command History ---------- */
#define HIST_MAX 16
char history[HIST_MAX][80];
int hist_count = 0;
int hist_pos = 0;

void hist_add(const char *cmd) {
    if (cmd[0] == 0)
        return;
    if (hist_count == HIST_MAX) {
        for (int i = 1; i < HIST_MAX; i++)
            for (int k = 0; k < 80; k++)
                history[i - 1][k] = history[i][k];
        hist_count--;
    }
    int k = 0;
    while (cmd[k] && k < 79) {
        history[hist_count][k] = cmd[k];
        k++;
    }
    history[hist_count][k] = 0;
    hist_count++;
}

int parse_token(const char **p, char *out, int max_len) {
    while (**p == ' ') (*p)++;
    int n = 0;
    while (**p && **p != ' ' && n < max_len - 1) {
        out[n++] = *(*p)++;
    }
    out[n] = 0;
    return n;
}

/* ---------- CPU Benchmark ---------- */
void cmd_bench(void) {
    print("Running CPU integer throughput benchmark (1 second)...\n");
    uint32_t start = ticks;
    while (ticks == start);
    start = ticks;
    uint32_t count = 0;
    while ((uint32_t)(ticks - start) < 100) {
        count += 20;
    }
    uint32_t kops = count / 1000;
    color = 0x0E;
    print("Benchmark complete!\n");
    color = 0x0F;
    print("Throughput: ");
    color = 0x0A;
    print_int((int)kops);
    print(" k-loops / sec (x86 32-bit Protected Mode)\n");
    color = theme;
}

/* ---------- Retro Pong Game ---------- */
void pong_game(void) {
    int p_score = 0;
    int c_score = 0;
    const int max_score = 5;

play_pong_again:
    set_mode13h();
    gfx_palette();

    int p_y = 87;
    int c_y = 87;
    int bx = 160, by = 100;
    int bvx = -2, bvy = 1;
    int paused = 0;

    fill_rect(0, 0, 320, 200, 0);
    draw_rect_border(4, 18, 312, 178, 9);
    draw_string(20, 5, "RETRO PONG", 10, 0);
    draw_string(110, 5, "PLAYER:", 15, 0);
    draw_int(160, 5, p_score, 14, 0);
    draw_string(190, 5, "CPU:", 15, 0);
    draw_int(220, 5, c_score, 12, 0);
    draw_string(16, 188, "[ARROWS/W,S] Move  [P] Pause  [ESC] Exit", 7, 0);

    for (int y = 20; y < 194; y += 8) {
        fill_rect(159, y, 2, 4, 8);
    }

    while (has_key()) check_key();

    while (p_score < max_score && c_score < max_score) {
        while (has_key()) {
            char c = check_key();
            if (c == 1 || c == 'w' || c == 'W') {
                if (p_y > 22) p_y -= 6;
            } else if (c == 2 || c == 's' || c == 'S') {
                if (p_y < 166) p_y += 6;
            } else if (c == 'p' || c == 'P' || c == ' ') {
                paused = !paused;
            } else if (c == 27 || c == 'q' || c == 'Q') {
                gfx_console_start();
                banner();
                return;
            }
        }

        if (paused) {
            draw_string(136, 96, "PAUSED", 14, 0);
            sleep_ticks(5);
            continue;
        }

        if (by > c_y + 16 && c_y < 166) c_y += 2;
        else if (by < c_y + 10 && c_y > 22) c_y -= 2;

        fill_rect(10, 20, 5, 174, 0);
        fill_rect(305, 20, 5, 174, 0);
        fill_rect(bx - 2, by - 2, 8, 8, 0);

        for (int y = 20; y < 194; y += 8) {
            fill_rect(159, y, 2, 4, 8);
        }

        bx += bvx;
        by += bvy;

        if (by <= 21) { by = 21; bvy = -bvy; }
        if (by >= 191) { by = 191; bvy = -bvy; }

        if (bx <= 15 && bx >= 10 && by >= p_y - 2 && by <= p_y + 28) {
            bx = 16;
            bvx = -bvx;
            if (by < p_y + 8) bvy = -2;
            else if (by > p_y + 18) bvy = 2;
        }

        if (bx >= 301 && bx <= 306 && by >= c_y - 2 && by <= c_y + 28) {
            bx = 300;
            bvx = -bvx;
            if (by < c_y + 8) bvy = -2;
            else if (by > c_y + 18) bvy = 2;
        }

        if (bx < 8) {
            c_score++;
            bx = 160; by = 100;
            bvx = 2; bvy = 1;
            draw_int(220, 5, c_score, 12, 0);
            sleep_ticks(20);
        } else if (bx > 312) {
            p_score++;
            bx = 160; by = 100;
            bvx = -2; bvy = -1;
            draw_int(160, 5, p_score, 14, 0);
            sleep_ticks(20);
        }

        fill_rect(10, p_y, 5, 26, 10);
        fill_rect(305, c_y, 5, 26, 12);
        fill_rect(bx, by, 4, 4, 14);

        sleep_ticks(2);
    }

    fill_rect(80, 70, 160, 60, 1);
    draw_rect_border(80, 70, 160, 60, 11);
    if (p_score >= max_score) {
        draw_string(115, 80, "YOU WIN! :)", 10, 1);
    } else {
        draw_string(115, 80, "CPU WINS! :(", 12, 1);
    }
    draw_string(95, 100, "[ENTER] Play Again", 15, 1);
    draw_string(105, 112, "[ESC] Exit to OS", 7, 1);

    while (1) {
        char k = read_key();
        if (k == '\n' || k == ' ') {
            p_score = 0; c_score = 0;
            goto play_pong_again;
        } else if (k == 27 || k == 'q' || k == 'Q') {
            gfx_console_start();
            banner();
            return;
        }
    }
}

/* ---------- Matrix Digital Rain Screensaver ---------- */
void matrix_screensaver(void) {
    set_mode13h();
    gfx_palette();
    fill_rect(0, 0, 320, 200, 0);

    const int cols = 40;
    int y_pos[40];
    int len[40];

    for (int i = 0; i < cols; i++) {
        y_pos[i] = -(int)(snake_rand() % 30);
        len[i] = 6 + (snake_rand() % 12);
    }

    while (!has_key()) {
        for (int i = 0; i < cols; i++) {
            int px = i * 8 + 1;
            int head_y = y_pos[i] * 8;

            if (head_y >= 0 && head_y < 192) {
                char rand_char = 'A' + (snake_rand() % 26);
                draw_char(px, head_y, rand_char, 15, 0);
            }
            int neck_y = (y_pos[i] - 1) * 8;
            if (neck_y >= 0 && neck_y < 192) {
                char rand_char = '0' + (snake_rand() % 10);
                draw_char(px, neck_y, rand_char, 10, 0);
            }
            int tail_y = (y_pos[i] - len[i]) * 8;
            if (tail_y >= 0 && tail_y < 192) {
                fill_rect(px, tail_y, 6, 8, 0);
            }

            y_pos[i]++;
            if ((y_pos[i] - len[i]) * 8 >= 200) {
                y_pos[i] = 0;
                len[i] = 6 + (snake_rand() % 12);
            }
        }
        sleep_ticks(3);
    }
    check_key();
    gfx_console_start();
    banner();
}

/* ---------- Desktop Graphical Launcher ---------- */
void desktop_gui(void) {
    int selected = 0;
    const int app_count = 7;
    const char *names[] = {
        "1. Terminal",
        "2. Snake Game",
        "3. Pong Game",
        "4. Text Editor",
        "5. Sysinfo",
        "6. Matrix Rain",
        "7. Benchmark"
    };
    const char *descs[] = {
        "Return to OS command line prompt",
        "Classic arcade snake with high scores",
        "Play 2D Pong arcade game vs CPU",
        "Edit notes file in RAM file system",
        "View CPU, RAM and hardware stats",
        "Digital rain Matrix screensaver",
        "Stress test CPU integer performance"
    };

    set_mode13h();
    gfx_palette();

redraw_gui:
    fill_rect(0, 0, 320, 200, 1);

    fill_rect(0, 0, 320, 14, 7);
    draw_string(6, 3, "MyOS Desktop v0.3", 0, 7);
    draw_int(240, 3, rtc_val(0x04), 0, 7);
    draw_char(252, 3, ':', 0, 7);
    draw_int(258, 3, rtc_val(0x02), 0, 7);
    draw_string(274, 3, "UTC", 0, 7);

    draw_string(20, 24, "APPLICATIONS & SYSTEM TOOLS", 14, 1);
    draw_string(20, 34, "Select an app with [UP/DOWN] and press [ENTER]:", 15, 1);

    for (int i = 0; i < app_count; i++) {
        int y = 48 + i * 18;
        if (i == selected) {
            fill_rect(16, y - 2, 288, 16, 9);
            draw_rect_border(16, y - 2, 288, 16, 15);
            draw_string(24, y + 2, names[i], 14, 9);
            draw_string(118, y + 2, descs[i], 15, 9);
        } else {
            fill_rect(16, y - 2, 288, 16, 0);
            draw_rect_border(16, y - 2, 288, 16, 8);
            draw_string(24, y + 2, names[i], 11, 0);
            draw_string(118, y + 2, descs[i], 7, 0);
        }
    }

    fill_rect(0, 186, 320, 14, 8);
    draw_string(10, 189, "[UP/DOWN] Navigate  [ENTER] Launch  [1-7] Direct  [ESC] Exit", 15, 8);

    while (1) {
        char c = read_key();
        if (c == 1 || c == 'w' || c == 'W') {
            if (selected > 0) { selected--; goto redraw_gui; }
        } else if (c == 2 || c == 's' || c == 'S') {
            if (selected < app_count - 1) { selected++; goto redraw_gui; }
        } else if (c >= '1' && c <= '7') {
            selected = c - '1';
            goto launch_app;
        } else if (c == '\n' || c == ' ') {
            goto launch_app;
        } else if (c == 27 || c == 'q' || c == 'Q') {
            gfx_console_start();
            banner();
            return;
        }
    }

launch_app:
    if (selected == 0) {
        gfx_console_start();
        banner();
        return;
    } else if (selected == 1) {
        snake_game();
        goto redraw_gui;
    } else if (selected == 2) {
        pong_game();
        goto redraw_gui;
    } else if (selected == 3) {
        cmd_edit("notes");
        goto redraw_gui;
    } else if (selected == 4) {
        gfx_console_start();
        cmd_sysinfo();
        print("\nPress any key to return to Desktop...");
        read_key();
        goto redraw_gui;
    } else if (selected == 5) {
        matrix_screensaver();
        goto redraw_gui;
    } else if (selected == 6) {
        gfx_console_start();
        cmd_bench();
        print("\nPress any key to return to Desktop...");
        read_key();
        goto redraw_gui;
    }
}

void run_command(const char *cmd) {
    if (cmd[0] == 0)
        return;

    if (str_equal(cmd, "help")) {
        print("Applications & GUI:\n");
        print("  gui       - graphical desktop application launcher\n");
        print("  snake     - arcade Snake game (arrow keys / WASD)\n");
        print("  pong      - Retro Pong vs smart AI (arrows / W,S)\n");
        print("  matrix    - digital code rain screensaver\n");
        print("  edit F    - full-screen text editor for file F\n");
        print("  sysinfo   - hardware, CPU & memory dashboard\n");
        print("  bench     - CPU integer throughput benchmark\n");
        print("File System:\n");
        print("  ls        - list RAM disk files\n");
        print("  cat F     - display content of file F\n");
        print("  touch F   - create empty file F\n");
        print("  write F T - write text T to file F\n");
        print("  append F T- append text T to file F\n");
        print("  cp S D    - copy file S to D\n");
        print("  mv S D    - rename / move file S to D\n");
        print("  rm F      - delete file F\n");
        print("  wc F      - count lines, words, chars in file F\n");
        print("  head F    - display first 5 lines of file F\n");
        print("  tail F    - display last 5 lines of file F\n");
        print("  find T    - search for text T across all files\n");
        print("Utilities & System:\n");
        print("  calc A+B  - calculator (+ - * /)\n");
        print("  history   - show command line history\n");
        print("  fortune   - famous computing wisdom\n");
        print("  rand [M]  - random number generator (0 to M)\n");
        print("  sleep N   - delay execution for N seconds\n");
        print("  uptime    - seconds since boot\n");
        print("  time/date - RTC real-time clock\n");
        print("  mem/alloc - memory heap stats & allocator test\n");
        print("  color X   - terminal theme color\n");
        print("  gfx       - scenic graphics demo\n");
        print("  clear     - clear screen\n");
        print("  reboot    - restart OS\n");
        print("  shutdown  - power off system\n");
    } else if (str_equal(cmd, "gui") || str_equal(cmd, "desktop")) {
        desktop_gui();
    } else if (str_equal(cmd, "snake")) {
        snake_game();
    } else if (str_equal(cmd, "pong")) {
        pong_game();
    } else if (str_equal(cmd, "matrix")) {
        matrix_screensaver();
    } else if (str_equal(cmd, "bench")) {
        cmd_bench();
    } else if (str_equal(cmd, "history")) {
        if (hist_count == 0) {
            print("No history recorded yet.\n");
        } else {
            for (int i = 0; i < hist_count; i++) {
                print("  "); print_int(i + 1); print(": ");
                print(history[i]); print("\n");
            }
        }
    } else if (str_equal(cmd, "fortune") || str_equal(cmd, "quote")) {
        static const char *quotes[] = {
            "\"Talk is cheap. Show me the code.\" - Linus Torvalds",
            "\"The best way to predict the future is to invent it.\" - Alan Kay",
            "\"Simplicity is prerequisite for reliability.\" - Edsger W. Dijkstra",
            "\"UNIX is simple. It takes a genius to understand its simplicity.\" - Dennis Ritchie",
            "\"First, solve the problem. Then, write the code.\" - John Johnson"
        };
        int q_idx = (int)(snake_rand() % 5);
        color = 0x0E;
        print(quotes[q_idx]);
        print("\n");
        color = theme;
    } else if (starts_with(cmd, "rand")) {
        const char *p = cmd + 4;
        int max = parse_int(&p);
        if (max <= 0) max = 100;
        int r = (int)(snake_rand() % (uint32_t)max);
        print_int(r);
        print("\n");
    } else if (starts_with(cmd, "sleep ")) {
        const char *p = cmd + 6;
        int s = parse_int(&p);
        if (s <= 0) s = 1;
        print("Sleeping "); print_int(s); print(" second(s)...\n");
        sleep_ticks((uint32_t)(s * 100));
        print("Done.\n");
    } else if (starts_with(cmd, "edit ")) {
        cmd_edit(cmd + 5);
    } else if (str_equal(cmd, "edit")) {
        cmd_edit("");
    } else if (str_equal(cmd, "sysinfo")) {
        cmd_sysinfo();
    } else if (str_equal(cmd, "clear")) {
        clear_screen();
    } else if (str_equal(cmd, "about")) {
        color = 0x0E;
        print("MyOS v0.3 - Bare metal operating system with GUI, games & RAM disk.\n");
        color = theme;
    } else if (str_equal(cmd, "uptime")) {
        print("Uptime: ");
        print_int((int)(ticks / 100));
        print(" seconds\n");
    } else if (starts_with(cmd, "echo ")) {
        print(cmd + 5);
        print("\n");
    } else if (starts_with(cmd, "touch ")) {
        int r = fs_create(cmd + 6);
        if (r == -1)      print("Bad file name (1 to 15 characters)\n");
        else if (r == -2) print("File already exists\n");
        else if (r == -3) print("No space left\n");
        else              print("Created\n");
    } else if (str_equal(cmd, "ls")) {
        int count = 0;
        for (int i = 0; i < MAX_FILES; i++) {
            if (files[i].used) {
                print(files[i].name);
                print("   ");
                print_int(files[i].size);
                print(" bytes\n");
                count++;
            }
        }
        if (count == 0)
            print("(no files)\n");
    } else if (starts_with(cmd, "cat ")) {
        int i = fs_find(cmd + 4);
        if (i < 0) {
            print("No such file\n");
        } else {
            print(files[i].data);
            print("\n");
        }
    } else if (starts_with(cmd, "rm ")) {
        int i = fs_find(cmd + 3);
        if (i < 0) {
            print("No such file\n");
        } else {
            files[i].used = 0;
            print("Removed\n");
        }
    } else if (starts_with(cmd, "cp ")) {
        const char *p = cmd + 3;
        char src[NAME_LEN], dst[NAME_LEN];
        if (parse_token(&p, src, NAME_LEN) == 0 || parse_token(&p, dst, NAME_LEN) == 0) {
            print("Usage: cp <source> <dest>\n");
        } else {
            int s = fs_find(src);
            if (s < 0) {
                print("Source file not found\n");
            } else {
                int d = fs_create(dst);
                if (d < 0) {
                    print("Cannot create destination (exists or full)\n");
                } else {
                    for (int i = 0; i < files[s].size; i++)
                        files[d].data[i] = files[s].data[i];
                    files[d].data[files[s].size] = 0;
                    files[d].size = files[s].size;
                    print("Copied '"); print(src); print("' -> '"); print(dst); print("'\n");
                }
            }
        }
    } else if (starts_with(cmd, "mv ")) {
        const char *p = cmd + 3;
        char src[NAME_LEN], dst[NAME_LEN];
        if (parse_token(&p, src, NAME_LEN) == 0 || parse_token(&p, dst, NAME_LEN) == 0) {
            print("Usage: mv <old> <new>\n");
        } else {
            int s = fs_find(src);
            if (s < 0) {
                print("File not found\n");
            } else if (fs_find(dst) >= 0) {
                print("Destination already exists\n");
            } else {
                for (int i = 0; i < NAME_LEN; i++) files[s].name[i] = 0;
                for (int i = 0; dst[i] && i < NAME_LEN - 1; i++) files[s].name[i] = dst[i];
                print("Renamed '"); print(src); print("' -> '"); print(dst); print("'\n");
            }
        }
    } else if (starts_with(cmd, "wc ")) {
        int idx = fs_find(cmd + 3);
        if (idx < 0) {
            print("File not found\n");
        } else {
            int lines = 0, words = 0, chars = files[idx].size;
            int in_word = 0;
            for (int i = 0; i < chars; i++) {
                char ch = files[idx].data[i];
                if (ch == '\n') lines++;
                if (ch == ' ' || ch == '\n' || ch == '\t') {
                    in_word = 0;
                } else if (!in_word) {
                    in_word = 1;
                    words++;
                }
            }
            if (chars > 0 && files[idx].data[chars - 1] != '\n') lines++;
            print("Lines: "); print_int(lines);
            print("  Words: "); print_int(words);
            print("  Chars: "); print_int(chars);
            print("  File: "); print(files[idx].name);
            print("\n");
        }
    } else if (starts_with(cmd, "head ")) {
        int idx = fs_find(cmd + 5);
        if (idx < 0) {
            print("File not found\n");
        } else {
            int line_count = 0;
            for (int i = 0; i < files[idx].size; i++) {
                print_char(files[idx].data[i]);
                if (files[idx].data[i] == '\n') {
                    line_count++;
                    if (line_count >= 5) break;
                }
            }
            if (files[idx].size > 0 && files[idx].data[files[idx].size - 1] != '\n')
                print_char('\n');
        }
    } else if (starts_with(cmd, "tail ")) {
        int idx = fs_find(cmd + 5);
        if (idx < 0) {
            print("File not found\n");
        } else {
            int total_lines = 0;
            for (int i = 0; i < files[idx].size; i++) {
                if (files[idx].data[i] == '\n') total_lines++;
            }
            int skip_lines = (total_lines > 5) ? (total_lines - 5) : 0;
            int cur_line = 0;
            for (int i = 0; i < files[idx].size; i++) {
                if (cur_line >= skip_lines) {
                    print_char(files[idx].data[i]);
                }
                if (files[idx].data[i] == '\n') cur_line++;
            }
            if (files[idx].size > 0 && files[idx].data[files[idx].size - 1] != '\n')
                print_char('\n');
        }
    } else if (starts_with(cmd, "find ")) {
        const char *query = cmd + 5;
        while (*query == ' ') query++;
        if (*query == 0) {
            print("Usage: find <text>\n");
        } else {
            int matches = 0;
            int qlen = str_len(query);
            for (int f = 0; f < MAX_FILES; f++) {
                if (!files[f].used) continue;
                for (int i = 0; i + qlen <= files[f].size; i++) {
                    int match = 1;
                    for (int k = 0; k < qlen; k++) {
                        if (files[f].data[i + k] != query[k]) { match = 0; break; }
                    }
                    if (match) {
                        matches++;
                        color = 0x0E; print(files[f].name); color = 0x07; print(": match at offset ");
                        print_int(i); print("\n");
                        break;
                    }
                }
            }
            if (matches == 0) print("No matches found\n");
        }
    } else if (starts_with(cmd, "write ")) {
        char name[NAME_LEN];
        const char *p = cmd + 6;
        int n = 0;
        while (*p && *p != ' ' && n < NAME_LEN - 1)
            name[n++] = *p++;
        name[n] = 0;
        if (*p == ' ')
            p++;
        int i = fs_find(name);
        if (i < 0) {
            print("No such file (use touch first)\n");
        } else {
            int s = 0;
            while (p[s] && s < FILE_SIZE - 1) {
                files[i].data[s] = p[s];
                s++;
            }
            files[i].data[s] = 0;
            files[i].size = s;
            print("Saved\n");
        }
    } else if (starts_with(cmd, "append ")) {
        char name[NAME_LEN];
        const char *p = cmd + 7;
        int n = 0;
        while (*p && *p != ' ' && n < NAME_LEN - 1)
            name[n++] = *p++;
        name[n] = 0;
        if (*p == ' ')
            p++;
        int i = fs_find(name);
        if (i < 0) {
            print("No such file (use touch first)\n");
        } else {
            int s = files[i].size;
            if (s > 0 && s < FILE_SIZE - 1)
                files[i].data[s++] = ' ';
            int k = 0;
            while (p[k] && s < FILE_SIZE - 1)
                files[i].data[s++] = p[k++];
            files[i].data[s] = 0;
            files[i].size = s;
            print("Appended\n");
        }
    } else if (starts_with(cmd, "calc ")) {
        const char *p = cmd + 5;
        int a = parse_int(&p);
        while (*p == ' ')
            p++;
        char op = *p;
        if (op == 0) {
            print("Usage: calc 12*5\n");
        } else {
            p++;
            int b = parse_int(&p);
            int r = 0;
            int ok = 1;
            if (op == '+')      r = a + b;
            else if (op == '-') r = a - b;
            else if (op == '*') r = a * b;
            else if (op == '/') {
                if (b == 0) {
                    print("Cannot divide by zero\n");
                    ok = 0;
                } else {
                    r = a / b;
                }
            } else {
                print("Use + - * or /\n");
                ok = 0;
            }
            if (ok) {
                if (r < 0) {
                    print("-");
                    r = -r;
                }
                print_int(r);
                print("\n");
            }
        }
    } else if (str_equal(cmd, "time")) {
        print_2d(rtc_val(0x04));
        print(":");
        print_2d(rtc_val(0x02));
        print(":");
        print_2d(rtc_val(0x00));
        print(" UTC\n");
    } else if (str_equal(cmd, "date")) {
        print_2d(rtc_val(0x07));
        print("/");
        print_2d(rtc_val(0x08));
        print("/20");
        print_2d(rtc_val(0x09));
        print("\n");
    } else if (str_equal(cmd, "mem")) {
        print("Heap total: ");
        print_int((int)((heap_end - heap_start) / 1024));
        print(" KB\n");
        print("Heap used:  ");
        print_int((int)(heap_next - heap_start));
        print(" bytes\n");
        print("Heap free:  ");
        print_int((int)((heap_end - heap_next) / 1024));
        print(" KB\n");
    } else if (str_equal(cmd, "gfx")) {
        gfx_demo();
    } else if (str_equal(cmd, "reboot")) {
        print("Rebooting...\n");
        uint8_t status = 0x02;
        while (status & 0x02)
            status = inb(0x64);
        outb(0x64, 0xFE);
        while (1)
            __asm__ volatile ("hlt");
    } else if (str_equal(cmd, "shutdown")) {
        print("Shutting down...\n");
        __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
        __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
        print("It is now safe to turn off.\n");
        __asm__ volatile ("cli");
        while (1)
            __asm__ volatile ("hlt");
    } else if (starts_with(cmd, "color ")) {
        const char *n = cmd + 6;
        if (str_equal(n, "green"))       theme = 0x0A;
        else if (str_equal(n, "red"))    theme = 0x0C;
        else if (str_equal(n, "blue"))   theme = 0x09;
        else if (str_equal(n, "yellow")) theme = 0x0E;
        else if (str_equal(n, "white"))  theme = 0x0F;
        else if (str_equal(n, "cyan"))   theme = 0x0B;
        else if (str_equal(n, "pink"))   theme = 0x0D;
        else {
            print("Colors: green red blue yellow white cyan pink\n");
        }
        color = theme;
    } else if (str_equal(cmd, "alloc")) {
        void *p = kmalloc(64);
        if (p) {
            print("Allocated 64 bytes at ");
            print_hex((uint32_t)p);
            print("\n");
            print("Heap used: ");
            print_int((int)(heap_next - heap_start));
            print(" bytes\n");
        } else {
            print("Out of memory\n");
        }
    } else {
        color = 0x0C;
        print("Unknown command: ");
        print(cmd);
        print("\n");
        color = theme;
    }
}


void replace_line(char *buf, int *len, const char *text) {
    while (*len > 0) {
        print_char('\b');
        (*len)--;
    }
    while (text[*len] && *len < 78) {
        buf[*len] = text[*len];
        print_char(text[*len]);
        (*len)++;
    }
}

void kernel_main(void) {
    char buf[80];
    int len = 0;

    gdt_init();
    clear_screen();
    pic_init();
    pit_init();
    idt_init();
    __asm__ volatile ("sti");

    gfx_console_start();
    fs_init();
    banner();
    print("> ");

    while (1) {
        char c = read_key();
        if (c == '\n') {
            print_char('\n');
            buf[len] = 0;
            hist_add(buf);
            hist_pos = hist_count;
            run_command(buf);
            len = 0;
            print("> ");
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                print_char('\b');
            }
        } else if (c == 1) {
            if (hist_pos > 0) {
                hist_pos--;
                replace_line(buf, &len, history[hist_pos]);
            }
        } else if (c == 2) {
            if (hist_pos < hist_count) {
                hist_pos++;
                if (hist_pos == hist_count)
                    replace_line(buf, &len, "");
                else
                    replace_line(buf, &len, history[hist_pos]);
            }
        } else if (c >= ' ' && len < 78) {
            buf[len++] = c;
            print_char(c);
        }
    }
}