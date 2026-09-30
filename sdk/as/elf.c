/* elf.c : ELF32 big-endian relocatable object writer for ColdFire */

#include "as.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************
 * ELF constants
 ****************************************************************/

#define EI_NIDENT   16
#define ELFMAG      "\177ELF"

#define ELFCLASS32  1
#define ELFDATA2MSB 2
#define EV_CURRENT  1
#define ELFOSABI_NONE 0

#define ET_REL      1
#define EM_68K      4

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8

#define SHF_WRITE    0x1
#define SHF_ALLOC    0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL    0
#define STB_GLOBAL   1

#define STT_NOTYPE   0
#define STT_SECTION  3

#define SHN_UNDEF    0
#define SHN_ABS      0xFFF1

#define R_68K_32     1

#define ELF32_ST_INFO(b, t) (((b) << 4) | ((t) & 0xF))

/****************************************************************
 * Big-endian write helpers
 ****************************************************************/

static void
put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void
put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* sh_addralign for a section: the strongest .align it saw, but never below
   the 4-byte default the writer has always used. */
static uint32_t
sec_addralign(const struct section *s)
{
    return s->align > 4 ? (uint32_t)s->align : 4;
}

/****************************************************************
 * String table builder
 ****************************************************************/

struct strtab {
    char *data;
    int len;
    int cap;
};

static void
strtab_init(struct strtab *st)
{
    st->cap = 256;
    st->data = xmalloc(st->cap);
    st->data[0] = '\0';
    st->len = 1;
}

static int
strtab_add(struct strtab *st, const char *s)
{
    int slen = (int)strlen(s) + 1;
    int pos = st->len;

    while (st->len + slen > st->cap) {
        st->cap *= 2;
        st->data = realloc(st->data, st->cap);
    }
    memcpy(st->data + st->len, s, slen);
    st->len += slen;
    return pos;
}

/****************************************************************
 * ELF symbol entry builder
 ****************************************************************/

struct elf_sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
};

static void
write_sym(uint8_t *buf, struct elf_sym *s)
{
    put32(buf + 0,  s->st_name);
    put32(buf + 4,  s->st_value);
    put32(buf + 8,  s->st_size);
    buf[12] = s->st_info;
    buf[13] = s->st_other;
    put16(buf + 14, s->st_shndx);
}

/****************************************************************
 * Section indices
 ****************************************************************/

enum {
    SH_NULL,
    SH_TEXT,
    SH_DATA,
    SH_BSS,
    SH_SYMTAB,
    SH_STRTAB,
    SH_RELA_TEXT,
    SH_RELA_DATA,
    SH_SHSTRTAB,
    SH_COUNT,
};

/****************************************************************
 * ELF writer
 ****************************************************************/

void
elf_write(struct assembler *a, FILE *out)
{
    struct strtab shstrtab;
    struct strtab strtab;
    int shname[SH_COUNT];
    uint8_t ehdr[52];
    uint8_t shdr[SH_COUNT][40];
    uint32_t offset;
    int text_len, data_len;
    int has_rela_text, has_rela_data;
    int nsyms_local, nsyms_total;
    int k;

    /* section names */
    strtab_init(&shstrtab);
    shname[SH_NULL] = 0;
    shname[SH_TEXT] = strtab_add(&shstrtab, ".text");
    shname[SH_DATA] = strtab_add(&shstrtab, ".data");
    shname[SH_BSS] = strtab_add(&shstrtab, ".bss");
    shname[SH_SYMTAB] = strtab_add(&shstrtab, ".symtab");
    shname[SH_STRTAB] = strtab_add(&shstrtab, ".strtab");
    shname[SH_RELA_TEXT] = strtab_add(&shstrtab, ".rela.text");
    shname[SH_RELA_DATA] = strtab_add(&shstrtab, ".rela.data");
    shname[SH_SHSTRTAB] = strtab_add(&shstrtab, ".shstrtab");

    text_len = a->sections[SEC_TEXT].len;
    data_len = a->sections[SEC_DATA].len;
    has_rela_text = a->sections[SEC_TEXT].nrelocs > 0;
    has_rela_data = a->sections[SEC_DATA].nrelocs > 0;

    /*
     * Build ELF symbol table.
     *
     * Order: null sym, section syms (.text, .data, .bss),
     * local defined symbols, then global/undefined symbols.
     * Track the mapping from assembler sym index to ELF sym index
     * so relocations can reference the right entry.
     */
    strtab_init(&strtab);

    int *sym_map = xmalloc(a->st.nsyms * sizeof(int));

    struct elf_sym *esyms;
    int nesyms = 0;
    int esym_cap = 4 + a->st.nsyms;
    esyms = xmalloc(esym_cap * sizeof(struct elf_sym));

    /* null symbol */
    memset(&esyms[nesyms], 0, sizeof(struct elf_sym));
    nesyms++;

    /* section symbols */
    int sec_sym_text, sec_sym_data, sec_sym_bss;

    esyms[nesyms] = (struct elf_sym){
        .st_name = 0, .st_value = 0, .st_size = 0,
        .st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_other = 0, .st_shndx = SH_TEXT,
    };
    sec_sym_text = nesyms++;

    esyms[nesyms] = (struct elf_sym){
        .st_name = 0, .st_value = 0, .st_size = 0,
        .st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_other = 0, .st_shndx = SH_DATA,
    };
    sec_sym_data = nesyms++;

    esyms[nesyms] = (struct elf_sym){
        .st_name = 0, .st_value = 0, .st_size = 0,
        .st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_other = 0, .st_shndx = SH_BSS,
    };
    sec_sym_bss = nesyms++;

    /* local defined symbols */
    for (k = 0; k < a->st.nsyms; k++) {
        if (a->st.syms[k].global || !a->st.syms[k].defined)
            continue;
        uint16_t shndx;
        switch (a->st.syms[k].section) {
        case SEC_TEXT: shndx = SH_TEXT; break;
        case SEC_DATA: shndx = SH_DATA; break;
        case SEC_BSS:  shndx = SH_BSS; break;
        default:       shndx = SHN_UNDEF; break;
        }
        esyms[nesyms] = (struct elf_sym){
            .st_name = (uint32_t)strtab_add(&strtab, a->st.syms[k].name),
            .st_value = a->st.syms[k].value,
            .st_size = 0,
            .st_info = ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE),
            .st_other = 0,
            .st_shndx = shndx,
        };
        sym_map[k] = nesyms++;
    }

    nsyms_local = nesyms;

    /* global and undefined symbols */
    for (k = 0; k < a->st.nsyms; k++) {
        if (!a->st.syms[k].global && a->st.syms[k].defined)
            continue;
        if (!a->st.syms[k].global && !a->st.syms[k].defined) {
            /* undefined external */
            esyms[nesyms] = (struct elf_sym){
                .st_name = (uint32_t)strtab_add(&strtab, a->st.syms[k].name),
                .st_value = 0,
                .st_size = 0,
                .st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                .st_other = 0,
                .st_shndx = SHN_UNDEF,
            };
            sym_map[k] = nesyms++;
            continue;
        }
        uint16_t shndx;
        if (!a->st.syms[k].defined) {
            shndx = SHN_UNDEF;
        } else {
            switch (a->st.syms[k].section) {
            case SEC_TEXT: shndx = SH_TEXT; break;
            case SEC_DATA: shndx = SH_DATA; break;
            case SEC_BSS:  shndx = SH_BSS; break;
            default:       shndx = SHN_UNDEF; break;
            }
        }
        esyms[nesyms] = (struct elf_sym){
            .st_name = (uint32_t)strtab_add(&strtab, a->st.syms[k].name),
            .st_value = a->st.syms[k].value,
            .st_size = 0,
            .st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE),
            .st_other = 0,
            .st_shndx = shndx,
        };
        sym_map[k] = nesyms++;
    }

    nsyms_total = nesyms;

    /*
     * Build RELA entries, remapping sym indices through sym_map.
     * For defined symbols that reference a section, use the section
     * symbol and encode the symbol value as the addend.
     */
    int rela_text_size = a->sections[SEC_TEXT].nrelocs * 12;
    int rela_data_size = a->sections[SEC_DATA].nrelocs * 12;

    uint8_t *rela_text_buf = NULL;
    uint8_t *rela_data_buf = NULL;

    if (has_rela_text)
        rela_text_buf = xmalloc(rela_text_size);
    if (has_rela_data)
        rela_data_buf = xmalloc(rela_data_size);

    for (k = 0; k < a->sections[SEC_TEXT].nrelocs; k++) {
        struct reloc *r = &a->sections[SEC_TEXT].relocs[k];
        uint8_t *p = rela_text_buf + k * 12;
        int elf_sym_idx = sym_map[r->sym_idx];
        int32_t addend = r->addend;

        if (a->st.syms[r->sym_idx].defined && !a->st.syms[r->sym_idx].global) {
            int sec_sym;
            switch (a->st.syms[r->sym_idx].section) {
            case SEC_TEXT: sec_sym = sec_sym_text; break;
            case SEC_DATA: sec_sym = sec_sym_data; break;
            case SEC_BSS:  sec_sym = sec_sym_bss; break;
            default:       sec_sym = 0; break;
            }
            addend += (int32_t)a->st.syms[r->sym_idx].value;
            elf_sym_idx = sec_sym;
        }

        put32(p + 0, r->offset);
        put32(p + 4, (uint32_t)((elf_sym_idx << 8) | R_68K_32));
        put32(p + 8, (uint32_t)addend);
    }

    for (k = 0; k < a->sections[SEC_DATA].nrelocs; k++) {
        struct reloc *r = &a->sections[SEC_DATA].relocs[k];
        uint8_t *p = rela_data_buf + k * 12;
        int elf_sym_idx = sym_map[r->sym_idx];
        int32_t addend = r->addend;

        if (a->st.syms[r->sym_idx].defined && !a->st.syms[r->sym_idx].global) {
            int sec_sym;
            switch (a->st.syms[r->sym_idx].section) {
            case SEC_TEXT: sec_sym = sec_sym_text; break;
            case SEC_DATA: sec_sym = sec_sym_data; break;
            case SEC_BSS:  sec_sym = sec_sym_bss; break;
            default:       sec_sym = 0; break;
            }
            addend += (int32_t)a->st.syms[r->sym_idx].value;
            elf_sym_idx = sec_sym;
        }

        put32(p + 0, r->offset);
        put32(p + 4, (uint32_t)((elf_sym_idx << 8) | R_68K_32));
        put32(p + 8, (uint32_t)addend);
    }

    /*
     * Compute section offsets.
     * Layout: ehdr, .text, .data, .symtab, .strtab,
     *         .rela.text, .rela.data, .shstrtab, section headers
     */
    int symtab_size = nsyms_total * 16;

    offset = 52;

    uint32_t text_off = offset;
    offset += (uint32_t)text_len;

    uint32_t data_off = offset;
    offset += (uint32_t)data_len;

    uint32_t symtab_off = offset;
    offset += (uint32_t)symtab_size;

    uint32_t strtab_off = offset;
    offset += (uint32_t)strtab.len;

    uint32_t rela_text_off = offset;
    if (has_rela_text)
        offset += (uint32_t)rela_text_size;

    uint32_t rela_data_off = offset;
    if (has_rela_data)
        offset += (uint32_t)rela_data_size;

    uint32_t shstrtab_off = offset;
    offset += (uint32_t)shstrtab.len;

    uint32_t shdr_off = offset;

    /* ELF header */
    memset(ehdr, 0, sizeof(ehdr));
    memcpy(ehdr, ELFMAG, 4);
    ehdr[4] = ELFCLASS32;
    ehdr[5] = ELFDATA2MSB;
    ehdr[6] = EV_CURRENT;
    ehdr[7] = ELFOSABI_NONE;
    put16(ehdr + 16, ET_REL);
    put16(ehdr + 18, EM_68K);
    put32(ehdr + 20, EV_CURRENT);
    put32(ehdr + 24, 0);           /* e_entry */
    put32(ehdr + 28, 0);           /* e_phoff */
    put32(ehdr + 32, shdr_off);    /* e_shoff */
    put32(ehdr + 36, 0);           /* e_flags */
    put16(ehdr + 40, 52);          /* e_ehsize */
    put16(ehdr + 42, 0);           /* e_phentsize */
    put16(ehdr + 44, 0);           /* e_phnum */
    put16(ehdr + 46, 40);          /* e_shentsize */
    put16(ehdr + 48, SH_COUNT);    /* e_shnum */
    put16(ehdr + 50, SH_SHSTRTAB);/* e_shstrndx */

    /* Section headers */
    memset(shdr, 0, sizeof(shdr));

    /* SH_NULL — already zeroed */

    /* SH_TEXT */
    put32(shdr[SH_TEXT] + 0, (uint32_t)shname[SH_TEXT]);
    put32(shdr[SH_TEXT] + 4, SHT_PROGBITS);
    put32(shdr[SH_TEXT] + 8, SHF_ALLOC | SHF_EXECINSTR);
    put32(shdr[SH_TEXT] + 16, text_off);
    put32(shdr[SH_TEXT] + 20, (uint32_t)text_len);
    put32(shdr[SH_TEXT] + 32, sec_addralign(&a->sections[SEC_TEXT]));

    /* SH_DATA */
    put32(shdr[SH_DATA] + 0, (uint32_t)shname[SH_DATA]);
    put32(shdr[SH_DATA] + 4, SHT_PROGBITS);
    put32(shdr[SH_DATA] + 8, SHF_WRITE | SHF_ALLOC);
    put32(shdr[SH_DATA] + 16, data_off);
    put32(shdr[SH_DATA] + 20, (uint32_t)data_len);
    put32(shdr[SH_DATA] + 32, sec_addralign(&a->sections[SEC_DATA]));

    /* SH_BSS */
    put32(shdr[SH_BSS] + 0, (uint32_t)shname[SH_BSS]);
    put32(shdr[SH_BSS] + 4, SHT_NOBITS);
    put32(shdr[SH_BSS] + 8, SHF_WRITE | SHF_ALLOC);
    put32(shdr[SH_BSS] + 16, data_off + (uint32_t)data_len);
    put32(shdr[SH_BSS] + 20, (uint32_t)a->sections[SEC_BSS].len);
    put32(shdr[SH_BSS] + 32, sec_addralign(&a->sections[SEC_BSS]));

    /* SH_SYMTAB */
    put32(shdr[SH_SYMTAB] + 0, (uint32_t)shname[SH_SYMTAB]);
    put32(shdr[SH_SYMTAB] + 4, SHT_SYMTAB);
    put32(shdr[SH_SYMTAB] + 16, symtab_off);
    put32(shdr[SH_SYMTAB] + 20, (uint32_t)symtab_size);
    put32(shdr[SH_SYMTAB] + 24, SH_STRTAB);  /* sh_link */
    put32(shdr[SH_SYMTAB] + 28, (uint32_t)nsyms_local); /* sh_info */
    put32(shdr[SH_SYMTAB] + 32, 4);  /* sh_addralign */
    put32(shdr[SH_SYMTAB] + 36, 16); /* sh_entsize */

    /* SH_STRTAB */
    put32(shdr[SH_STRTAB] + 0, (uint32_t)shname[SH_STRTAB]);
    put32(shdr[SH_STRTAB] + 4, SHT_STRTAB);
    put32(shdr[SH_STRTAB] + 16, strtab_off);
    put32(shdr[SH_STRTAB] + 20, (uint32_t)strtab.len);
    put32(shdr[SH_STRTAB] + 36, 1);

    /* SH_RELA_TEXT */
    put32(shdr[SH_RELA_TEXT] + 0, (uint32_t)shname[SH_RELA_TEXT]);
    put32(shdr[SH_RELA_TEXT] + 4, SHT_RELA);
    put32(shdr[SH_RELA_TEXT] + 16, rela_text_off);
    put32(shdr[SH_RELA_TEXT] + 20, has_rela_text ? (uint32_t)rela_text_size : 0);
    put32(shdr[SH_RELA_TEXT] + 24, SH_SYMTAB);  /* sh_link */
    put32(shdr[SH_RELA_TEXT] + 28, SH_TEXT);     /* sh_info */
    put32(shdr[SH_RELA_TEXT] + 32, 4);  /* sh_addralign */
    put32(shdr[SH_RELA_TEXT] + 36, 12); /* sh_entsize */

    /* SH_RELA_DATA */
    put32(shdr[SH_RELA_DATA] + 0, (uint32_t)shname[SH_RELA_DATA]);
    put32(shdr[SH_RELA_DATA] + 4, SHT_RELA);
    put32(shdr[SH_RELA_DATA] + 16, rela_data_off);
    put32(shdr[SH_RELA_DATA] + 20, has_rela_data ? (uint32_t)rela_data_size : 0);
    put32(shdr[SH_RELA_DATA] + 24, SH_SYMTAB);
    put32(shdr[SH_RELA_DATA] + 28, SH_DATA);
    put32(shdr[SH_RELA_DATA] + 32, 4);
    put32(shdr[SH_RELA_DATA] + 36, 12);

    /* SH_SHSTRTAB */
    put32(shdr[SH_SHSTRTAB] + 0, (uint32_t)shname[SH_SHSTRTAB]);
    put32(shdr[SH_SHSTRTAB] + 4, SHT_STRTAB);
    put32(shdr[SH_SHSTRTAB] + 16, shstrtab_off);
    put32(shdr[SH_SHSTRTAB] + 20, (uint32_t)shstrtab.len);
    put32(shdr[SH_SHSTRTAB] + 36, 1);

    /* Write everything */
    fwrite(ehdr, 1, 52, out);

    if (text_len > 0)
        fwrite(a->sections[SEC_TEXT].data, 1, text_len, out);
    if (data_len > 0)
        fwrite(a->sections[SEC_DATA].data, 1, data_len, out);

    /* Write symtab */
    for (k = 0; k < nsyms_total; k++) {
        uint8_t sbuf[16];
        write_sym(sbuf, &esyms[k]);
        fwrite(sbuf, 1, 16, out);
    }

    /* Write strtab */
    fwrite(strtab.data, 1, strtab.len, out);

    /* Write rela sections */
    if (has_rela_text)
        fwrite(rela_text_buf, 1, rela_text_size, out);
    if (has_rela_data)
        fwrite(rela_data_buf, 1, rela_data_size, out);

    /* Write shstrtab */
    fwrite(shstrtab.data, 1, shstrtab.len, out);

    /* Write section header table */
    for (k = 0; k < SH_COUNT; k++)
        fwrite(shdr[k], 1, 40, out);

    /* Cleanup */
    free(sym_map);
    free(esyms);
    free(rela_text_buf);
    free(rela_data_buf);
    free(strtab.data);
    free(shstrtab.data);
}
